#include "dongle/a2dp_packetizer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "Check failed at line %d: %s\n", __LINE__, #expr); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

static void test_sbc_and_ldac_and_aptx_framing(void)
{
    uint8_t pdu[128];
    const uint8_t dummy[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

    size_t n = dongle_a2dp_packetize_sbc(pdu, sizeof(pdu), 0x1234U, 0x11223344U, 1U, 3U, dummy, 8U);
    CHECK(n == 12U + 1U + 8U);
    CHECK(pdu[0] == 0x80U && pdu[1] == 96U);
    CHECK(pdu[2] == 0x12U && pdu[3] == 0x34U);
    CHECK(pdu[12] == 3U);

    n = dongle_a2dp_packetize_ldac(pdu, sizeof(pdu), 1U, 128U, 2U, 2U, dummy, 8U);
    CHECK(n == 12U + 1U + 8U);
    CHECK(pdu[12] == 2U);

    /* aptX Classic has NO RTP header. */
    n = dongle_a2dp_packetize_aptx(pdu, sizeof(pdu), dummy, 8U);
    CHECK(n == 8U);
    CHECK(memcmp(pdu, dummy, 8U) == 0);

    /* aptX HD HAS 12B RTP header. */
    n = dongle_a2dp_packetize_aptx_hd(pdu, sizeof(pdu), 7U, 256U, 3U, dummy, 8U);
    CHECK(n == 20U);
    CHECK(pdu[0] == 0x80U && pdu[3] == 7U);
    CHECK(memcmp(&pdu[12], dummy, 8U) == 0);
}

static void test_aac_latm_packetizer(void)
{
    uint8_t pdu[512];
    uint8_t raw_aac[300];
    for (size_t i = 0U; i < sizeof(raw_aac); ++i) {
        raw_aac[i] = (uint8_t)(i & 0xFFU);
    }

    const size_t n = dongle_a2dp_packetize_aac_latm(
        pdu, sizeof(pdu), 10U, 1024U, 0x55U, 48000U, raw_aac, sizeof(raw_aac));
    /* Header is 46 bits = 5 bytes + 6 bits, plus 2 bytes PayloadLengthInfo (255 + 45) = 62 bits -> 8 bytes?
     * 16 (streamMux/prog/layer) + 16 (AudioSpecificConfig) + 13 (frameLen/fullness/other/crc) + 16 (len=255,45) = 61 bits + 2400 bits = 2461 bits -> 308 bytes + 12 RTP = 320 bytes. */
    CHECK(n == 320U);
    CHECK(pdu[0] == 0x80U && pdu[3] == 10U);
}

static void test_resampler(void)
{
    int16_t in[8] = { 0, 0, 1000, -1000, 2000, -2000, 3000, -3000 };
    int16_t out[4];
    CHECK(dongle_audio_resample_s16_stereo(in, 4U, out, 2U) == 2U);
    CHECK(out[0] == 0 && out[1] == 0);
    CHECK(out[2] == 3000 && out[3] == -3000);
}

int main(void)
{
    test_sbc_and_ldac_and_aptx_framing();
    test_aac_latm_packetizer();
    test_resampler();
    puts("A2DP packetizer tests passed");
    return 0;
}
