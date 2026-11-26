#include "dongle/prefs_codec.h"
#include "dongle/a2dp_codec.h"
#include "codec_crc32.h"

#include <stdbool.h>
#include <string.h>

#define PREFS_CRC_OFFSET (DONGLE_PREFS_BLOB_SIZE - 4U)

static const uint8_t prefs_magic[4] = { 'U', 'P', 'R', 'F' };

static bool prefs_is_valid(const dongle_prefs_t *prefs)
{
    return prefs != NULL &&
           prefs->boot_profile < DONGLE_PROFILE_COUNT &&
           prefs->audio_codec < DONGLE_AUDIO_CODEC_COUNT &&
           prefs->mic_source < DONGLE_MIC_SOURCE_COUNT &&
           prefs->dualsense_poll_rate < DONGLE_DS_POLL_RATE_COUNT &&
           prefs->rumble_gain_pct <= DONGLE_RUMBLE_GAIN_MAX;
}

dongle_prefs_result_t dongle_prefs_init_defaults(dongle_prefs_t *prefs)
{
    if (prefs == NULL) {
        return DONGLE_PREFS_INVALID_ARGUMENT;
    }
    prefs->boot_profile = (uint8_t)DONGLE_PROFILE_GENERIC;
    prefs->audio_codec = 0U;
    prefs->mic_source = (uint8_t)DONGLE_MIC_SOURCE_AUTO;
    prefs->dualsense_poll_rate = (uint8_t)DONGLE_DS_POLL_250HZ;
    prefs->rumble_gain_pct = 100U;
    return DONGLE_PREFS_OK;
}

dongle_prefs_result_t dongle_prefs_encode(
    const dongle_prefs_t *prefs,
    uint8_t *buffer,
    size_t buffer_size)
{
    if (prefs == NULL || buffer == NULL || buffer_size < DONGLE_PREFS_BLOB_SIZE) {
        return DONGLE_PREFS_INVALID_ARGUMENT;
    }
    if (!prefs_is_valid(prefs)) {
        return DONGLE_PREFS_INVALID_DATA;
    }

    memset(buffer, 0, DONGLE_PREFS_BLOB_SIZE);
    memcpy(buffer, prefs_magic, sizeof(prefs_magic));
    buffer[4] = 1U;
    buffer[5] = prefs->boot_profile;
    buffer[6] = prefs->audio_codec;
    buffer[7] = prefs->mic_source;
    buffer[8] = prefs->dualsense_poll_rate;
    const uint16_t stored_gain = (uint16_t)(prefs->rumble_gain_pct + 1U);
    buffer[9] = (uint8_t)(stored_gain & 0xffU);
    buffer[10] = (uint8_t)(stored_gain >> 8U);
    dongle_codec_write_le32(&buffer[PREFS_CRC_OFFSET],
                            dongle_codec_crc32(buffer, PREFS_CRC_OFFSET));
    return DONGLE_PREFS_OK;
}

dongle_prefs_result_t dongle_prefs_decode(
    const uint8_t *buffer,
    size_t buffer_size,
    dongle_prefs_t *prefs)
{
    if (buffer == NULL || prefs == NULL || buffer_size != DONGLE_PREFS_BLOB_SIZE) {
        return DONGLE_PREFS_INVALID_ARGUMENT;
    }
    if (memcmp(buffer, prefs_magic, sizeof(prefs_magic)) != 0 ||
        buffer[4] != 1U ||
        dongle_codec_read_le32(&buffer[PREFS_CRC_OFFSET]) !=
            dongle_codec_crc32(buffer, PREFS_CRC_OFFSET)) {
        return DONGLE_PREFS_INVALID_DATA;
    }

    const uint16_t stored_gain =
        (uint16_t)buffer[9] | ((uint16_t)buffer[10] << 8U);
    const dongle_prefs_t decoded = {
        .boot_profile = buffer[5],
        .audio_codec = buffer[6],
        .mic_source = buffer[7],
        .dualsense_poll_rate = buffer[8],
        .rumble_gain_pct = stored_gain == 0U ? 100U : (uint16_t)(stored_gain - 1U),
    };
    if (!prefs_is_valid(&decoded)) {
        return DONGLE_PREFS_INVALID_DATA;
    }
    *prefs = decoded;
    return DONGLE_PREFS_OK;
}
