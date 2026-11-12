"""Live BSS138 UART chart. Run with Python 3.10 or later."""

import tkinter as tk
from pathlib import Path
from statistics import median
from time import monotonic
from tkinter import filedialog, messagebox, ttk

import serial
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk
from matplotlib.figure import Figure
from serial.tools import list_ports

from protocol import (
    Capture, CURRENT_CHECK_TOLERANCE, EXPECTED_POINTS, NEGATIVE_CURRENT_LIMIT,
    VOLTAGE_TOLERANCE,
)


BAUD_RATE = 115200
MODES = (
    "0 · 전달 특성 (공급 2.8 V)",
    "1 · 출력 특성 (게이트 1.0 V)",
    "2 · 출력 특성 (게이트 전압 비교)",
    "3 · 전달 특성 (공급 전압 비교)",
)


class Monitor:
    def __init__(self, root):
        self.root = root
        self.port = None
        self.capture = Capture()
        self.dirty = True
        self.last_plot = 0.0
        self.last_sample = None
        self.start_deadline = None
        self.running_mode = None
        self.connection = "COM 포트를 선택하세요."
        root.title("BSS138 · UART 측정 모니터")
        root.geometry("1120x690")
        root.minsize(900, 540)
        root.protocol("WM_DELETE_WINDOW", self.close)

        controls = ttk.Frame(root, padding=(12, 10))
        controls.pack(fill="x")
        ttk.Label(controls, text="COM 포트").pack(side="left")
        self.port_picker = ttk.Combobox(controls, width=14)
        self.port_picker.pack(side="left", padx=8)
        ttk.Button(controls, text="새로고침", command=self.refresh_ports).pack(side="left")
        self.connect_button = ttk.Button(controls, text="연결", command=self.toggle_port)
        self.connect_button.pack(side="left", padx=8)
        ttk.Label(controls, text="115200 baud · 8N1").pack(side="left")
        self.mode_picker = ttk.Combobox(controls, values=MODES, state="readonly", width=28)
        self.mode_picker.current(0)
        self.mode_picker.pack(side="left", padx=(14, 6))
        self.start_button = ttk.Button(controls, text="측정 시작", command=self.start_measurement)
        self.start_button.pack(side="left")
        self.start_button.state(["disabled"])
        self.save_button = ttk.Button(controls, text="CSV 저장", command=self.save_csv)
        self.save_button.pack(side="right")
        self.save_button.state(["disabled"])

        summary = ttk.Frame(root, padding=(12, 0, 12, 8))
        summary.pack(fill="x")
        self.summary = ttk.Label(summary)
        self.summary.pack(anchor="w")
        self.progress = ttk.Progressbar(summary, maximum=EXPECTED_POINTS)
        self.progress.pack(fill="x", pady=(5, 0))

        body = ttk.Frame(root, padding=(12, 0, 12, 10))
        body.pack(fill="both", expand=True)
        plot = ttk.Frame(body)
        plot.pack(side="left", fill="both", expand=True)
        figure = Figure(figsize=(7, 5), layout="constrained")
        self.axes = figure.add_subplot()
        self.canvas = FigureCanvasTkAgg(figure, master=plot)
        self.canvas.get_tk_widget().pack(fill="both", expand=True)
        toolbar = NavigationToolbar2Tk(self.canvas, plot, pack_toolbar=False)
        toolbar.pack(fill="x")

        side = ttk.Frame(body, padding=(10, 0, 0, 0))
        side.pack(side="right", fill="y")
        self.status = self.info_box(side, "수신 상태")
        self.measurement = self.info_box(side, "최신 측정값")
        self.checks = self.info_box(side, "조건 확인")
        self.refresh_ports()
        self.poll()

    @staticmethod
    def info_box(parent, title):
        box = ttk.LabelFrame(parent, text=title, padding=8)
        box.pack(fill="x", pady=(0, 8))
        label = ttk.Label(box, width=32, wraplength=245, justify="left")
        label.pack(anchor="w")
        return label

    def refresh_ports(self):
        ports = [port.device for port in list_ports.comports()]
        self.port_picker.configure(values=ports)
        if ports and not self.port_picker.get():
            self.port_picker.set(ports[0])

    def toggle_port(self):
        if self.port is not None:
            self.disconnect("연결을 끊었습니다.")
            return
        name = self.port_picker.get().strip()
        if not name:
            messagebox.showinfo("COM 포트", "보드의 COM 포트를 선택하세요.")
            return
        try:
            self.port = serial.Serial(name, BAUD_RATE, timeout=0, write_timeout=0.5)
        except (serial.SerialException, OSError) as error:
            messagebox.showerror("연결 실패", str(error))
            return
        self.capture = Capture()
        self.last_sample = None
        self.dirty = True
        self.connection = f"{name} 연결됨 · 모드를 선택하고 측정 시작을 누르세요."
        self.connect_button.configure(text="연결 끊기")
        self.port_picker.state(["disabled"])

    def start_measurement(self):
        if self.port is None or self.start_deadline is not None or self.running_mode is not None:
            return
        mode = self.mode_picker.current()
        if mode < 0:
            return
        try:
            self.port.write(f"START {mode}\n".encode("ascii"))
        except (serial.SerialException, OSError) as error:
            self.disconnect(f"명령 전송 실패 · {error}")
            return
        self.start_deadline = monotonic() + 3.0
        self.connection = f"모드 {mode} 시작 명령 전송 · 보드 응답 대기"
        self.start_button.state(["disabled"])

    def handle_event(self, event):
        if event.startswith("RUN "):
            self.running_mode = int(event[-1])
            self.connection = f"모드 {self.running_mode} 측정 중"
        else:
            self.running_mode = None
            if event.startswith("DONE "):
                self.connection = f"모드 {event[-1]} 측정 종료 · 다음 측정을 시작할 수 있습니다."
            elif event == "READY":
                self.connection = "보드 준비 완료 · 모드를 선택하고 측정 시작을 누르세요."
            else:
                self.connection = "보드가 명령을 거부했습니다."
        self.start_deadline = None

    def disconnect(self, reason):
        if self.port is not None:
            try:
                self.port.close()
            except (serial.SerialException, OSError):
                pass
            self.port = None
        self.connection = reason
        self.start_deadline = None
        self.running_mode = None
        self.connect_button.configure(text="연결")
        self.port_picker.state(["!disabled"])

    def poll(self):
        now = monotonic()
        if self.port is not None:
            try:
                waiting = self.port.in_waiting
                if waiting:
                    previous = self.capture.latest
                    self.dirty |= self.capture.feed(self.port.read(min(waiting, 16384)))
                    for event in self.capture.events:
                        self.handle_event(event)
                    if self.capture.latest is not previous:
                        self.last_sample = now if self.capture.latest is not None else None
            except (serial.SerialException, OSError) as error:
                self.disconnect(f"연결 종료 · {error}")
        if self.start_deadline is not None and now >= self.start_deadline:
            self.start_deadline = None
            self.connection = "보드 응답 없음 · 새 펌웨어, COM 포트와 UART RX를 확인하세요."
        if self.dirty and now - self.last_plot >= 0.35:
            self.draw_chart()
            self.last_plot = now
            self.dirty = False
        self.update_status(now)
        self.root.after(80, self.poll)

    def draw_chart(self):
        capture = self.capture
        self.axes.clear()
        axis = capture.mode or "VGS"
        self.axes.set_title(f"BSS138 | {axis}–ID | {len(capture.samples)} points")
        self.axes.set_xlabel(f"Measured {axis} (V)")
        self.axes.set_ylabel("Drain current (µA)")
        self.axes.grid(alpha=0.25)
        for sweep_id, points in capture.curves():
            if axis == "VDS":
                x = [sample.drain for sample in points]
                label = f"#{sweep_id}  VGS set {points[0].gate_set:.2f} V"
            else:
                x = [sample.gate for sample in points]
                supply = median(sample.supply for sample in points)
                label = f"#{sweep_id}  VSupply {supply:.2f} V"
            self.axes.plot(x, [sample.current for sample in points],
                           ".-", markersize=3, linewidth=1.3, label=label)
        if capture.samples:
            self.axes.legend(loc="best", fontsize="small")
        self.canvas.draw_idle()

    def update_status(self, now):
        capture = self.capture
        sample = capture.latest
        count = len(capture.samples)
        self.progress.configure(value=min(count, EXPECTED_POINTS))
        self.save_button.state(["!disabled"] if count else ["disabled"])
        busy = self.start_deadline is not None or self.running_mode is not None
        self.start_button.state(["disabled"] if self.port is None or busy else ["!disabled"])
        position = f" · 스윕 {sample.sweep_id} · 점 {sample.point_index + 1}" if sample else ""
        self.summary.configure(text=f"{self.connection}   |   {count}/{EXPECTED_POINTS}점{position}")
        age = f"{now - self.last_sample:.1f}초 전" if self.last_sample is not None else "—"
        self.status.configure(text=f"수신량  {capture.received_bytes:,} bytes\n"
                                   f"마지막 정상 데이터  {age}\n"
                                   f"미수신  {capture.missing}  ·  중복  {capture.duplicates}\n"
                                   f"형식 오류  {capture.errors}  ·  경고  {capture.warnings}")
        if sample is None:
            self.measurement.configure(text="CSV 헤더와 측정값을 기다리는 중")
            self.checks.configure(text="—")
            return
        supply_set = f"공급 설정  {sample.supply_set:.4f} V\n" if sample.supply_set is not None else ""
        self.measurement.configure(
            text=f"게이트 설정  {sample.gate_set:.4f} V\n"
                 f"게이트 실측  {sample.gate:.4f} V\n"
                 f"{supply_set}공급 실측  {sample.supply:.4f} V\n"
                 f"드레인 실측  {sample.drain:.4f} V\n"
                 f"드레인 전류  {sample.current:.4f} µA\n"
                 f"재계산 전류  {sample.calculated_current:.4f} µA\n"
                 f"보드 시간  {sample.time_ms} ms\n"
                 f"샘플 번호  {sample.sample_id}")
        gate_error = abs(sample.gate - sample.gate_set)
        current_error = abs(sample.current - sample.calculated_current)
        conditions = [
            f"게이트 오차  {gate_error:.4f} V  ({'확인' if gate_error > VOLTAGE_TOLERANCE else '정상'})",
            f"전류 검산 차이  {current_error:.4f} µA"
            f"  ({'확인' if current_error > CURRENT_CHECK_TOLERANCE else '정상'})",
            f"음의 전류 경고  {'있음' if sample.current < NEGATIVE_CURRENT_LIMIT else '없음'}",
        ]
        if sample.supply_error is not None:
            error = sample.supply_error
            conditions.insert(1, f"공급 오차  {error:.4f} V  ({'확인' if error > VOLTAGE_TOLERANCE else '정상'})")
        self.checks.configure(text="\n".join(conditions))

    def save_csv(self):
        capture = self.capture
        if not capture.rows:
            return
        # Snapshot before the file dialog runs its nested Tk event loop.
        lines = [capture.header, *(capture.rows[index] for index in sorted(capture.rows))]
        filename = filedialog.asksaveasfilename(defaultextension=".csv", filetypes=[("CSV", "*.csv")])
        if filename:
            try:
                Path(filename).write_text("\n".join(lines) + "\n", encoding="utf-8")
            except OSError as error:
                messagebox.showerror("저장 실패", str(error))

    def close(self):
        self.disconnect("")
        self.root.destroy()


if __name__ == "__main__":
    root = tk.Tk()
    Monitor(root)
    root.mainloop()
