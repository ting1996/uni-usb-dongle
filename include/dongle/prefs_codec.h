#ifndef DONGLE_PREFS_CODEC_H
#define DONGLE_PREFS_CODEC_H

#include "dongle/output_profile.h"
#include "dongle/usb_ota_protocol.h"

#include <stddef.h>
#include <stdint.h>

#define DONGLE_PREFS_BLOB_SIZE 16U

typedef enum {
    DONGLE_PREFS_OK = 0,
    DONGLE_PREFS_INVALID_ARGUMENT,
    DONGLE_PREFS_INVALID_DATA
} dongle_prefs_result_t;

typedef struct {
    uint8_t boot_profile;
    uint8_t audio_codec;
    uint8_t mic_source;
    uint8_t dualsense_poll_rate;
    uint16_t rumble_gain_pct;
} dongle_prefs_t;

dongle_prefs_result_t dongle_prefs_init_defaults(dongle_prefs_t *prefs);

dongle_prefs_result_t dongle_prefs_encode(
    const dongle_prefs_t *prefs,
    uint8_t *buffer,
    size_t buffer_size);

dongle_prefs_result_t dongle_prefs_decode(
    const uint8_t *buffer,
    size_t buffer_size,
    dongle_prefs_t *prefs);

#endif
