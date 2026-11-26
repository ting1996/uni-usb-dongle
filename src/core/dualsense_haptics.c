#include "dongle/dualsense_haptics.h"
#include <string.h>

static uint16_t abs_s16_le(const uint8_t *p)
{
    const int16_t s = (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U));
    return s == INT16_MIN ? 32767U : (uint16_t)(s < 0 ? -s : s);
}

static uint8_t abs_diff_u8(uint8_t a, uint8_t b)
{
    return a >= b ? (uint8_t)(a - b) : (uint8_t)(b - a);
}

static uint16_t update_envelope(uint16_t env, uint16_t inst)
{
    if (inst > env) {
        uint16_t step = (uint16_t)((inst - env) >> 1U);
        if (step == 0U) {
            step = 1U;
        }
        return (uint16_t)(env + step);
    }
    const uint16_t decay = (uint16_t)((env - inst + 15U) >> 4U);
    return env >= decay ? (uint16_t)(env - decay) : 0U;
}

static uint8_t env_to_motor(uint16_t env, uint16_t gain_pct)
{
    if (gain_pct == 0U || env < DONGLE_DS_HAPTICS_NOISE_FLOOR) {
        return 0U;
    }
    const uint32_t gained = ((uint32_t)env * (uint32_t)gain_pct) / 100U;
    if (gained < DONGLE_DS_HAPTICS_NOISE_FLOOR) {
        return 0U;
    }
    if (gained >= DONGLE_DS_HAPTICS_FULL_SCALE) {
        return 255U;
    }
    const uint32_t scaled =
        ((gained - DONGLE_DS_HAPTICS_NOISE_FLOOR) *
         (255U - DONGLE_DS_HAPTICS_MIN_MOTOR)) /
            (DONGLE_DS_HAPTICS_FULL_SCALE - DONGLE_DS_HAPTICS_NOISE_FLOOR) +
        DONGLE_DS_HAPTICS_MIN_MOTOR;
    return scaled > 255U ? 255U : (uint8_t)scaled;
}

void dongle_ds_haptics_init(dongle_ds_haptics_t *state)
{
    if (!state) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->gain_pct = DONGLE_DS_HAPTICS_GAIN_DEFAULT;
}

void dongle_ds_haptics_set_gain(dongle_ds_haptics_t *state, uint16_t gain_pct)
{
    if (!state) {
        return;
    }
    state->gain_pct = gain_pct > DONGLE_DS_HAPTICS_GAIN_MAX
        ? DONGLE_DS_HAPTICS_GAIN_MAX
        : gain_pct;
}

uint16_t dongle_ds_haptics_get_gain(const dongle_ds_haptics_t *state)
{
    return state ? state->gain_pct : DONGLE_DS_HAPTICS_GAIN_DEFAULT;
}

bool dongle_ds_haptics_reset(dongle_ds_haptics_t *state)
{
    if (!state) {
        return false;
    }
    const bool was_active = state->active;
    const uint16_t gain = state->gain_pct;
    memset(state, 0, sizeof(*state));
    state->gain_pct = gain;
    return was_active;
}

bool dongle_ds_haptics_process_4ch(
    dongle_ds_haptics_t *state,
    const uint8_t *pcm4,
    size_t size,
    uint8_t *stereo_out,
    size_t *stereo_len,
    uint16_t *out_peak_l,
    uint16_t *out_peak_r,
    uint8_t *out_motor_l,
    uint8_t *out_motor_r,
    bool *out_changed)
{
    if (!state || !pcm4 || size == 0U || (size % 8U) != 0U) {
        return false;
    }

    const size_t frames = size / 8U;
    uint16_t peak_fl = 0U;
    uint16_t peak_fr = 0U;
    uint16_t peak_hl = 0U;
    uint16_t peak_hr = 0U;
    uint32_t sum_hl = 0U;
    uint32_t sum_hr = 0U;

    for (size_t i = 0U; i < frames; ++i) {
        const uint8_t *f = &pcm4[i * 8U];
        if (stereo_out != NULL) {
            memcpy(&stereo_out[i * 4U], f, 4U);
        }
        const uint16_t fl = abs_s16_le(&f[0]);
        const uint16_t fr = abs_s16_le(&f[2]);
        const uint16_t hl = abs_s16_le(&f[4]);
        const uint16_t hr = abs_s16_le(&f[6]);
        if (fl > peak_fl) peak_fl = fl;
        if (fr > peak_fr) peak_fr = fr;
        if (hl > peak_hl) peak_hl = hl;
        if (hr > peak_hr) peak_hr = hr;
        sum_hl += hl;
        sum_hr += hr;
    }

    const uint16_t inst_l = (uint16_t)((peak_hl + (uint16_t)(sum_hl / frames)) / 2U);
    const uint16_t inst_r = (uint16_t)((peak_hr + (uint16_t)(sum_hr / frames)) / 2U);

    state->env_l = update_envelope(state->env_l, inst_l);
    state->env_r = update_envelope(state->env_r, inst_r);

    uint8_t ml = env_to_motor(state->env_l, state->gain_pct);
    uint8_t mr = env_to_motor(state->env_r, state->gain_pct);

    if (state->emit_age_ms < 255U) {
        ++state->emit_age_ms;
    }

    bool changed = false;
    if (ml > 0U || mr > 0U) {
        const bool was_active = state->active;
        state->active = true;
        state->silent_ms = 0U;
        changed = !was_active ||
                  abs_diff_u8(ml, state->last_l) >= DONGLE_DS_HAPTICS_CHANGE_STEP ||
                  abs_diff_u8(mr, state->last_r) >= DONGLE_DS_HAPTICS_CHANGE_STEP ||
                  state->emit_age_ms >= DONGLE_DS_HAPTICS_REFRESH_MS;
        if (changed) {
            state->last_l = ml;
            state->last_r = mr;
            state->emit_age_ms = 0U;
        }
    } else if (state->active) {
        if (state->silent_ms < 255U) {
            ++state->silent_ms;
        }
        if (state->silent_ms >= DONGLE_DS_HAPTICS_SILENCE_MS) {
            state->active = false;
            state->last_l = 0U;
            state->last_r = 0U;
            state->emit_age_ms = 0U;
            changed = true;
        } else {
            changed = false;
            ml = state->last_l;
            mr = state->last_r;
        }
    }

    if (stereo_len != NULL) {
        *stereo_len = size / 2U;
    }
    if (out_peak_l != NULL) {
        *out_peak_l = peak_fl > peak_hl ? peak_fl : peak_hl;
    }
    if (out_peak_r != NULL) {
        *out_peak_r = peak_fr > peak_hr ? peak_fr : peak_hr;
    }
    if (out_motor_l != NULL) {
        *out_motor_l = ml;
    }
    if (out_motor_r != NULL) {
        *out_motor_r = mr;
    }
    if (out_changed != NULL) {
        *out_changed = changed;
    }
    return true;
}
