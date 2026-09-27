#include "svf_filter.h"

#include <math.h>

SVF_Filter lowpass_filter;
volatile uint8_t filter_enabled = 1U;
volatile uint8_t filter_gain_compensation_enabled = 1U;
float filter_cutoff_hz = 2000.0f;
float filter_q = 0.707f;
volatile float filter_cutoff_target_hz = 2000.0f;
volatile float filter_q_target = 0.707f;

static float MapMIDIFrequency(uint8_t value, float minimum, float maximum)
{
    float normalized = (float)value / 127.0f;
    return minimum * powf(maximum / minimum, normalized);
}

void SVF_Reset(SVF_Filter *filter)
{
    filter->ic1eq = 0.0f;
    filter->ic2eq = 0.0f;
}

void SVF_SetParameters(SVF_Filter *filter,
                       float cutoff_hz,
                       float q,
                       float sample_rate)
{
    const float pi = 3.14159265358979323846f;

    if (cutoff_hz < FILTER_CUTOFF_MIN_HZ) {
        cutoff_hz = FILTER_CUTOFF_MIN_HZ;
    } else if (cutoff_hz > FILTER_CUTOFF_MAX_HZ) {
        cutoff_hz = FILTER_CUTOFF_MAX_HZ;
    }

    if (q < FILTER_Q_MIN) {
        q = FILTER_Q_MIN;
    } else if (q > FILTER_Q_MAX) {
        q = FILTER_Q_MAX;
    }

    filter->g = tanf(pi * cutoff_hz / sample_rate);
    filter->k = 1.0f / q;
    filter->a1 = 1.0f / (1.0f + filter->g * (filter->g + filter->k));
    filter->a2 = filter->g * filter->a1;
    filter->a3 = filter->g * filter->a2;
}

float SVF_ProcessLowpass(SVF_Filter *filter, float input)
{
    float v3 = input - filter->ic2eq;
    float v1 = filter->a1 * filter->ic1eq + filter->a2 * v3;
    float v2 = filter->ic2eq
             + filter->a2 * filter->ic1eq
             + filter->a3 * v3;

    filter->ic1eq = 2.0f * v1 - filter->ic1eq;
    filter->ic2eq = 2.0f * v2 - filter->ic2eq;
    return v2;
}

void Filter_SetParameter(Filter_Parameter parameter, uint8_t value)
{
    float normalized = (float)value / 127.0f;

    switch (parameter) {
    case FILTER_PARAM_CUTOFF:
        filter_cutoff_target_hz =
            MapMIDIFrequency(value,
                             FILTER_CUTOFF_MIN_HZ,
                             FILTER_CUTOFF_MAX_HZ);
        break;

    case FILTER_PARAM_RESONANCE:
        filter_q_target =
            FILTER_Q_MIN * powf(FILTER_Q_MAX / FILTER_Q_MIN, normalized);
        break;

    default:
        break;
    }
}
