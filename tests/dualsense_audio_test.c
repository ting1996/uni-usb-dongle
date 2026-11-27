#include "dongle/dualsense_audio.h"
#include "dongle/dualsense.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

int main(void)
{
    /* 1. Polyphase sinc resampler preserves DC across all 480 output frames. */
    int16_t pcm4[DONGLE_DS_AUDIO_HALF_SAMPLES * 4U];
    for (size_t i = 0U; i < DONGLE_DS_AUDIO_HALF_SAMPLES; ++i) {
        pcm4[i * 4U + 0U] = 10000;
        pcm4[i * 4U + 1U] = -10000;
        pcm4[i * 4U + 2U] = (int16_t)((i % 32U == 0U) ? 16384 : 0);
        pcm4[i * 4U + 3U] = (int16_t)((i % 32U == 0U) ? -16384 : 0);
    }

    int16_t stereo[DONGLE_DS_AUDIO_OPUS_SAMPLES * 2U];
    dongle_ds_audio_resample_512_480(pcm4, stereo);
    for (size_t i = 0U; i < DONGLE_DS_AUDIO_OPUS_SAMPLES; ++i) {
        CHECK(stereo[i * 2U] >= 9998 && stereo[i * 2U] <= 10002);
        CHECK(stereo[i * 2U + 1U] >= -10002 && stereo[i * 2U + 1U] <= -9998);
    }

    /* 2. 16:1 haptics decimation, gain scaling, and saturation clamping. */
    int8_t hap0[DONGLE_DS_AUDIO_HAPTIC_BYTES];
    dongle_ds_audio_decimate_haptics(pcm4, 100U, hap0);
    CHECK(hap0[0] == 64 && hap0[1] == -64);
    CHECK(hap0[2] == 0 && hap0[3] == 0);
    CHECK(hap0[4] == 64 && hap0[5] == -64);

    int8_t hap_gain[DONGLE_DS_AUDIO_HAPTIC_BYTES];
    dongle_ds_audio_decimate_haptics(pcm4, 200U, hap_gain);
    CHECK(hap_gain[0] == 127 && hap_gain[1] == -128);

    int8_t hap_zero[DONGLE_DS_AUDIO_HAPTIC_BYTES];
    dongle_ds_audio_decimate_haptics(pcm4, 0U, hap_zero);
    for (size_t i = 0U; i < DONGLE_DS_AUDIO_HAPTIC_BYTES; ++i) {
        CHECK(hap_zero[i] == 0);
    }

    /* 3. Report 0x39 layout, speaker/headset/mic/mute flags, and 0xA2 CRC-32. */
    uint8_t opus0[DONGLE_DS_AUDIO_OPUS_BYTES];
    uint8_t opus1[DONGLE_DS_AUDIO_OPUS_BYTES];
    memset(opus0, 0x5AU, sizeof(opus0));
    memset(opus1, 0xA5U, sizeof(opus1));

    uint8_t pkt[DONGLE_DS_AUDIO_BT_REPORT_SIZE];
    dongle_ds_audio_build_report_39(
        0x07U, 0x24U, true, false, true, hap0, hap_gain, opus0, opus1, pkt);
    CHECK(pkt[0] == 0x39U);
    CHECK(pkt[1] == 0x70U);
    CHECK(pkt[2] == 0x91U && pkt[3] == 6U && pkt[4] == 0x7FU);
    CHECK(pkt[5] == 0x10U && pkt[8] == 0x10U && pkt[9] == 0x24U);
    CHECK(pkt[10] == 0xD2U && pkt[11] == 64U);
    CHECK(memcmp(pkt + 12U, hap0, 64U) == 0);
    CHECK(memcmp(pkt + 76U, hap_gain, 64U) == 0);
    CHECK(pkt[140] == 0xD3U && pkt[141] == 200U);
    CHECK(memcmp(pkt + 142U, opus0, 200U) == 0);
    CHECK(memcmp(pkt + 342U, opus1, 200U) == 0);
    CHECK(pkt[542] == 0x00U);
    CHECK(read_le32(pkt + 543U) == dongle_dualsense_crc32(0xA2U, pkt, 543U));

    /* Headset plugged switches audio tag to 0xD6; mic_active=false sets 0x7E. */
    dongle_ds_audio_build_report_39(
        0x08U, 0x26U, false, true, true, hap0, hap_gain, opus0, opus1, pkt);
    CHECK(pkt[4] == 0x7EU);
    CHECK(pkt[140] == 0xD6U);
    CHECK(read_le32(pkt + 543U) == dongle_dualsense_crc32(0xA2U, pkt, 543U));

    /* Speaker disabled leaves bytes 140..541 zeroed. */
    dongle_ds_audio_build_report_39(
        0x09U, 0x28U, false, false, false, hap0, hap_gain, opus0, opus1, pkt);
    CHECK(pkt[140] == 0x00U && pkt[141] == 0x00U);
    CHECK(pkt[142] == 0x00U && pkt[541] == 0x00U);
    CHECK(read_le32(pkt + 543U) == dongle_dualsense_crc32(0xA2U, pkt, 543U));

    return 0;
}
