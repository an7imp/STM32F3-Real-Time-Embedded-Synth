#ifndef ADSR_H
#define ADSR_H

#include <stdint.h>

typedef enum {
    ADSR_IDLE = 0,
    ADSR_ATTACK,
    ADSR_DECAY,
    ADSR_SUSTAIN,
    ADSR_RELEASE
} ADSR_State;

typedef enum {
    ADSR_PARAM_ATTACK = 0,
    ADSR_PARAM_DECAY,
    ADSR_PARAM_SUSTAIN,
    ADSR_PARAM_RELEASE
} ADSR_Parameter;

extern volatile ADSR_State adsr_state;
extern volatile float adsr_level;
extern volatile float adsr_attack_seconds;
extern volatile float adsr_decay_seconds;
extern volatile float adsr_sustain_level;
extern volatile float adsr_release_seconds;

void ADSR_SetParameter(ADSR_Parameter parameter, uint8_t value);
void ADSR_NoteOn(void);
void ADSR_NoteOff(void);
float ADSR_ProcessSample(void);

#endif /* ADSR_H */
