#include "adsr.h"

#include "audio_config.h"

extern volatile uint8_t sound_enabled;

volatile ADSR_State adsr_state = ADSR_IDLE;
volatile float adsr_level = 0.0f;
volatile float adsr_attack_seconds = 0.010f;
volatile float adsr_decay_seconds = 0.150f;
volatile float adsr_sustain_level = 0.80f;
volatile float adsr_release_seconds = 0.300f;

static volatile float adsr_attack_step = 0.0f;
static volatile float adsr_decay_step = 0.0f;
static volatile float adsr_release_step = 0.0f;

static float MapMIDITime(uint8_t value, float minimum, float maximum)
{
    float normalized = (float)value / 127.0f;
    return minimum + (maximum - minimum) * normalized * normalized;
}

void ADSR_SetParameter(ADSR_Parameter parameter, uint8_t value)
{
    switch (parameter) {
    case ADSR_PARAM_ATTACK:
        adsr_attack_seconds = MapMIDITime(value, 0.001f, 5.0f);
        if (adsr_state == ADSR_ATTACK) {
            adsr_attack_step = (1.0f - adsr_level) /
                               (adsr_attack_seconds * (float)SAMPLE_RATE);
        }
        break;

    case ADSR_PARAM_DECAY:
        adsr_decay_seconds = MapMIDITime(value, 0.001f, 5.0f);
        if (adsr_state == ADSR_DECAY) {
            adsr_decay_step = (adsr_level - adsr_sustain_level) /
                              (adsr_decay_seconds * (float)SAMPLE_RATE);
        }
        break;

    case ADSR_PARAM_SUSTAIN:
        adsr_sustain_level = (float)value / 127.0f;
        if (adsr_state == ADSR_DECAY) {
            if (adsr_level <= adsr_sustain_level) {
                adsr_level = adsr_sustain_level;
                adsr_state = ADSR_SUSTAIN;
            } else {
                adsr_decay_step = (adsr_level - adsr_sustain_level) /
                                  (adsr_decay_seconds * (float)SAMPLE_RATE);
            }
        }
        break;

    case ADSR_PARAM_RELEASE:
        adsr_release_seconds = MapMIDITime(value, 0.005f, 8.0f);
        if (adsr_state == ADSR_RELEASE) {
            adsr_release_step = adsr_level /
                                (adsr_release_seconds * (float)SAMPLE_RATE);
        }
        break;

    default:
        break;
    }
}

void ADSR_NoteOn(void)
{
    float remaining_level = 1.0f - adsr_level;
    adsr_attack_step =
        remaining_level / (adsr_attack_seconds * (float)SAMPLE_RATE);
    adsr_state = ADSR_ATTACK;
}

void ADSR_NoteOff(void)
{
    adsr_release_step =
        adsr_level / (adsr_release_seconds * (float)SAMPLE_RATE);
    adsr_state = ADSR_RELEASE;
}

float ADSR_ProcessSample(void)
{
    switch (adsr_state) {
    case ADSR_ATTACK:
        adsr_level += adsr_attack_step;
        if (adsr_level >= 1.0f) {
            adsr_level = 1.0f;
            adsr_decay_step = (1.0f - adsr_sustain_level) /
                              (adsr_decay_seconds * (float)SAMPLE_RATE);
            adsr_state = ADSR_DECAY;
        }
        break;

    case ADSR_DECAY:
        adsr_level -= adsr_decay_step;
        if (adsr_level <= adsr_sustain_level) {
            adsr_level = adsr_sustain_level;
            adsr_state = ADSR_SUSTAIN;
        }
        break;

    case ADSR_SUSTAIN:
        adsr_level = adsr_sustain_level;
        break;

    case ADSR_RELEASE:
        adsr_level -= adsr_release_step;
        if ((adsr_level <= 0.0f) || (adsr_release_step <= 0.0f)) {
            adsr_level = 0.0f;
            adsr_state = ADSR_IDLE;
            sound_enabled = 0U;
        }
        break;

    case ADSR_IDLE:
    default:
        adsr_level = 0.0f;
        break;
    }

    return adsr_level;
}
