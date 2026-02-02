#pragma once

typedef enum { ENV_IDLE = 0, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE } env_state_t;

typedef struct {
    env_state_t state;
    float value;      // current envelope value (0..1)
    float attack_inc; // increment per sample during attack
    float decay_inc;  // decrement per sample during decay/release
    float sustain;    // sustain level (0..1)
} envelope_t;

float envelope_process(envelope_t* env);
void envelope_note_on(envelope_t* env);
void envelope_note_off(envelope_t* env);