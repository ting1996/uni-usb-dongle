#include "dongle/audio_stream.h"
#include <limits.h>
#include <string.h>

#ifdef BL616
#define DONGLE_TCM_CODE __attribute__((section(".tcm_code")))
#else
#define DONGLE_TCM_CODE
#endif

bool dongle_audio_sbc_rtp_timestamp(uint8_t *p, size_t size, uint32_t *next_sample)
{
    if (!p || !next_sample || size < 17 || p[0] != 0x80 || (p[1] & 0x7f) != 96 ||
        !p[12] || (p[12] & 0xf0) || p[13] != 0x9c) return false;
    unsigned blocks = 4 * (((p[14] >> 4) & 3) + 1);
    unsigned subbands = (p[14] & 1) ? 8 : 4;
    unsigned mode = (p[14] >> 2) & 3;
    unsigned channels = mode ? 2 : 1;
    unsigned bits = blocks * p[15] * (mode < 2 ? channels : 1) + (mode == 3 ? subbands : 0);
    unsigned frame_size = 4 + subbands * channels / 2 + (bits + 7) / 8;
    if (size != 13 + (size_t)p[12] * frame_size) return false;
    uint32_t timestamp = *next_sample;
    for (unsigned i = 0; i < 4; ++i) p[4 + i] = timestamp >> (24 - i * 8);
    *next_sample += p[12] * blocks * subbands;
    return true;
}

bool dongle_audio_format_is_supported(const dongle_audio_format_t *format)
{
    if (format == NULL || (format->channels != 1U && format->channels != 2U) ||
        format->bits_per_sample != 16U) {
        return false;
    }

    switch (format->sample_rate_hz) {
        case 8000U:
        case 16000U:
        case 32000U:
        case 44100U:
        case 48000U:
        case 88200U:
        case 96000U:
            return true;
        default:
            return false;
    }
}

dongle_audio_stream_result_t dongle_audio_stream_init(
    dongle_audio_stream_t *stream,
    uint8_t *storage,
    size_t capacity,
    const dongle_audio_format_t *format)
{
    if (stream == NULL || storage == NULL || format == NULL) {
        return DONGLE_AUDIO_STREAM_INVALID_ARGUMENT;
    }
    if (!dongle_audio_format_is_supported(format)) {
        return DONGLE_AUDIO_STREAM_UNSUPPORTED_FORMAT;
    }

    const uint8_t frame_size =
        (uint8_t)(format->channels * (format->bits_per_sample / 8U));
    if (capacity < frame_size || (capacity & (capacity - 1U)) != 0U ||
        capacity % frame_size != 0U ||
        capacity > (size_t)INT32_MAX) {
        return DONGLE_AUDIO_STREAM_INVALID_ARGUMENT;
    }

    stream->storage = storage;
    stream->capacity = capacity;
    stream->frame_size = frame_size;
    stream->format = *format;
    atomic_init(&stream->write_cursor, 0U);
    atomic_init(&stream->read_cursor, 0U);
    return DONGLE_AUDIO_STREAM_OK;
}

static DONGLE_TCM_CODE void copy_into_ring(
    uint8_t *ring,
    size_t capacity,
    uint32_t cursor,
    const uint8_t *source,
    size_t size)
{
    const size_t offset = cursor % capacity;
    const size_t first_size = size < capacity - offset ? size : capacity - offset;
    memcpy(&ring[offset], source, first_size);
    memcpy(ring, &source[first_size], size - first_size);
}

static DONGLE_TCM_CODE void copy_from_ring(
    const uint8_t *ring,
    size_t capacity,
    uint32_t cursor,
    uint8_t *destination,
    size_t size)
{
    const size_t offset = cursor % capacity;
    const size_t first_size = size < capacity - offset ? size : capacity - offset;
    memcpy(destination, &ring[offset], first_size);
    memcpy(&destination[first_size], ring, size - first_size);
}

DONGLE_TCM_CODE dongle_audio_stream_result_t dongle_audio_stream_write(
    dongle_audio_stream_t *stream,
    const uint8_t *samples,
    size_t sample_size,
    size_t *written_size)
{
    if (stream == NULL || written_size == NULL ||
        (sample_size > 0U && samples == NULL) ||
        stream->storage == NULL || stream->frame_size == 0U ||
        sample_size % stream->frame_size != 0U) {
        return DONGLE_AUDIO_STREAM_INVALID_ARGUMENT;
    }

    *written_size = 0U;
    const uint32_t write_cursor =
        atomic_load_explicit(&stream->write_cursor, memory_order_relaxed);
    const uint32_t read_cursor =
        atomic_load_explicit(&stream->read_cursor, memory_order_acquire);
    const uint32_t used = write_cursor - read_cursor;
    if (used > stream->capacity) {
        return DONGLE_AUDIO_STREAM_INVALID_ARGUMENT;
    }

    const size_t available = stream->capacity - used;
    size_t size = sample_size < available ? sample_size : available;
    size -= size % stream->frame_size;
    if (size > 0U) {
        copy_into_ring(stream->storage, stream->capacity, write_cursor,
                       samples, size);
        atomic_store_explicit(&stream->write_cursor,
                              write_cursor + (uint32_t)size,
                              memory_order_release);
    }
    *written_size = size;
    return size == sample_size ? DONGLE_AUDIO_STREAM_OK
                               : DONGLE_AUDIO_STREAM_PARTIAL;
}

DONGLE_TCM_CODE dongle_audio_stream_result_t dongle_audio_stream_read(
    dongle_audio_stream_t *stream,
    uint8_t *samples,
    size_t sample_capacity,
    size_t *read_size)
{
    if (stream == NULL || read_size == NULL ||
        (sample_capacity > 0U && samples == NULL) ||
        stream->storage == NULL || stream->frame_size == 0U ||
        sample_capacity % stream->frame_size != 0U) {
        return DONGLE_AUDIO_STREAM_INVALID_ARGUMENT;
    }

    *read_size = 0U;
    const uint32_t read_cursor =
        atomic_load_explicit(&stream->read_cursor, memory_order_relaxed);
    const uint32_t write_cursor =
        atomic_load_explicit(&stream->write_cursor, memory_order_acquire);
    const uint32_t used = write_cursor - read_cursor;
    if (used > stream->capacity) {
        return DONGLE_AUDIO_STREAM_INVALID_ARGUMENT;
    }

    size_t size = sample_capacity < used ? sample_capacity : used;
    size -= size % stream->frame_size;
    if (size > 0U) {
        copy_from_ring(stream->storage, stream->capacity, read_cursor,
                       samples, size);
        atomic_store_explicit(&stream->read_cursor,
                              read_cursor + (uint32_t)size,
                              memory_order_release);
    }
    *read_size = size;
    return size == sample_capacity ? DONGLE_AUDIO_STREAM_OK
                                   : DONGLE_AUDIO_STREAM_PARTIAL;
}

void dongle_audio_stream_reset(dongle_audio_stream_t *stream)
{
    if (stream == NULL || stream->storage == NULL) {
        return;
    }
    atomic_store_explicit(&stream->read_cursor, 0U, memory_order_relaxed);
    atomic_store_explicit(&stream->write_cursor, 0U, memory_order_relaxed);
}
