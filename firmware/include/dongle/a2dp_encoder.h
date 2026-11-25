#ifndef DONGLE_A2DP_ENCODER_H
#define DONGLE_A2DP_ENCODER_H

#include "dongle/a2dp_codec.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int dongle_a2dp_encoder_open(dongle_audio_codec_t codec,
                             uint32_t sample_rate_hz,
                             uint16_t media_mtu,
                             bool controller_active);

void dongle_a2dp_encoder_close(void);

/* Number of stereo PCM frames expected per dongle_a2dp_encoder_encode() call. */
size_t dongle_a2dp_encoder_frame_samples(void);

/* Encode `num_frames` stereo int16 samples into `out_pdu`.
 * Sets `*out_len = 0` when the encoder buffers a frame without emitting a PDU yet.
 * Returns 0 on success, <0 on error. */
int dongle_a2dp_encoder_encode(const int16_t *pcm_s16,
                               size_t num_frames,
                               uint8_t *out_pdu,
                               size_t out_capacity,
                               size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif
