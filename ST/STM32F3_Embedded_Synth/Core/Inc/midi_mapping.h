#ifndef MIDI_MAPPING_H
#define MIDI_MAPPING_H

#include <stdint.h>

typedef enum {
    SYNTH_PARAM_FILTER_CUTOFF = 0,
    SYNTH_PARAM_FILTER_RESONANCE,
    SYNTH_PARAM_ATTACK,
    SYNTH_PARAM_DECAY,
    SYNTH_PARAM_SUSTAIN,
    SYNTH_PARAM_RELEASE
} SynthParameter;

typedef struct {
    uint8_t cc_number;
    SynthParameter parameter;
} MidiCCMapping;

void MIDI_ApplyControlChange(uint8_t cc_number, uint8_t value);

#endif /* MIDI_MAPPING_H */
