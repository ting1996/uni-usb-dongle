#ifndef DONGLE_AUDIO_STREAM_H
#define DONGLE_AUDIO_STREAM_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t sample_rate_hz;
    uint8_t channels;
    uint8_t bits_per_sample;
} dongle_audio_format_t;

typedef enum {
    DONGLE_AUDIO_STREAM_OK = 0,
    DONGLE_AUDIO_STREAM_PARTIAL,
    DONGLE_AUDIO_STREAM_INVALID_ARGUMENT,
    DONGLE_AUDIO_STREAM_UNSUPPORTED_FORMAT
} dongle_audio_stream_result_t;

/* One producer and one consumer may run concurrently; reset only when stopped. */
typedef struct {
    uint8_t *storage;
    size_t capacity;
    uint8_t frame_size;
    dongle_audio_format_t format;
    atomic_uint_least32_t write_cursor;
    atomic_uint_least32_t read_cursor;
} dongle_audio_stream_t;

bool dongle_audio_format_is_supported(const dongle_audio_format_t *format);

dongle_audio_stream_result_t dongle_audio_stream_init(
    dongle_audio_stream_t *stream,
    uint8_t *storage,
    size_t capacity,
    const dongle_audio_format_t *format);

dongle_audio_stream_result_t dongle_audio_stream_write(
    dongle_audio_stream_t *stream,
    const uint8_t *samples,
    size_t sample_size,
    size_t *written_size);

dongle_audio_stream_result_t dongle_audio_stream_read(
    dongle_audio_stream_t *stream,
    uint8_t *samples,
    size_t sample_capacity,
    size_t *read_size);

void dongle_audio_stream_reset(dongle_audio_stream_t *stream);

/* Rewrite complete, unfragmented SBC RTP packets using the audio sample clock.
 * Other packet formats and incomplete payloads are left untouched. */
bool dongle_audio_sbc_rtp_timestamp(uint8_t *packet, size_t size, uint32_t *next_sample);

#endif
