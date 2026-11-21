#include "dongle/dualsense.h"
#include <string.h>

static uint32_t crc_byte(uint32_t crc, uint8_t byte)
{
    static const uint32_t table[16] = {
        0x00000000U, 0x1db71064U, 0x3b6e20c8U, 0x26d930acU,
        0x76dc4190U, 0x6b6b51f4U, 0x4db26158U, 0x5005713cU,
        0xedb88320U, 0xf00f9344U, 0xd6d6a3e8U, 0xcb61b38cU,
        0x9b64c2b0U, 0x86d3d2d4U, 0xa00ae278U, 0xbdbdf21cU,
    };
    crc ^= byte;
    crc = (crc >> 4U) ^ table[crc & 0x0fU];
    crc = (crc >> 4U) ^ table[crc & 0x0fU];
    return crc;
}

static uint32_t ds_crc32(uint8_t seed, const uint8_t *data, size_t len)
{
    uint32_t crc = crc_byte(UINT32_MAX, seed);
    for (size_t i = 0; i < len; ++i) crc = crc_byte(crc, data[i]);
    return ~crc;
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static void write_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
    p[2] = (uint8_t)(value >> 16U);
    p[3] = (uint8_t)(value >> 24U);
}

bool dongle_dualsense_input(const uint8_t *data, size_t size, uint8_t output[9])
{
    if (!data || !output || !size) return false;
    const uint8_t *p;
    uint8_t buttons0, buttons1, buttons2;
    if (data[0] == 0x31 && size == 78) {
        if (ds_crc32(0xa1U, data, 74U) != read_le32(data + 74)) return false;
        p = data + 2;
        buttons0 = p[7]; buttons1 = p[8]; buttons2 = p[9];
        output[4] = p[4]; output[5] = p[5];
    } else if (data[0] == 0x01 && size == 10) {
        /* Basic Bluetooth report, before requesting feature 0x05. */
        p = data + 1;
        buttons0 = p[4]; buttons1 = p[5]; buttons2 = p[6];
        output[4] = p[7]; output[5] = p[8];
    } else return false;
    if ((buttons0 & 15) > 8) return false;
    for (unsigned i = 0; i < 4; ++i) output[i] = p[i];
    output[6] = (buttons0 >> 4) | (uint8_t)(buttons1 << 4);
    output[7] = (buttons1 >> 4) | (uint8_t)((buttons2 & 7) << 4);
    output[8] = buttons0 & 15;
    return true;
}

bool dongle_dualsense_bt_to_usb_input(const uint8_t *data, size_t size,
                                      uint8_t usb_out[64])
{
    if (!data || !usb_out || !size) return false;
    if (data[0] == 0x31U && size == 78U) {
        if (ds_crc32(0xa1U, data, 74U) != read_le32(data + 74)) return false;
        usb_out[0] = 0x01U;
        memcpy(usb_out + 1, data + 2, 63U);
        return true;
    }
    if (data[0] == 0x01U && size == 10U) {
        if ((data[5] & 15U) > 8U) return false;
        memset(usb_out, 0, 64U);
        usb_out[0] = 0x01U;
        memcpy(usb_out + 1, data + 1, 4U);
        usb_out[5] = data[8];
        usb_out[6] = data[9];
        usb_out[8] = data[5];
        usb_out[9] = data[6];
        usb_out[10] = data[7];
        usb_out[33] = 0x80U;
        usb_out[37] = 0x80U;
        usb_out[53] = 0x1aU;
        return true;
    }
    return false;
}

bool dongle_dualsense_usb_to_bt_output(const uint8_t *usb_report, size_t size,
                                       uint8_t seq, uint8_t bt_out[78])
{
    if (!usb_report || !bt_out || size < 48U || usb_report[0] != 0x02U) {
        return false;
    }
    memset(bt_out, 0, 78U);
    bt_out[0] = 0x31U;
    bt_out[1] = (uint8_t)((seq & 0x0fU) << 4U);
    bt_out[2] = 0x10U;
    memcpy(bt_out + 3, usb_report + 1, 47U);
    write_le32(bt_out + 74, ds_crc32(0xa2U, bt_out, 74U));
    return true;
}

size_t dongle_dualsense_feature_size(uint8_t report_id)
{
    switch (report_id) {
        case 0x05U: return 41U;
        case 0x08U: return 48U;
        case 0x09U: return 20U;
        case 0x20U:
        case 0x80U:
        case 0x81U: return 64U;
        default: return 0U;
    }
}

bool dongle_dualsense_bt_feature_to_usb(const uint8_t *bt_data, size_t bt_size,
                                      uint8_t *usb_out, size_t usb_cap,
                                      size_t *usb_len)
{
    if (!bt_data || !usb_out || bt_size < 6U) {
        return false;
    }
    const size_t expected = dongle_dualsense_feature_size(bt_data[0]);
    if (expected < 6U || bt_size < expected || usb_cap < expected) {
        return false;
    }
    const size_t payload = expected - 4U;
    if (ds_crc32(0xa3U, bt_data, payload) != read_le32(bt_data + payload)) {
        return false;
    }
    memcpy(usb_out, bt_data, payload);
    memset(usb_out + payload, 0, 4U);
    if (usb_len != NULL) {
        *usb_len = expected;
    }
    return true;
}

size_t dongle_dualsense_usb_to_bt_feature(const uint8_t *usb_report,
                                         size_t usb_size, uint8_t *bt_out,
                                         size_t bt_cap)
{
    if (!usb_report || !bt_out || usb_size == 0U) {
        return 0U;
    }
    const size_t expected = dongle_dualsense_feature_size(usb_report[0]);
    if (expected < 6U || bt_cap < expected) {
        return 0U;
    }
    const size_t payload = expected - 4U;
    if (usb_size < payload) {
        return 0U;
    }
    memset(bt_out, 0, expected);
    memcpy(bt_out, usb_report, payload);
    write_le32(bt_out + payload, ds_crc32(0x53U, bt_out, payload));
    return expected;
}

void dongle_dualsense_bt_unlock_leds(uint8_t seq, uint8_t bt_out[78])
{
    if (!bt_out) {
        return;
    }
    uint8_t usb[48] = { 0x02U };
    usb[39] = 0x02U; /* valid_flag2: LIGHTBAR_SETUP_CONTROL_ENABLE */
    usb[42] = 0x02U; /* lightbar_setup: LIGHT_OUT */
    (void)dongle_dualsense_usb_to_bt_output(usb, sizeof(usb), seq, bt_out);
}

void dongle_dualsense_merge_usb_output(uint8_t dst[48], const uint8_t src[48])
{
    if (!dst || !src || dst[0] != 0x02U || src[0] != 0x02U) {
        return;
    }
    if ((src[1] & 0x03U) != 0U || (src[39] & 0x04U) != 0U) {
        dst[3] = src[3];
        dst[4] = src[4];
        dst[1] = (uint8_t)((dst[1] & ~0x03U) | (src[1] & 0x03U));
        dst[39] = (uint8_t)((dst[39] & ~0x04U) | (src[39] & 0x04U));
    }
    if ((src[1] & 0x04U) != 0U) {
        memcpy(&dst[11], &src[11], 11U);
        dst[1] |= 0x04U;
    }
    if ((src[1] & 0x08U) != 0U) {
        memcpy(&dst[22], &src[22], 11U);
        dst[1] |= 0x08U;
    }
    static const struct {
        uint8_t flag_offset;
        uint8_t mask;
        uint8_t data_offset;
    } byte_fields[] = {
        { 1U, 0x10U,  5U },
        { 1U, 0x20U,  6U },
        { 1U, 0x40U,  7U },
        { 1U, 0x80U,  8U },
        { 2U, 0x01U,  9U },
        { 2U, 0x02U, 10U },
        { 2U, 0x10U, 44U },
        { 2U, 0x20U, 40U },
        { 2U, 0x40U, 37U },
        { 2U, 0x80U, 38U },
        { 39U, 0x01U, 43U },
        { 39U, 0x02U, 42U },
    };
    for (size_t i = 0U; i < sizeof(byte_fields) / sizeof(byte_fields[0]); ++i) {
        if ((src[byte_fields[i].flag_offset] & byte_fields[i].mask) != 0U) {
            dst[byte_fields[i].data_offset] = src[byte_fields[i].data_offset];
            dst[byte_fields[i].flag_offset] |= byte_fields[i].mask;
        }
    }
    if ((src[2] & 0x04U) != 0U) {
        memcpy(&dst[45], &src[45], 3U);
        dst[2] |= 0x04U;
    }
    if ((src[2] & 0x08U) != 0U) {
        dst[2] |= 0x08U;
    }
    memcpy(&dst[33], &src[33], 4U);
}
