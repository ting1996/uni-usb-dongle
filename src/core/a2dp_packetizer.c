#include "dongle/a2dp_packetizer.h"

#include <string.h>

typedef struct {
    uint8_t *buf;
    size_t capacity;
    size_t bit_pos;
} bitwriter_t;

static void bw_write_bits(bitwriter_t *bw, uint32_t val, unsigned bits)
{
    for (unsigned i = 0U; i < bits; ++i) {
        const size_t byte_idx = bw->bit_pos >> 3U;
        const unsigned bit_in_byte = 7U - (unsigned)(bw->bit_pos & 7U);
        if (byte_idx < bw->capacity) {
            if (bit_in_byte == 7U) {
                bw->buf[byte_idx] = 0U;
            }
            const uint32_t bit = (val >> (bits - 1U - i)) & 1U;
            bw->buf[byte_idx] |= (uint8_t)(bit << bit_in_byte);
        }
        ++bw->bit_pos;
    }
}

static void bw_write_bytes(bitwriter_t *bw, const uint8_t *data, size_t len)
{
    if ((bw->bit_pos & 7U) == 0U) {
        const size_t byte_idx = bw->bit_pos >> 3U;
        if (byte_idx + len <= bw->capacity) {
            memcpy(&bw->buf[byte_idx], data, len);
        }
        bw->bit_pos += len * 8U;
        return;
    }
    for (size_t i = 0U; i < len; ++i) {
        bw_write_bits(bw, data[i], 8U);
    }
}

void dongle_a2dp_write_rtp_header(uint8_t out[DONGLE_A2DP_RTP_HEADER_SIZE],
                                  uint16_t seq,
                                  uint32_t timestamp,
                                  uint32_t ssrc,
                                  uint8_t payload_type)
{
    out[0] = 0x80U;
    out[1] = (uint8_t)(payload_type & 0x7FU);
    out[2] = (uint8_t)(seq >> 8U);
    out[3] = (uint8_t)(seq & 0xFFU);
    out[4] = (uint8_t)(timestamp >> 24U);
    out[5] = (uint8_t)(timestamp >> 16U);
    out[6] = (uint8_t)(timestamp >> 8U);
    out[7] = (uint8_t)(timestamp & 0xFFU);
    out[8] = (uint8_t)(ssrc >> 24U);
    out[9] = (uint8_t)(ssrc >> 16U);
    out[10] = (uint8_t)(ssrc >> 8U);
    out[11] = (uint8_t)(ssrc & 0xFFU);
}

static size_t packetize_rtp_with_1b_hdr(uint8_t *out, size_t capacity,
                                        uint16_t seq, uint32_t timestamp, uint32_t ssrc,
                                        uint8_t hdr_byte,
                                        const uint8_t *frames, size_t frames_len)
{
    const size_t total = DONGLE_A2DP_RTP_HEADER_SIZE + 1U + frames_len;
    if (out == NULL || frames == NULL || frames_len == 0U || capacity < total) {
        return 0U;
    }
    dongle_a2dp_write_rtp_header(out, seq, timestamp, ssrc, 96U);
    out[DONGLE_A2DP_RTP_HEADER_SIZE] = hdr_byte;
    memcpy(&out[DONGLE_A2DP_RTP_HEADER_SIZE + 1U], frames, frames_len);
    return total;
}

size_t dongle_a2dp_packetize_sbc(uint8_t *out, size_t capacity,
                                 uint16_t seq, uint32_t timestamp, uint32_t ssrc,
                                 uint8_t frame_count,
                                 const uint8_t *frames, size_t frames_len)
{
    return packetize_rtp_with_1b_hdr(out, capacity, seq, timestamp, ssrc,
                                     (uint8_t)(frame_count & 0x0FU),
                                     frames, frames_len);
}

size_t dongle_a2dp_packetize_ldac(uint8_t *out, size_t capacity,
                                  uint16_t seq, uint32_t timestamp, uint32_t ssrc,
                                  uint8_t frame_count,
                                  const uint8_t *frames, size_t frames_len)
{
    return packetize_rtp_with_1b_hdr(out, capacity, seq, timestamp, ssrc,
                                     (uint8_t)(frame_count & 0x0FU),
                                     frames, frames_len);
}

size_t dongle_a2dp_packetize_aac_latm(uint8_t *out, size_t capacity,
                                      uint16_t seq, uint32_t timestamp, uint32_t ssrc,
                                      uint32_t sample_rate_hz,
                                      const uint8_t *raw_aac, size_t raw_aac_len)
{
    if (out == NULL || raw_aac == NULL || raw_aac_len == 0U ||
        capacity <= DONGLE_A2DP_RTP_HEADER_SIZE + 8U) {
        return 0U;
    }

    uint8_t sf_idx = 4U; /* 44100 Hz */
    if (sample_rate_hz == 48000U) {
        sf_idx = 3U;
    } else if (sample_rate_hz != 44100U) {
        return 0U;
    }

    dongle_a2dp_write_rtp_header(out, seq, timestamp, ssrc, 96U);

    bitwriter_t bw = {
        .buf = &out[DONGLE_A2DP_RTP_HEADER_SIZE],
        .capacity = capacity - DONGLE_A2DP_RTP_HEADER_SIZE,
        .bit_pos = 0U,
    };

    /* AudioMuxElement (muxConfigPresent = 1) */
    bw_write_bits(&bw, 0U, 1U); /* useSameStreamMux = 0 */
    bw_write_bits(&bw, 0U, 1U); /* audioMuxVersion = 0 */
    bw_write_bits(&bw, 1U, 1U); /* allStreamsSameTimeFraming = 1 */
    bw_write_bits(&bw, 0U, 6U); /* numSubFrames = 0 (1 subframe) */
    bw_write_bits(&bw, 0U, 4U); /* numProgram = 0 (1 program) */
    bw_write_bits(&bw, 0U, 3U); /* numLayer = 0 (1 layer) */

    /* AudioSpecificConfig (AAC-LC, sf_idx, 2ch, 1024-sample GASpecificConfig) */
    bw_write_bits(&bw, 2U, 5U);      /* audioObjectType = 2 (AAC-LC) */
    bw_write_bits(&bw, sf_idx, 4U);  /* samplingFrequencyIndex */
    bw_write_bits(&bw, 2U, 4U);      /* channelConfiguration = 2 (stereo) */
    bw_write_bits(&bw, 0U, 1U);      /* frameLengthFlag = 0 (1024) */
    bw_write_bits(&bw, 0U, 1U);      /* dependsOnCoreCoder = 0 */
    bw_write_bits(&bw, 0U, 1U);      /* extensionFlag = 0 */

    bw_write_bits(&bw, 0U, 3U);      /* frameLengthType = 0 */
    bw_write_bits(&bw, 0xFFU, 8U);   /* latmBufferFullness = 0xFF */
    bw_write_bits(&bw, 0U, 1U);      /* otherDataPresent = 0 */
    bw_write_bits(&bw, 0U, 1U);      /* crcCheckPresent = 0 */

    /* PayloadLengthInfo */
    size_t rem = raw_aac_len;
    while (rem >= 255U) {
        bw_write_bits(&bw, 0xFFU, 8U);
        rem -= 255U;
    }
    bw_write_bits(&bw, (uint32_t)rem, 8U);

    /* PayloadMux */
    bw_write_bytes(&bw, raw_aac, raw_aac_len);

    const size_t mux_bytes = (bw.bit_pos + 7U) >> 3U;
    if (mux_bytes > bw.capacity) {
        return 0U;
    }
    return DONGLE_A2DP_RTP_HEADER_SIZE + mux_bytes;
}

size_t dongle_a2dp_packetize_aptx(uint8_t *out, size_t capacity,
                                  const uint8_t *frames, size_t frames_len)
{
    if (out == NULL || frames == NULL || frames_len == 0U || capacity < frames_len) {
        return 0U;
    }
    memcpy(out, frames, frames_len);
    return frames_len;
}

size_t dongle_a2dp_packetize_aptx_hd(uint8_t *out, size_t capacity,
                                     uint16_t seq, uint32_t timestamp, uint32_t ssrc,
                                     const uint8_t *frames, size_t frames_len)
{
    const size_t total = DONGLE_A2DP_RTP_HEADER_SIZE + frames_len;
    if (out == NULL || frames == NULL || frames_len == 0U || capacity < total) {
        return 0U;
    }
    dongle_a2dp_write_rtp_header(out, seq, timestamp, ssrc, 96U);
    memcpy(&out[DONGLE_A2DP_RTP_HEADER_SIZE], frames, frames_len);
    return total;
}

size_t dongle_audio_resample_s16_stereo(const int16_t *in, size_t in_frames,
                                        int16_t *out, size_t out_frames)
{
    if (in == NULL || out == NULL || in_frames == 0U || out_frames == 0U) {
        return 0U;
    }
    if (in_frames == out_frames) {
        memcpy(out, in, out_frames * 2U * sizeof(int16_t));
        return out_frames;
    }
    if (in_frames == 1U) {
        for (size_t i = 0U; i < out_frames; ++i) {
            out[i * 2U] = in[0];
            out[i * 2U + 1U] = in[1];
        }
        return out_frames;
    }

    const uint64_t step_q16 = ((uint64_t)(in_frames - 1U) << 16U) /
                              (uint64_t)(out_frames > 1U ? (out_frames - 1U) : 1U);
    uint64_t pos_q16 = 0U;
    for (size_t i = 0U; i < out_frames; ++i) {
        size_t idx0 = (size_t)(pos_q16 >> 16U);
        if (idx0 >= in_frames - 1U) {
            idx0 = in_frames - 1U;
            out[i * 2U] = in[idx0 * 2U];
            out[i * 2U + 1U] = in[idx0 * 2U + 1U];
        } else {
            const int32_t frac = (int32_t)(pos_q16 & 0xFFFFU);
            const int32_t l0 = in[idx0 * 2U];
            const int32_t r0 = in[idx0 * 2U + 1U];
            const int32_t l1 = in[(idx0 + 1U) * 2U];
            const int32_t r1 = in[(idx0 + 1U) * 2U + 1U];
            out[i * 2U] = (int16_t)(l0 + (((l1 - l0) * frac) >> 16));
            out[i * 2U + 1U] = (int16_t)(r0 + (((r1 - r0) * frac) >> 16));
        }
        pos_q16 += step_q16;
    }
    return out_frames;
}
