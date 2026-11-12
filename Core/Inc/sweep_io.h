#ifndef SWEEP_IO_H
#define SWEEP_IO_H

#include "sweep_app.h"

typedef struct
{
    float supply_v;
    float drain_v;
    float gate_v;
} SweepSample;

void Sweep_StartHardware(void);
void Sweep_SetOutput(uint32_t channel, float volts);
SweepSample Sweep_ReadSample(void);
void Sweep_SendText(const char *text);
void Sweep_Fatal(uint32_t status);

#endif
