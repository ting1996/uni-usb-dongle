#include "dongle/audio_stream.h"

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

static void test_pcm_format_validation(void)
{
    const dongle_audio_format_t supported = {
        .sample_rate_hz = 48000U,
        .channels = 2U,
        .bits_per_sample = 16U,
    };
    const dongle_audio_format_t supported_96k = {
        .sample_rate_hz = 96000U,
        .channels = 2U,
        .bits_per_sample = 16U,
    };
    const dongle_audio_format_t unsupported = {
        .sample_rate_hz = 192000U,
        .channels = 2U,
        .bits_per_sample = 16U,
    };

    CHECK(dongle_audio_format_is_supported(&supported));
    CHECK(dongle_audio_format_is_supported(&supported_96k));
    CHECK(!dongle_audio_format_is_supported(&unsupported));
    CHECK(!dongle_audio_format_is_supported(NULL));
}

static void test_ring_wrap_and_partial_transfer_preserve_frames(void)
{
    dongle_audio_stream_t stream;
    uint8_t storage[16];
    uint8_t input[20];
    uint8_t output[20];
    size_t transferred;
    const dongle_audio_format_t format = {
        .sample_rate_hz = 44100U,
        .channels = 2U,
        .bits_per_sample = 16U,
    };

    for (size_t index = 0; index < sizeof(input); ++index) {
        input[index] = (uint8_t)index;
    }
    CHECK(dongle_audio_stream_init(&stream, storage, sizeof(storage), &format) ==
          DONGLE_AUDIO_STREAM_OK);
    CHECK(dongle_audio_stream_write(&stream, input, 12U, &transferred) ==
          DONGLE_AUDIO_STREAM_OK);
    CHECK(transferred == 12U);
    CHECK(dongle_audio_stream_read(&stream, output, 8U, &transferred) ==
          DONGLE_AUDIO_STREAM_OK);
    CHECK(transferred == 8U);
    CHECK(memcmp(output, input, 8U) == 0);

    CHECK(dongle_audio_stream_write(&stream, &input[12], 8U, &transferred) ==
          DONGLE_AUDIO_STREAM_OK);
    CHECK(transferred == 8U);
    CHECK(dongle_audio_stream_read(&stream, output, sizeof(output), &transferred) ==
          DONGLE_AUDIO_STREAM_PARTIAL);
    CHECK(transferred == 12U);
    CHECK(memcmp(output, &input[8], 12U) == 0);
    CHECK(dongle_audio_stream_read(&stream, output, 4U, &transferred) ==
          DONGLE_AUDIO_STREAM_PARTIAL);
    CHECK(transferred == 0U);
}

static void test_invalid_frame_sizes_and_capacity_are_rejected(void)
{
    dongle_audio_stream_t stream;
    uint8_t storage[15];
    uint8_t sample[4] = { 0 };
    size_t transferred;
    const dongle_audio_format_t format = {
        .sample_rate_hz = 16000U,
        .channels = 2U,
        .bits_per_sample = 16U,
    };

    CHECK(dongle_audio_stream_init(&stream, storage, sizeof(storage), &format) ==
          DONGLE_AUDIO_STREAM_INVALID_ARGUMENT);
    CHECK(dongle_audio_stream_init(&stream, storage, 16U, &format) ==
          DONGLE_AUDIO_STREAM_OK);
    CHECK(dongle_audio_stream_write(&stream, sample, 3U, &transferred) ==
          DONGLE_AUDIO_STREAM_INVALID_ARGUMENT);
    CHECK(dongle_audio_stream_read(&stream, sample, 3U, &transferred) ==
          DONGLE_AUDIO_STREAM_INVALID_ARGUMENT);
}

static void test_sbc_rtp_uses_sample_clock_and_preserves_payload(void)
{
    /* Real Theater U trace: five 119-byte, 44.1 kHz joint-stereo SBC frames.
     * SDK timestamps advanced in milliseconds; each packet covers 640 samples. */
    uint8_t packet[608] = {0x80, 0x60, 0, 1, 0, 0, 0xb2, 0, 0x11, 0x22, 0x33, 0x44,
                           5, 0x9c, 0xbd, 0x35, 0xa8};
    uint32_t clock = 0xffffff00U;
    CHECK(dongle_audio_sbc_rtp_timestamp(packet, sizeof(packet), &clock));
    CHECK(packet[4] == 0xff && packet[5] == 0xff && packet[6] == 0xff && packet[7] == 0);
    CHECK(clock == 0x180U);
    CHECK(dongle_audio_sbc_rtp_timestamp(packet, sizeof(packet), &clock));
    CHECK(packet[4] == 0 && packet[5] == 0 && packet[6] == 1 && packet[7] == 0x80);
    CHECK(clock == 0x400U && packet[16] == 0xa8 && packet[3] == 1);
    CHECK(!dongle_audio_sbc_rtp_timestamp(packet, sizeof(packet) - 1, &clock));
    packet[12] = 0x85; /* Fragmented SBC must not be interpreted as full frames. */
    CHECK(!dongle_audio_sbc_rtp_timestamp(packet, sizeof(packet), &clock));
    CHECK(clock == 0x400U);
}

int main(void)
{
    test_pcm_format_validation();
    test_ring_wrap_and_partial_transfer_preserve_frames();
    test_invalid_frame_sizes_and_capacity_are_rejected();
    test_sbc_rtp_uses_sample_clock_and_preserves_payload();
    return 0;
}
