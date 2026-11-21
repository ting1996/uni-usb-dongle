#include "dongle/output_profile.h"
#include <string.h>

static void le16(uint8_t *p, int32_t value)
{
    const uint16_t raw = (uint16_t)value;
    p[0] = (uint8_t)raw;
    p[1] = (uint8_t)(raw >> 8U);
}
static int32_t stick(uint8_t value, bool invert)
{
    int32_t v = value < 128 ? ((int32_t)value - 128) * 256 :
                             ((int32_t)value - 128) * 32767 / 127;
    if (invert) v = v == -32768 ? 32767 : -v;
    return v;
}
static void stick12(uint8_t *p, uint16_t x, uint16_t y)
{
    p[0] = (uint8_t)x;
    p[1] = (uint8_t)((x >> 8U) | ((y & 15U) << 4U));
    p[2] = (uint8_t)(y >> 4U);
}
static uint16_t axis12(uint8_t v, bool invert)
{
    uint16_t result = (uint16_t)(v < 128U
        ? (uint32_t)v * 16U
        : 2048U + (((uint32_t)v - 128U) * 2047U) / 127U);
    return invert ? (uint16_t)(4095U - result) : result;
}
size_t dongle_output_encode(dongle_output_profile_t profile, const uint8_t in[9],
                           uint8_t counter, uint8_t out[64])
{
    if (!in || !out || in[8] > 8 || profile >= DONGLE_PROFILE_COUNT) return 0;
    memset(out, 0, 64);
    uint16_t buttons = (uint16_t)(in[6] | ((uint16_t)in[7] << 8U));
    uint8_t hat = in[8];
    bool up = hat == 7 || hat == 0 || hat == 1;
    bool down = hat >= 3 && hat <= 5;
    bool left = hat >= 5 && hat <= 7;
    bool right = hat >= 1 && hat <= 3;
    if (profile == DONGLE_PROFILE_GENERIC) {
        out[0] = 1; memcpy(out + 1, in, 9); return 10;
    }
    if (profile == DONGLE_PROFILE_XINPUT) {
        out[1] = 20;
        uint16_t b = (uint16_t)((up ? 1U : 0U) | (down ? 2U : 0U) |
                                (left ? 4U : 0U) | (right ? 8U : 0U));
        const uint8_t source[] = {9,8,10,11,4,5,12,1,2,0,3};
        const uint8_t target[] = {4,5,6,7,8,9,10,12,13,14,15};
        for (unsigned i = 0; i < sizeof(source); ++i)
            if (buttons & (1U << source[i])) b |= (uint16_t)(1U << target[i]);
        le16(out + 2, b); out[4] = in[4]; out[5] = in[5];
        le16(out + 6, stick(in[0], false)); le16(out + 8, stick(in[1], true));
        le16(out + 10, stick(in[2], false)); le16(out + 12, stick(in[3], true));
        return 20;
    }
    if (profile == DONGLE_PROFILE_PS4) {
        out[0] = 1; memcpy(out + 1, in, 4);
        out[5] = (uint8_t)(((buttons & 15U) << 4U) | hat);
        out[6] = (uint8_t)(((buttons >> 4U) & 15U) | (((buttons >> 8U) & 15U) << 4U));
        out[7] = (uint8_t)(((buttons >> 12U) & 3U) | ((counter & 63U) << 2U));
        out[8] = in[4]; out[9] = in[5];
        out[30] = 0x1b; /* USB attached, full battery. */
        out[35] = out[39] = 0x80; /* Both touch contacts inactive. */
        return 64;
    }
    if (profile == DONGLE_PROFILE_DUALSENSE) {
        out[0] = 0x01U;
        memcpy(out + 1, in, 6U);
        out[7] = counter;
        out[8] = (uint8_t)(((buttons & 15U) << 4U) | hat);
        out[9] = (uint8_t)((buttons >> 4U) & 0xffU);
        out[10] = (uint8_t)((buttons >> 12U) & 0x07U);
        out[33] = 0x80U;
        out[37] = 0x80U;
        out[53] = 0x1aU;
        return DONGLE_DUALSENSE_USB_INPUT_SIZE;
    }
    out[0] = 0x30; out[1] = counter; out[2] = 0x81;
    /* Nintendo face labels preserve physical positions: Square=Y, Cross=B. */
    if (buttons & 1) out[3] |= 1;
    if (buttons & 8) out[3] |= 2;
    if (buttons & 2) out[3] |= 4;
    if (buttons & 4) out[3] |= 8;
    if (buttons & (1 << 5)) out[3] |= 0x40;
    if (in[5] > 127 || buttons & (1 << 7)) out[3] |= 0x80;
    if (buttons & (1 << 8)) out[4] |= 1;
    if (buttons & (1 << 9)) out[4] |= 2;
    if (buttons & (1 << 11)) out[4] |= 4;
    if (buttons & (1 << 10)) out[4] |= 8;
    if (buttons & (1 << 12)) out[4] |= 0x10;
    if (buttons & (1 << 13)) out[4] |= 0x20;
    out[5] = (uint8_t)((down ? 1U : 0U) | (up ? 2U : 0U) |
                       (right ? 4U : 0U) | (left ? 8U : 0U));
    if (buttons & (1 << 4)) out[5] |= 0x40;
    if (in[4] > 127 || buttons & (1 << 6)) out[5] |= 0x80;
    stick12(out + 6, axis12(in[0], false), axis12(in[1], true));
    stick12(out + 9, axis12(in[2], false), axis12(in[3], true));
    return 64;
}

size_t dongle_switch_reply(const uint8_t *request, size_t size,
                          const uint8_t in[9], uint8_t counter,
                          const uint8_t address[6], uint8_t out[64])
{
    if (!request || !size || !in || !address || !out) return 0;
    if (request[0] == 0x80 && size >= 2) {
        if (request[1] < 1 || request[1] > 5) return 0;
        memset(out, 0, 64); out[0] = 0x81; out[1] = request[1];
        if (request[1] == 1) { out[3] = 3; memcpy(out + 4, address, 6); }
        return 64;
    }
    if (request[0] != 1 || size < 11) return 0;
    dongle_output_encode(DONGLE_PROFILE_SWITCH, in, counter, out);
    out[0] = 0x21; out[13] = 0x80; out[14] = request[10];
    switch (request[10]) {
    case 2:
        out[13] = 0x82; out[15] = 3; out[16] = 0x48; out[17] = 3;
        out[18] = 2; memcpy(out + 19, address, 6); out[25] = 1; out[26] = 1;
        break;
    case 0x10: {
        if (size < 16 || request[15] > 29) { out[13] = 0; break; }
        uint32_t base = (uint32_t)request[11] | (uint32_t)request[12] << 8 |
                        (uint32_t)request[13] << 16 | (uint32_t)request[14] << 24;
        out[13] = 0x90; memcpy(out + 15, request + 11, 5);
        memset(out + 20, 0xff, request[15]);
        uint8_t calibration[18];
        stick12(calibration, 2047, 2047); stick12(calibration + 3, 2048, 2048);
        stick12(calibration + 6, 2048, 2048); stick12(calibration + 9, 2048, 2048);
        stick12(calibration + 12, 2048, 2048); stick12(calibration + 15, 2047, 2047);
        for (unsigned i = 0; i < request[15]; ++i) {
            uint32_t a = base + i;
            if (a >= 0x603d && a < 0x604f) out[20 + i] = calibration[a - 0x603d];
            /* Stick dead zone parameters: 15%, range 100%. */
            if ((a >= 0x6086 && a < 0x6098) || (a >= 0x6098 && a < 0x60aa)) {
                const uint8_t params[18] = {0x0f,0x30,0x61,0xae,0x90,0xd9,0xd4,0x14,0x54,0x41,0x15,0x54,0xc7,0x79,0x9c,0x33,0x36,0x63};
                out[20 + i] = params[(a - 0x6086) % 18];
            }
        }
        break;
    }
    case 3: case 0x30: case 0x38: case 0x40: case 0x41: case 0x48:
        if (size < 12) out[13] = 0;
        break;
    default: out[13] = 0; break;
    }
    return 64;
}
