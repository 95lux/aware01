#include "envelope.h"

#include "project_config.h"

#define ATTACK_MIN_SEC 0.006f // 6 ms
#define ATTACK_MAX_SEC 10.0f  // 10 s
#define DECAY_MIN_SEC 0.010f  // 10 ms
#define DECAY_MAX_SEC 15.0f   // 15 s

float envelope_process(envelope_t* env) {
    switch (env->state) {
    case ENV_IDLE:
        env->value = 0.0f;
        break;

    case ENV_ATTACK: {
        float remaining = 1.0f - env->value;
        env->value += env->attack_inc * remaining;

        if (env->value >= 0.999f) {
            env->value = 1.0f;
            env->state = ENV_DECAY;
        }
        break;
    }
    case ENV_DECAY:
        env->value -= env->decay_inc;
        if (env->value <= env->sustain) {
            env->value = env->sustain;
            env->state = ENV_RELEASE;
        }
        break;

        // case ENV_SUSTAIN:
        //     // hold value
        //     break;

    case ENV_RELEASE: {
        env->value -= env->decay_inc * env->value;

        if (env->value <= 0.0001f) {
            env->value = 0.0f;
            env->state = ENV_IDLE;
        }
    } break;
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

float envelope_set_attack_norm(envelope_t* env, float u) {
    if (u < 0.0f)
        u = 0.0f;
    if (u > 1.0f)
        u = 1.0f;

    float attack_time = ATTACK_MIN_SEC * powf(ATTACK_MAX_SEC / ATTACK_MIN_SEC, u);
    env->attack_inc = 1.0f / (attack_time * AUDIO_SAMPLE_RATE);
    return attack_time;
}

float envelope_set_decay_norm(envelope_t* env, float u) {
    if (u < 0.0f)
        u = 0.0f;
    if (u > 1.0f)
        u = 1.0f;

    float decay_time = DECAY_MIN_SEC * powf(DECAY_MAX_SEC / DECAY_MIN_SEC, u);
    env->decay_inc = 1.0f / (decay_time * AUDIO_SAMPLE_RATE);
    return decay_time;
}

bool envelope_set_attack(envelope_t* env, float attack_time_sec) {
    if (attack_time_sec < ATTACK_MIN_SEC || attack_time_sec > ATTACK_MAX_SEC)
        return false;
    env->attack_inc = 1.0f / (attack_time_sec * AUDIO_SAMPLE_RATE);
    return true;
}

bool envelope_set_decay(envelope_t* env, float decay_time_sec) {
    if (decay_time_sec < DECAY_MIN_SEC || decay_time_sec > DECAY_MAX_SEC)
        return false;
    env->decay_inc = 1.0f / (decay_time_sec * AUDIO_SAMPLE_RATE);
    return true;
}