#ifndef SVF_FILTER_H
#define SVF_FILTER_H

#include <stdint.h>

#define FILTER_CUTOFF_MIN_HZ 40.0f
#define FILTER_CUTOFF_MAX_HZ 16000.0f
#define FILTER_Q_MIN 0.5f
#define FILTER_Q_MAX 8.0f

typedef struct {
    float g;
    float k;
    float a1;
    float a2;
    float a3;
    float ic1eq;
    float ic2eq;
} SVF_Filter;

typedef enum {
    FILTER_PARAM_CUTOFF = 0,
    FILTER_PARAM_RESONANCE
} Filter_Parameter;

extern SVF_Filter lowpass_filter;
extern volatile uint8_t filter_enabled;
extern volatile uint8_t filter_gain_compensation_enabled;
extern float filter_cutoff_hz;
extern float filter_q;
extern volatile float filter_cutoff_target_hz;
extern volatile float filter_q_target;

void SVF_Reset(SVF_Filter *filter);
void SVF_SetParameters(SVF_Filter *filter,
                       float cutoff_hz,
                       float q,
                       float sample_rate);
float SVF_ProcessLowpass(SVF_Filter *filter, float input);
void Filter_SetParameter(Filter_Parameter parameter, uint8_t value);

#endif /* SVF_FILTER_H */
