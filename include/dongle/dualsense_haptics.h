#ifndef DONGLE_DUALSENSE_HAPTICS_H
#define DONGLE_DUALSENSE_HAPTICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DONGLE_DS_HAPTICS_NOISE_FLOOR 128U
#define DONGLE_DS_HAPTICS_FULL_SCALE 8192U
#define DONGLE_DS_HAPTICS_MIN_MOTOR 16U
#define DONGLE_DS_HAPTICS_SILENCE_MS 16U
#define DONGLE_DS_HAPTICS_CHANGE_STEP 6U
#define DONGLE_DS_HAPTICS_REFRESH_MS 16U
#define DONGLE_DS_HAPTICS_GAIN_DEFAULT 100U
#define DONGLE_DS_HAPTICS_GAIN_MAX 400U

typedef struct {
    uint16_t env_l;
    uint16_t env_r;
    uint16_t gain_pct;
    uint8_t last_l;
    uint8_t last_r;
    uint8_t silent_ms;
    uint8_t emit_age_ms;
    bool active;
} dongle_ds_haptics_t;

/* Initializes a DualSense 4-channel audio haptics demux/envelope state. */
void dongle_ds_haptics_init(dongle_ds_haptics_t *state);

/* Sets/gets DualSense audio haptics gain percentage (0..400, default 100). */
void dongle_ds_haptics_set_gain(dongle_ds_haptics_t *state, uint16_t gain_pct);
uint16_t dongle_ds_haptics_get_gain(const dongle_ds_haptics_t *state);

/* Resets the haptics state; returns true if haptics were active so the caller
 * can emit a (0, 0) stop report. */
bool dongle_ds_haptics_reset(dongle_ds_haptics_t *state);

/* Demuxes 4-channel 16-bit LE PCM [FL, FR, HL, HR] into 2-channel [FL, FR]
 * stereo_out (optional) and extracts DualSense motor_l/motor_r amplitudes
 * from channels 3 & 4 via an integer envelope follower. */
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
    bool *out_changed);

#endif
