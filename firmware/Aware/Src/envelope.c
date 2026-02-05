#include "envelope.h"


float envelope_process(envelope_t* env) {
    switch (env->state) {
    case ENV_IDLE:
        env->value = 0.0f;
        break;

    case ENV_ATTACK:
        env->value += env->attack_inc;
        if (env->value >= 1.0f) {
            env->value = 1.0f;
            env->state = ENV_DECAY;
        }
        break;

    case ENV_DECAY:
        env->value -= env->decay_inc;
        if (env->value <= env->sustain) {
            env->value = env->sustain;
            env->state = ENV_SUSTAIN;
        }
        break;

    case ENV_SUSTAIN:
        // hold value
        break;

    case ENV_RELEASE:
        env->value -= env->decay_inc;
        if (env->value <= 0.0f) {
            env->value = 0.0f;
            env->state = ENV_IDLE;
        }
        break;
    }
    return env->value;
}

void envelope_note_on(envelope_t* env) {
    env->state = ENV_ATTACK;
}

void envelope_note_off(envelope_t* env) {
    env->state = ENV_RELEASE;
}

bool envelope_is_open(envelope_t* env) {
    return env->state != ENV_IDLE;
}