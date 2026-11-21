#ifndef DONGLE_SETTINGS_CODEC_H
#define DONGLE_SETTINGS_CODEC_H

#include "dongle/device_catalog.h"

#include <stddef.h>
#include <stdint.h>

#define DONGLE_SETTINGS_BLOB_SIZE 24U

typedef enum {
    DONGLE_SETTINGS_CODEC_OK = 0,
    DONGLE_SETTINGS_CODEC_INVALID_ARGUMENT,
    DONGLE_SETTINGS_CODEC_INVALID_DATA
} dongle_settings_codec_result_t;

dongle_settings_codec_result_t dongle_settings_encode(
    const dongle_device_catalog_t *catalog,
    uint8_t *buffer,
    size_t buffer_size);

dongle_settings_codec_result_t dongle_settings_decode(
    const uint8_t *buffer,
    size_t buffer_size,
    dongle_device_catalog_t *catalog);

#endif
