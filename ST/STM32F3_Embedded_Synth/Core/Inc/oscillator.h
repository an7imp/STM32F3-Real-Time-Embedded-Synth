#ifndef OSCILLATOR_H
#define OSCILLATOR_H

#include <stdint.h>

extern volatile uint8_t wave;
extern volatile uint8_t oscillator_mipmapped_enabled;
extern volatile uint8_t wavetable_interpolation_enabled;

extern volatile uint32_t phase_accumulator;
extern uint32_t phase_increment;
extern uint8_t active_mip_level;
extern float current_frequency;

void GenerateSampleTable(void);
void SetFrequency(float frequency_hz);
float Oscillator_ProcessSample(void);

#endif /* OSCILLATOR_H */
