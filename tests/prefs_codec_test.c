#include "dongle/prefs_codec.h"
#include "dongle/a2dp_codec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            fprintf(stderr, "Check failed at line %d: %s\n", __LINE__, #expression); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

int main(void)
{
    dongle_prefs_t prefs;
    CHECK(dongle_prefs_init_defaults(NULL) == DONGLE_PREFS_INVALID_ARGUMENT);
    CHECK(dongle_prefs_init_defaults(&prefs) == DONGLE_PREFS_OK);
    CHECK(prefs.boot_profile == DONGLE_PROFILE_GENERIC);
    CHECK(prefs.audio_codec == 0U);
    CHECK(prefs.mic_source == DONGLE_MIC_SOURCE_AUTO);
    CHECK(prefs.dualsense_poll_rate == DONGLE_DS_POLL_250HZ);
    CHECK(prefs.rumble_gain_pct == 100U);

    prefs.boot_profile = DONGLE_PROFILE_DUALSENSE;
    prefs.audio_codec = 1U;
    prefs.mic_source = DONGLE_MIC_SOURCE_HEADPHONE;
    prefs.dualsense_poll_rate = DONGLE_DS_POLL_750HZ;
    prefs.rumble_gain_pct = 250U;

    uint8_t blob[DONGLE_PREFS_BLOB_SIZE];
    CHECK(dongle_prefs_encode(&prefs, blob, sizeof(blob) - 1U) ==
          DONGLE_PREFS_INVALID_ARGUMENT);
    CHECK(dongle_prefs_encode(&prefs, blob, sizeof(blob)) == DONGLE_PREFS_OK);

    dongle_prefs_t decoded;
    CHECK(dongle_prefs_init_defaults(&decoded) == DONGLE_PREFS_OK);
    CHECK(dongle_prefs_decode(blob, sizeof(blob), &decoded) == DONGLE_PREFS_OK);
    CHECK(decoded.boot_profile == DONGLE_PROFILE_DUALSENSE);
    CHECK(decoded.audio_codec == 1U);
    CHECK(decoded.mic_source == DONGLE_MIC_SOURCE_HEADPHONE);
    CHECK(decoded.dualsense_poll_rate == DONGLE_DS_POLL_750HZ);
    CHECK(decoded.rumble_gain_pct == 250U);

    /* Corrupt CRC / payload -> rejected without changing decoded. */
    blob[5] ^= 0x01U;
    CHECK(dongle_prefs_decode(blob, sizeof(blob), &decoded) ==
          DONGLE_PREFS_INVALID_DATA);
    CHECK(decoded.boot_profile == DONGLE_PROFILE_DUALSENSE);

    /* Valid audio codecs 0..5 -> accepted by encode/decode. */
    for (uint8_t c = 0U; c < DONGLE_AUDIO_CODEC_COUNT; ++c) {
        dongle_prefs_t p = prefs;
        p.audio_codec = c;
        CHECK(dongle_prefs_encode(&p, blob, sizeof(blob)) == DONGLE_PREFS_OK);
        CHECK(dongle_prefs_decode(blob, sizeof(blob), &decoded) == DONGLE_PREFS_OK);
        CHECK(decoded.audio_codec == c);
    }

    /* Valid DualSense polling rates 0..2 -> accepted by encode/decode. */
    for (uint8_t r = 0U; r < DONGLE_DS_POLL_RATE_COUNT; ++r) {
        dongle_prefs_t p = prefs;
        p.dualsense_poll_rate = r;
        CHECK(dongle_prefs_encode(&p, blob, sizeof(blob)) == DONGLE_PREFS_OK);
        CHECK(dongle_prefs_decode(blob, sizeof(blob), &decoded) == DONGLE_PREFS_OK);
        CHECK(decoded.dualsense_poll_rate == r);
    }

    /* Invalid enum values -> rejected by encode. */
    dongle_prefs_t invalid = prefs;
    invalid.boot_profile = DONGLE_PROFILE_COUNT;
    CHECK(dongle_prefs_encode(&invalid, blob, sizeof(blob)) == DONGLE_PREFS_INVALID_DATA);
    invalid = prefs;
    invalid.audio_codec = DONGLE_AUDIO_CODEC_COUNT;
    CHECK(dongle_prefs_encode(&invalid, blob, sizeof(blob)) == DONGLE_PREFS_INVALID_DATA);
    invalid = prefs;
    invalid.mic_source = DONGLE_MIC_SOURCE_COUNT;
    CHECK(dongle_prefs_encode(&invalid, blob, sizeof(blob)) == DONGLE_PREFS_INVALID_DATA);
    invalid = prefs;
    invalid.dualsense_poll_rate = DONGLE_DS_POLL_RATE_COUNT;
    CHECK(dongle_prefs_encode(&invalid, blob, sizeof(blob)) == DONGLE_PREFS_INVALID_DATA);
    invalid = prefs;
    invalid.rumble_gain_pct = DONGLE_RUMBLE_GAIN_MAX + 1U;
    CHECK(dongle_prefs_encode(&invalid, blob, sizeof(blob)) == DONGLE_PREFS_INVALID_DATA);

    /* Rumble gain 0% and 400% round-trip cleanly. */
    prefs.rumble_gain_pct = 0U;
    CHECK(dongle_prefs_encode(&prefs, blob, sizeof(blob)) == DONGLE_PREFS_OK);
    CHECK(dongle_prefs_decode(blob, sizeof(blob), &decoded) == DONGLE_PREFS_OK);
    CHECK(decoded.rumble_gain_pct == 0U);
    prefs.rumble_gain_pct = DONGLE_RUMBLE_GAIN_MAX;
    CHECK(dongle_prefs_encode(&prefs, blob, sizeof(blob)) == DONGLE_PREFS_OK);
    CHECK(dongle_prefs_decode(blob, sizeof(blob), &decoded) == DONGLE_PREFS_OK);
    CHECK(decoded.rumble_gain_pct == DONGLE_RUMBLE_GAIN_MAX);

    puts("Preferences codec tests passed");
    return 0;
}
