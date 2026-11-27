#include "dongle/dualsense_audio.h"
#include "dongle/dualsense.h"

#include <string.h>

#ifdef BL616
#define DONGLE_TCM_CODE __attribute__((section(".tcm_code")))
#else
#define DONGLE_TCM_CODE
#endif

#define SINC_HALF_TAPS 4
#define SINC_TAPS      (2 * SINC_HALF_TAPS)
#define RESAMP_PHASES  15

/* Pre-computed 15-phase, 8-tap Hann-windowed sinc coefficients in q15
 * (cutoff = 480/512; each phase row sums to exactly 32768). */
static const int16_t sinc_q15[RESAMP_PHASES][SINC_TAPS] = {
    { 76, -640, 1463, 30721, 2035, -1434, 620, -73 },
    { 51, -313, 0, 30216, 4234, -2133, 799, -86 },
    { 25, 0, -1266, 29378, 6562, -2787, 953, -97 },
    { 0, 289, -2329, 28228, 8982, -3374, 1076, -104 },
    { -24, 547, -3185, 26789, 11455, -3870, 1163, -107 },
    { -46, 768, -3834, 25091, 13939, -4254, 1210, -106 },
    { -65, 948, -4281, 23165, 16394, -4505, 1214, -102 },
    { -81, 1083, -4534, 21048, 18777, -4604, 1172, -93 },
    { -93, 1172, -4604, 18777, 21048, -4534, 1083, -81 },
    { -102, 1214, -4505, 16394, 23165, -4281, 948, -65 },
    { -106, 1210, -4254, 13939, 25091, -3834, 768, -46 },
    { -107, 1163, -3870, 11455, 26789, -3185, 547, -24 },
    { -104, 1076, -3374, 8982, 28228, -2329, 289, 0 },
    { -97, 953, -2787, 6562, 29378, -1266, 0, 25 },
    { -86, 799, -2133, 4234, 30216, 0, -313, 51 },
};

static DONGLE_TCM_CODE int16_t clamp_s16(int32_t v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

static DONGLE_TCM_CODE int8_t clamp_s8(int32_t v)
{
    if (v > 127) return 127;
    if (v < -128) return -128;
    return (int8_t)v;
}

DONGLE_TCM_CODE void dongle_ds_audio_resample_512_480(const int16_t *pcm4_512,
                                                      int16_t *stereo_out_480)
{
    if (!pcm4_512 || !stereo_out_480) {
        return;
    }

    for (int i = 0; i < (int)DONGLE_DS_AUDIO_OPUS_SAMPLES; ++i) {
        const uint32_t src_pos = (uint32_t)i * 16U;
        const int center = (int)(src_pos / RESAMP_PHASES);
        const int phase = (int)(src_pos % RESAMP_PHASES);
        const int16_t *c = sinc_q15[phase];
        int32_t sum_l = 1 << 14;
        int32_t sum_r = 1 << 14;

        if (center >= SINC_HALF_TAPS - 1 &&
            center <= (int)DONGLE_DS_AUDIO_HALF_SAMPLES - SINC_HALF_TAPS - 1) {
            const int16_t *s = pcm4_512 + (center - (SINC_HALF_TAPS - 1)) * 4;
#define DS_RESAMP_TAP(t) do { \
                const int32_t coeff = (int32_t)c[(t)]; \
                sum_l += (int32_t)s[(t) * 4] * coeff; \
                sum_r += (int32_t)s[(t) * 4 + 1] * coeff; \
            } while (0)
            DS_RESAMP_TAP(0);
            DS_RESAMP_TAP(1);
            DS_RESAMP_TAP(2);
            DS_RESAMP_TAP(3);
            DS_RESAMP_TAP(4);
            DS_RESAMP_TAP(5);
            DS_RESAMP_TAP(6);
            DS_RESAMP_TAP(7);
#undef DS_RESAMP_TAP
        } else {
            for (int t = 0; t < SINC_TAPS; ++t) {
                int idx = center + t - (SINC_HALF_TAPS - 1);
                if (idx < 0) idx = 0;
                if (idx >= (int)DONGLE_DS_AUDIO_HALF_SAMPLES) {
                    idx = (int)DONGLE_DS_AUDIO_HALF_SAMPLES - 1;
                }
                const int32_t coeff = (int32_t)c[t];
                sum_l += (int32_t)pcm4_512[idx * 4] * coeff;
                sum_r += (int32_t)pcm4_512[idx * 4 + 1] * coeff;
            }
        }

        stereo_out_480[i * 2] = clamp_s16(sum_l >> 15);
        stereo_out_480[i * 2 + 1] = clamp_s16(sum_r >> 15);
    }
}

DONGLE_TCM_CODE void dongle_ds_audio_decimate_haptics(
    const int16_t *pcm4_512,
    uint16_t gain_pct,
    int8_t out_64[DONGLE_DS_AUDIO_HAPTIC_BYTES])
{
    if (!pcm4_512 || !out_64) {
        return;
    }

    if (gain_pct > 400U) {
        gain_pct = 400U;
    }
    const int32_t gain_fp = (int32_t)(((uint32_t)gain_pct * 256U) / 100U);

    for (uint32_t i = 0U; i < DONGLE_DS_AUDIO_HAPTIC_BYTES / 2U; ++i) {
        const uint32_t idx = i * 16U * 4U;
        const int32_t val_l = ((int32_t)pcm4_512[idx + 2U] * gain_fp) >> 16;
        const int32_t val_r = ((int32_t)pcm4_512[idx + 3U] * gain_fp) >> 16;
        out_64[i * 2U] = clamp_s8(val_l);
        out_64[i * 2U + 1U] = clamp_s8(val_r);
    }
}

void dongle_ds_audio_build_report_39(
    uint8_t seq,
    uint8_t packet_counter,
    bool mic_active,
    bool headset_plugged,
    bool speaker_enabled,
    const int8_t haptic0[DONGLE_DS_AUDIO_HAPTIC_BYTES],
    const int8_t haptic1[DONGLE_DS_AUDIO_HAPTIC_BYTES],
    const uint8_t opus0[DONGLE_DS_AUDIO_OPUS_BYTES],
    const uint8_t opus1[DONGLE_DS_AUDIO_OPUS_BYTES],
    uint8_t out[DONGLE_DS_AUDIO_BT_REPORT_SIZE])
{
    if (!out) {
        return;
    }

    memset(out, 0, DONGLE_DS_AUDIO_BT_REPORT_SIZE);
    out[0] = DONGLE_DS_AUDIO_BT_REPORT_ID;
    out[1] = (uint8_t)((seq & 0x0FU) << 4U);

    /* Audio control header (tag 0x91, 6 bytes) */
    out[2] = 0x91U;
    out[3] = 6U;
    out[4] = mic_active ? 0x7FU : 0x7EU;
    out[5] = DONGLE_DS_AUDIO_DEFAULT_BUF_LEN;
    out[6] = DONGLE_DS_AUDIO_DEFAULT_BUF_LEN;
    out[7] = DONGLE_DS_AUDIO_DEFAULT_BUF_LEN;
    out[8] = DONGLE_DS_AUDIO_DEFAULT_BUF_LEN;
    out[9] = packet_counter;

    /* Haptics (tag 0xD2, 2x 64-byte blocks) */
    out[10] = 0xD2U;
    out[11] = DONGLE_DS_AUDIO_HAPTIC_BYTES;
    if (haptic0) {
        memcpy(out + 12U, haptic0, DONGLE_DS_AUDIO_HAPTIC_BYTES);
    }
    if (haptic1) {
        memcpy(out + 12U + DONGLE_DS_AUDIO_HAPTIC_BYTES, haptic1,
               DONGLE_DS_AUDIO_HAPTIC_BYTES);
    }

    /* Speaker / 3.5mm headset Opus (tag 0xD3 / 0xD6, 2x 200-byte blocks) */
    if (speaker_enabled) {
        out[140] = headset_plugged ? 0xD6U : 0xD3U;
        out[141] = DONGLE_DS_AUDIO_OPUS_BYTES;
        if (opus0) {
            memcpy(out + 142U, opus0, DONGLE_DS_AUDIO_OPUS_BYTES);
        }
        if (opus1) {
            memcpy(out + 142U + DONGLE_DS_AUDIO_OPUS_BYTES, opus1,
                   DONGLE_DS_AUDIO_OPUS_BYTES);
        }
    }

    out[542] = 0x00U;
    const uint32_t crc = dongle_dualsense_crc32(
        0xA2U, out, DONGLE_DS_AUDIO_BT_REPORT_SIZE - 4U);
    out[543] = (uint8_t)crc;
    out[544] = (uint8_t)(crc >> 8U);
    out[545] = (uint8_t)(crc >> 16U);
    out[546] = (uint8_t)(crc >> 24U);
}
