"""Read the two CSV formats emitted by the BSS138 firmware."""

from dataclasses import dataclass
from math import isfinite


VGS_HEADER = (
    "VGS_Set,VGS_Measured,VSupply,VDrain,ID_uA,"
    "time_ms,sample_id,sweep_id,point_index"
)
VDS_HEADER = (
    "VGS_Set,VGS_Measured,VSupply_Set,VSupply,VDS_Measured,ID_uA,"
    "time_ms,sample_id,sweep_id,point_index"
)
HEADERS = {VGS_HEADER: "VGS", VDS_HEADER: "VDS"}
CONTROL_LINES = {"READY", "ERR COMMAND"} | {
    f"{event} {mode}" for event in ("RUN", "DONE") for mode in range(4)
}
EXPECTED_POINTS = 512
RESISTOR_OHM = 10_000
VOLTAGE_TOLERANCE = 0.15
CURRENT_CHECK_TOLERANCE = 0.02
NEGATIVE_CURRENT_LIMIT = -2.0


@dataclass(frozen=True)
class Sample:
    gate_set: float
    gate: float
    supply: float
    drain: float
    current: float
    time_ms: int
    sample_id: int
    sweep_id: int
    point_index: int
    supply_set: float | None = None

    @property
    def calculated_current(self):
        return (self.supply - self.drain) * 1_000_000 / RESISTOR_OHM

    @property
    def supply_error(self):
        if self.supply_set is not None:
            return abs(self.supply - self.supply_set)
        return None

    @property
    def warning(self):
        return (
            abs(self.gate - self.gate_set) > VOLTAGE_TOLERANCE
            or (self.supply_error is not None and self.supply_error > VOLTAGE_TOLERANCE)
            or abs(self.current - self.calculated_current) > CURRENT_CHECK_TOLERANCE
            or self.current < NEGATIVE_CURRENT_LIMIT
        )


def parse_sample(line, mode):
    fields = line.split(",")
    voltage_count = 5 if mode == "VDS" else 4
    if len(fields) != voltage_count + 5:
        raise ValueError("unexpected CSV field count")

    values = tuple(map(float, fields[:voltage_count + 1]))
    if not all(map(isfinite, values)):
        raise ValueError("non-finite measurement")
    time_ms, sample_id, sweep_id, point_index = map(int, fields[-4:])
    if min(time_ms, sample_id, point_index) < 0 or sweep_id < 1:
        raise ValueError("invalid sample indices")

    if mode == "VDS":
        gate_set, gate, supply_set, supply, drain, current = values
    else:
        gate_set, gate, supply, drain, current = values
        supply_set = None
    return Sample(gate_set, gate, supply, drain, current, time_ms,
                  sample_id, sweep_id, point_index, supply_set)


class Capture:
    def __init__(self):
        self.header = ""
        self.mode = None
        self.samples = {}
        self.rows = {}
        self.latest = None
        self.errors = 0
        self.duplicates = 0
        self.warnings = 0
        self.received_bytes = 0
        self.buffer = bytearray()
        self.discard_line = False
        self.events = []

    def feed(self, chunk):
        """Return whether the chart or its header changed."""
        self.received_bytes += len(chunk)
        self.events.clear()
        self.buffer.extend(chunk)
        changed = False
        while (end := self.buffer.find(b"\n")) >= 0:
            raw = bytes(self.buffer[:end])
            del self.buffer[:end + 1]
            if self.discard_line:
                self.discard_line = False
                continue
            if len(raw) > 512:
                self.errors += 1
                continue
            try:
                line = raw.decode("ascii").strip()
            except UnicodeDecodeError:
                self.errors += 1
                continue
            if line in CONTROL_LINES:
                self.events.append(line)
            elif line in HEADERS:
                self.header = line
                self.mode = HEADERS[line]
                self.samples.clear()
                self.rows.clear()
                self.latest = None
                self.errors = self.duplicates = self.warnings = 0
                changed = True
            elif line and self.mode:
                changed = self.accept(line) or changed

        if len(self.buffer) > 512:
            self.buffer.clear()
            self.discard_line = True
            self.errors += 1
        return changed

    def accept(self, line):
        try:
            sample = parse_sample(line, self.mode)
        except ValueError:
            self.errors += 1
            return False
        if sample.sample_id in self.samples:
            self.duplicates += 1
            return False
        self.samples[sample.sample_id] = sample
        self.rows[sample.sample_id] = line
        self.latest = sample
        self.warnings += sample.warning
        return True

    @property
    def missing(self):
        return max(self.samples, default=-1) + 1 - len(self.samples)

    def curves(self):
        groups = {}
        for sample in self.samples.values():
            groups.setdefault(sample.sweep_id, []).append(sample)
        for sweep_id, points in sorted(groups.items()):
            yield sweep_id, sorted(points, key=lambda sample: sample.point_index)
