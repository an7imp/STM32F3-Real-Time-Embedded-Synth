#include "midi_mapping.h"

#include "adsr.h"
#include "svf_filter.h"

/*
 * Edit this table to adapt the controls to a different MIDI keyboard.
 * Each MIDI CC number is associated with one synthesizer parameter.
 */
static const MidiCCMapping midi_cc_map[] = {
    {21U, SYNTH_PARAM_FILTER_CUTOFF},
    {22U, SYNTH_PARAM_FILTER_RESONANCE},
    {71U, SYNTH_PARAM_ATTACK},
    {72U, SYNTH_PARAM_DECAY},
    {73U, SYNTH_PARAM_SUSTAIN},
    {74U, SYNTH_PARAM_RELEASE}
};

#define MIDI_CC_MAP_COUNT (sizeof(midi_cc_map) / sizeof(midi_cc_map[0]))

static void MIDI_ApplyMappedParameter(SynthParameter parameter, uint8_t value)
{
    switch (parameter) {
    case SYNTH_PARAM_FILTER_CUTOFF:
        Filter_SetParameter(FILTER_PARAM_CUTOFF, value);
        break;

    case SYNTH_PARAM_FILTER_RESONANCE:
        Filter_SetParameter(FILTER_PARAM_RESONANCE, value);
        break;

    case SYNTH_PARAM_ATTACK:
        ADSR_SetParameter(ADSR_PARAM_ATTACK, value);
        break;

    case SYNTH_PARAM_DECAY:
        ADSR_SetParameter(ADSR_PARAM_DECAY, value);
        break;

    case SYNTH_PARAM_SUSTAIN:
        ADSR_SetParameter(ADSR_PARAM_SUSTAIN, value);
        break;

    case SYNTH_PARAM_RELEASE:
        ADSR_SetParameter(ADSR_PARAM_RELEASE, value);
        break;

    default:
        break;
    }
}

void MIDI_ApplyControlChange(uint8_t cc_number, uint8_t value)
{
    for (uint32_t index = 0U; index < MIDI_CC_MAP_COUNT; ++index) {
        if (midi_cc_map[index].cc_number == cc_number) {
            MIDI_ApplyMappedParameter(midi_cc_map[index].parameter, value);
            return;
        }
    }
}
