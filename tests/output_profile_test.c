#include "dongle/output_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                          \
    do {                                                                     \
        if (!(expr)) {                                                       \
            fprintf(stderr, "Check failed at line %d: %s\n", __LINE__,       \
                    #expr);                                                  \
            exit(EXIT_FAILURE);                                              \
        }                                                                    \
    } while (0)

static int16_t read_le16_signed(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U));
}

static uint16_t read_u12_lo(const uint8_t p[3])
{
    return (uint16_t)(p[0] | ((uint16_t)(p[1] & 0x0fU) << 8U));
}

static uint16_t read_u12_hi(const uint8_t p[3])
{
    return (uint16_t)(((uint16_t)(p[1] >> 4U) & 0x0fU) | ((uint16_t)p[2] << 4U));
}

static void test_validation_rejects_bad_arguments(void)
{
    const uint8_t neutral[9] = { 128, 128, 128, 128, 0, 0, 0, 0, 8 };
    uint8_t bad_hat[9] = { 128, 128, 128, 128, 0, 0, 0, 0, 9 };
    uint8_t out[DONGLE_OUTPUT_MAX_REPORT];

    CHECK(dongle_output_encode(DONGLE_PROFILE_GENERIC, NULL, 0U, out) == 0U);
    CHECK(dongle_output_encode(DONGLE_PROFILE_GENERIC, neutral, 0U, NULL) == 0U);
    CHECK(dongle_output_encode(DONGLE_PROFILE_GENERIC, bad_hat, 0U, out) == 0U);
    CHECK(dongle_output_encode((dongle_output_profile_t)DONGLE_PROFILE_COUNT,
                               neutral, 0U, out) == 0U);
}

static void test_generic_profile_encoding(void)
{
    const uint8_t input[9] = { 10, 20, 200, 240, 12, 34, 0xab, 0x35, 3 };
    uint8_t out[DONGLE_OUTPUT_MAX_REPORT];

    CHECK(dongle_output_encode(DONGLE_PROFILE_GENERIC, input, 7U, out) == 10U);
    CHECK(out[0] == 1U);
    CHECK(memcmp(&out[1], input, sizeof(input)) == 0);
}

static void test_xinput_profile_encoding(void)
{
    /* Hat 2 = right; buttons: Square(0), Cross(1), R1(5), Share(8), Options(9), PS(12) */
    const uint16_t btns = (1U << 0) | (1U << 1) | (1U << 5) |
                          (1U << 8) | (1U << 9) | (1U << 12);
    const uint8_t input[9] = {
        0, 0, 255, 255, 77, 199,
        (uint8_t)(btns & 0xffU), (uint8_t)(btns >> 8U), 2
    };
    uint8_t out[DONGLE_OUTPUT_MAX_REPORT];

    CHECK(dongle_output_encode(DONGLE_PROFILE_XINPUT, input, 0U, out) == 20U);
    CHECK(out[0] == 0U && out[1] == 20U);
    /* D-pad right (bit 3) + Back(bit5) + Start(bit4) + RB(bit9) + Guide(bit10) + A(bit12) + X(bit14) */
    const uint16_t expected_b = (1U << 3) | (1U << 5) | (1U << 4) |
                                (1U << 9) | (1U << 10) | (1U << 12) | (1U << 14);
    const uint16_t actual_b = (uint16_t)(out[2] | ((uint16_t)out[3] << 8U));
    CHECK(actual_b == expected_b);
    CHECK(out[4] == 77U && out[5] == 199U);
    CHECK(read_le16_signed(&out[6]) == -32768);
    CHECK(read_le16_signed(&out[8]) == 32767);
    CHECK(read_le16_signed(&out[10]) == 32767);
    CHECK(read_le16_signed(&out[12]) == -32767);
}

static void test_ps4_profile_encoding(void)
{
    const uint16_t btns = (1U << 0) | (1U << 4) | (1U << 9) | (1U << 13);
    const uint8_t input[9] = {
        32, 64, 192, 224, 111, 222,
        (uint8_t)(btns & 0xffU), (uint8_t)(btns >> 8U), 5
    };
    uint8_t out[DONGLE_OUTPUT_MAX_REPORT];

    CHECK(dongle_output_encode(DONGLE_PROFILE_PS4, input, 65U, out) == 64U);
    CHECK(out[0] == 1U);
    CHECK(memcmp(&out[1], input, 4U) == 0);
    CHECK(out[5] == (uint8_t)((1U << 4) | 5U));
    CHECK(out[6] == (uint8_t)(1U | (2U << 4)));
    CHECK(out[7] == (uint8_t)(2U | (1U << 2))); /* counter=65 -> 65 & 63 == 1 */
    CHECK(out[8] == 111U && out[9] == 222U);
    CHECK(out[30] == 0x1bU);
    CHECK(out[35] == 0x80U && out[39] == 0x80U);
}

static void test_switch_profile_encoding(void)
{
    /* Square->Y(bit0), Cross->B(bit2), L1(bit4)->out[5]|0x40, R1(bit5)->out[3]|0x40,
     * analog L2=200(>127)->out[5]|0x80, analog R2=200(>127)->out[3]|0x80,
     * Share(8)->out[4]|1, Options(9)->out[4]|2, PS(12)->out[4]|0x10 */
    const uint16_t btns = (1U << 0) | (1U << 1) | (1U << 4) | (1U << 5) |
                          (1U << 8) | (1U << 9) | (1U << 12);
    const uint8_t input[9] = {
        0, 0, 255, 255, 200, 200,
        (uint8_t)(btns & 0xffU), (uint8_t)(btns >> 8U), 0
    };
    uint8_t out[DONGLE_OUTPUT_MAX_REPORT];

    CHECK(dongle_output_encode(DONGLE_PROFILE_SWITCH, input, 42U, out) == 64U);
    CHECK(out[0] == 0x30U && out[1] == 42U && out[2] == 0x81U);
    CHECK(out[3] == (uint8_t)(1U | 4U | 0x40U | 0x80U));
    CHECK(out[4] == (uint8_t)(1U | 2U | 0x10U));
    CHECK(out[5] == (uint8_t)(2U | 0x40U | 0x80U));
    CHECK(read_u12_lo(&out[6]) == 0U);
    CHECK(read_u12_hi(&out[6]) == 4095U);
    CHECK(read_u12_lo(&out[9]) == 4095U);
    CHECK(read_u12_hi(&out[9]) == 0U);
}

static void test_switch_handshake_and_subcommands(void)
{
    const uint8_t neutral[9] = { 128, 128, 128, 128, 0, 0, 0, 0, 8 };
    const uint8_t addr[6] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
    uint8_t out[DONGLE_OUTPUT_MAX_REPORT];

    const uint8_t hs_mac[2] = { 0x80, 0x01 };
    CHECK(dongle_switch_reply(hs_mac, sizeof(hs_mac), neutral, 3U, addr, out) == 64U);
    CHECK(out[0] == 0x81U && out[1] == 0x01U && out[3] == 0x03U);
    CHECK(memcmp(&out[4], addr, sizeof(addr)) == 0);

    const uint8_t hs_baud[2] = { 0x80, 0x02 };
    CHECK(dongle_switch_reply(hs_baud, sizeof(hs_baud), neutral, 3U, addr, out) == 64U);
    CHECK(out[0] == 0x81U && out[1] == 0x02U);

    const uint8_t hs_bad[2] = { 0x80, 0x06 };
    CHECK(dongle_switch_reply(hs_bad, sizeof(hs_bad), neutral, 3U, addr, out) == 0U);

    uint8_t dev_info[12] = { 0x01 };
    dev_info[10] = 0x02U;
    CHECK(dongle_switch_reply(dev_info, sizeof(dev_info), neutral, 9U, addr, out) == 64U);
    CHECK(out[0] == 0x21U && out[1] == 9U && out[13] == 0x82U && out[14] == 0x02U);
    CHECK(out[15] == 3U && out[16] == 0x48U);
    CHECK(memcmp(&out[19], addr, sizeof(addr)) == 0);

    uint8_t spi_cal[16] = { 0x01 };
    spi_cal[10] = 0x10U;
    spi_cal[11] = 0x3dU;
    spi_cal[12] = 0x60U;
    spi_cal[15] = 18U;
    CHECK(dongle_switch_reply(spi_cal, sizeof(spi_cal), neutral, 10U, addr, out) == 64U);
    CHECK(out[0] == 0x21U && out[13] == 0x90U && out[14] == 0x10U);
    CHECK(read_u12_lo(&out[20]) == 2047U && read_u12_hi(&out[20]) == 2047U);
    CHECK(read_u12_lo(&out[23]) == 2048U && read_u12_hi(&out[23]) == 2048U);

    spi_cal[11] = 0x86U;
    spi_cal[15] = 18U;
    CHECK(dongle_switch_reply(spi_cal, sizeof(spi_cal), neutral, 11U, addr, out) == 64U);
    CHECK(out[13] == 0x90U && out[20] == 0x0fU && out[21] == 0x30U);

    spi_cal[15] = 30U; /* Exceeds 29-byte limit -> NACK */
    CHECK(dongle_switch_reply(spi_cal, sizeof(spi_cal), neutral, 12U, addr, out) == 64U);
    CHECK(out[13] == 0U);

    uint8_t ack_sub[12] = { 0x01 };
    ack_sub[10] = 0x40U;
    CHECK(dongle_switch_reply(ack_sub, sizeof(ack_sub), neutral, 13U, addr, out) == 64U);
    CHECK(out[13] == 0x80U && out[14] == 0x40U);

    ack_sub[10] = 0x99U;
    CHECK(dongle_switch_reply(ack_sub, sizeof(ack_sub), neutral, 14U, addr, out) == 64U);
    CHECK(out[13] == 0U);
}

static void test_dualsense_profile_encoding(void)
{
    const uint16_t btns = (1U << 0) | (1U << 4) | (1U << 9) | (1U << 13);
    const uint8_t input[9] = {
        32, 64, 192, 224, 111, 222,
        (uint8_t)(btns & 0xffU), (uint8_t)(btns >> 8U), 5
    };
    uint8_t out[DONGLE_OUTPUT_MAX_REPORT];

    CHECK(dongle_output_encode(DONGLE_PROFILE_DUALSENSE, input, 42U, out) ==
          DONGLE_DUALSENSE_USB_INPUT_SIZE);
    CHECK(out[0] == 1U);
    CHECK(memcmp(&out[1], input, 6U) == 0);
    CHECK(out[7] == 42U);
    CHECK(out[8] == (uint8_t)((1U << 4) | 5U));
    CHECK(out[9] == (uint8_t)(1U | (2U << 4)));
    CHECK(out[10] == 2U);
    CHECK(out[33] == 0x80U && out[37] == 0x80U);
    CHECK(out[53] == 0x1aU);
}

int main(void)
{
    test_validation_rejects_bad_arguments();
    test_generic_profile_encoding();
    test_xinput_profile_encoding();
    test_ps4_profile_encoding();
    test_switch_profile_encoding();
    test_switch_handshake_and_subcommands();
    test_dualsense_profile_encoding();
    puts("Output profile, Switch handshake, and DualSense fallback tests passed");
    return 0;
}
