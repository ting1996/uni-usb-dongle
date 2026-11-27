#ifndef DONGLE_DUALSENSE_AUDIO_H
#define DONGLE_DUALSENSE_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DONGLE_DS_AUDIO_BT_REPORT_ID   0x39U
#define DONGLE_DS_AUDIO_BT_REPORT_SIZE 547U
#define DONGLE_DS_AUDIO_ACCUM_SAMPLES  1024U
#define DONGLE_DS_AUDIO_HALF_SAMPLES   512U
#define DONGLE_DS_AUDIO_OPUS_SAMPLES   480U
#define DONGLE_DS_AUDIO_OPUS_BYTES     200U
#define DONGLE_DS_AUDIO_HAPTIC_BYTES   64U
#define DONGLE_DS_AUDIO_DEFAULT_BUF_LEN 0x10U

/* Polyphase 8-tap Hann-windowed sinc resampler: extracts Ch0/Ch1 from 512
 * 4-channel 48 kHz frames and resamples 512 -> 480 stereo int16 samples. */
void dongle_ds_audio_resample_512_480(const int16_t *pcm4_512,
                                      int16_t *stereo_out_480);

/* 16:1 point-sample decimation of Ch2/Ch3 from 512 4-channel frames into 32
 * stereo int8 pairs (64 bytes) with fixed-point gain (gain_pct 0..400). */
void dongle_ds_audio_decimate_haptics(const int16_t *pcm4_512,
                                      uint16_t gain_pct,
                                      int8_t out_64[DONGLE_DS_AUDIO_HAPTIC_BYTES]);

/* Builds a 547-byte DualSense Bluetooth 0x39 dual-frame audio/haptics report
 * with 0xA2-seeded CRC-32 trailer. */
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
    uint8_t out[DONGLE_DS_AUDIO_BT_REPORT_SIZE]);

#endif
