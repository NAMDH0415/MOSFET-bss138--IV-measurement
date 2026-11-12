#ifndef SWEEP_APP_H
#define SWEEP_APP_H

#include "main.h"
#include <stdint.h>

/* ===== Configure measurements here; no CubeMX changes needed ===== */
#define MEASUREMENT_MODE_VGS_ID 0  /* Sweep VGS at a fixed drain supply. */
#define MEASUREMENT_MODE_VDS_ID 1  /* Sweep the drain supply at each fixed VGS and measure VDS. */

/* Select the measurement mode, then rebuild and flash the firmware. */
#ifndef MEASUREMENT_MODE
#define MEASUREMENT_MODE MEASUREMENT_MODE_VGS_ID
#endif

#if MEASUREMENT_MODE != MEASUREMENT_MODE_VGS_ID && MEASUREMENT_MODE != MEASUREMENT_MODE_VDS_ID
#error "Select MEASUREMENT_MODE_VGS_ID or MEASUREMENT_MODE_VDS_ID."
#endif

#define VGS_START_V             0.0f
#define VGS_END_V               3.0f
#define SWEEP_POINTS            512U    /* 0.0, 0.1, ... 3.0 V */
#define SWEEP_SETS              1U     /* Number of full sweeps */
#define VSUPPLY_DAC_SET_V       2.8f
#define ADC_VREF_ASSUMED_V      3.3f
#define DRAIN_RESISTOR_OHM      10000.0f
#define ADC_AVERAGE_COUNT       16U
#define DAC_SETTLE_MS           100U
#define POINT_PAUSE_MS          50U
#define SWEEP_PAUSE_MS          1000U

#if SWEEP_POINTS < 2U || SWEEP_POINTS > 512U
#error "SWEEP_POINTS must be in [2, 512]."
#endif
#if SWEEP_SETS < 1U
#error "SWEEP_SETS must be at least one."
#endif

/* Debug variables for CubeIDE Live Expressions */
extern volatile float vgs_set_live;
extern volatile float vgs_measured_live;
extern volatile float vsupply_measured_live;
extern volatile float vdrain_measured_live;
extern volatile float resistor_drop_live;
extern volatile float id_uA_live;
extern volatile uint32_t sample_id_live;
extern volatile uint32_t sweep_id_live;
extern volatile uint32_t point_index_live;
extern volatile uint32_t time_ms_live;
extern volatile uint32_t sweeps_completed_live;
extern volatile uint32_t measurement_done_live;
/* 0 OK; 1 VSupply < VDrain by >20 mV; 2 ADC error;
   3 UART error; 4 formatting error; 5 DAC error */
extern volatile uint32_t sweep_status_live;

/* Measurements from most recent sweep; not the average across sweeps. */
extern volatile float vgs_set_points[SWEEP_POINTS];
extern volatile float vgs_measured[SWEEP_POINTS];
extern volatile float vsupply_measured[SWEEP_POINTS];
extern volatile float vdrain_measured[SWEEP_POINTS];
extern volatile float id_uA[SWEEP_POINTS];

void Sweep_Init(void);
void Sweep_RunAllSets(void);

#endif
