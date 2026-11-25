#ifndef DONGLE_A2DP_PACKETIZER_H
#define DONGLE_A2DP_PACKETIZER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DONGLE_A2DP_RTP_HEADER_SIZE 12U

void dongle_a2dp_write_rtp_header(uint8_t out[DONGLE_A2DP_RTP_HEADER_SIZE],
                                  uint16_t seq,
                                  uint32_t timestamp,
                                  uint32_t ssrc,
                                  uint8_t payload_type);

size_t dongle_a2dp_packetize_sbc(uint8_t *out, size_t capacity,
                                 uint16_t seq, uint32_t timestamp, uint32_t ssrc,
                                 uint8_t frame_count,
                                 const uint8_t *frames, size_t frames_len);

size_t dongle_a2dp_packetize_aac_latm(uint8_t *out, size_t capacity,
                                      uint16_t seq, uint32_t timestamp, uint32_t ssrc,
                                      uint32_t sample_rate_hz,
                                      const uint8_t *raw_aac, size_t raw_aac_len);

size_t dongle_a2dp_packetize_ldac(uint8_t *out, size_t capacity,
                                  uint16_t seq, uint32_t timestamp, uint32_t ssrc,
                                  uint8_t frame_count,
                                  const uint8_t *frames, size_t frames_len);

size_t dongle_a2dp_packetize_aptx(uint8_t *out, size_t capacity,
                                  const uint8_t *frames, size_t frames_len);

size_t dongle_a2dp_packetize_aptx_hd(uint8_t *out, size_t capacity,
                                     uint16_t seq, uint32_t timestamp, uint32_t ssrc,
                                     const uint8_t *frames, size_t frames_len);

/* Linear-interpolation stereo int16 resampler. Returns output frame count. */
size_t dongle_audio_resample_s16_stereo(const int16_t *in, size_t in_frames,
                                        int16_t *out, size_t out_frames);

#ifdef __cplusplus
}
#endif

#endif
