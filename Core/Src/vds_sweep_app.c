#include "vds_sweep_app.h"
#include "sweep_io.h"
#include <stdio.h>

#define VDS_SUPPLY_START_V 0.0f
#define VDS_SUPPLY_END_V   2.8f
#define VDS_GATE_SET_V     1.0f  /* Fixed throughout the drain-supply sweep. */
#define VDS_POINTS         512U

void VdsSweep_Run(void)
{
    Sweep_StartHardware(0.0f);
    Sweep_SendText("VGS_Set,VGS_Measured,VSupply_Set,VSupply,VDS_Measured,ID_uA,"
                   "time_ms,sample_id,sweep_id,point_index\r\n");

    const unsigned long sweep_id = 1UL;
    Sweep_SetOutput(DAC_CHANNEL_1, VDS_GATE_SET_V);
    for (uint32_t point = 0U; point < VDS_POINTS; ++point)
    {
        const float supply_set = VDS_SUPPLY_START_V
            + (VDS_SUPPLY_END_V - VDS_SUPPLY_START_V) * (float)point / (float)(VDS_POINTS - 1U);
        Sweep_SetOutput(DAC_CHANNEL_2, supply_set);
        HAL_Delay(DAC_SETTLE_MS);

        const SweepSample sample = Sweep_ReadSample();
        const float drop_v = sample.supply_v - sample.drain_v;
        const float current_uA = drop_v * (1000000.0f / DRAIN_RESISTOR_OHM);
        const uint32_t measured_at_ms = HAL_GetTick();

        vgs_set_live = VDS_GATE_SET_V;
        vgs_measured_live = sample.gate_v;
        vsupply_measured_live = sample.supply_v;
        vdrain_measured_live = sample.drain_v;
        resistor_drop_live = drop_v;
        id_uA_live = current_uA;
        time_ms_live = measured_at_ms;
        sample_id_live = point;
        sweep_id_live = sweep_id;
        point_index_live = point;
        sweep_status_live = (drop_v < -0.020f) ? 1U : 0U;

        char csv_line[192];
        int length = snprintf(csv_line, sizeof(csv_line),
                              "%.6f,%.5f,%.6f,%.5f,%.5f,%.4f,%lu,%lu,%lu,%lu\r\n",
                              (double)VDS_GATE_SET_V, (double)sample.gate_v,
                              (double)supply_set, (double)sample.supply_v,
                              (double)sample.drain_v, (double)current_uA,
                              (unsigned long)measured_at_ms, (unsigned long)point,
                              sweep_id, (unsigned long)point);
        if (length < 0 || (size_t)length >= sizeof(csv_line)) Sweep_Fatal(4U);
        Sweep_SendText(csv_line);
        HAL_Delay(POINT_PAUSE_MS);
    }

    Sweep_SetOutput(DAC_CHANNEL_2, 0.0f);
    Sweep_SetOutput(DAC_CHANNEL_1, 0.0f);
    sweeps_completed_live = sweep_id;
    measurement_done_live = 1U;
}
