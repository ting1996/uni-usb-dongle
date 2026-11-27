#include "dongle/dualsense.h"
#include <string.h>

#ifdef BL616
#define DONGLE_TCM_CODE __attribute__((section(".tcm_code")))
#else
#define DONGLE_TCM_CODE
#endif

static const uint32_t crc32_table[256] = {
    0x00000000U, 0x77073096U, 0xee0e612cU, 0x990951baU,
    0x076dc419U, 0x706af48fU, 0xe963a535U, 0x9e6495a3U,
    0x0edb8832U, 0x79dcb8a4U, 0xe0d5e91eU, 0x97d2d988U,
    0x09b64c2bU, 0x7eb17cbdU, 0xe7b82d07U, 0x90bf1d91U,
    0x1db71064U, 0x6ab020f2U, 0xf3b97148U, 0x84be41deU,
    0x1adad47dU, 0x6ddde4ebU, 0xf4d4b551U, 0x83d385c7U,
    0x136c9856U, 0x646ba8c0U, 0xfd62f97aU, 0x8a65c9ecU,
    0x14015c4fU, 0x63066cd9U, 0xfa0f3d63U, 0x8d080df5U,
    0x3b6e20c8U, 0x4c69105eU, 0xd56041e4U, 0xa2677172U,
    0x3c03e4d1U, 0x4b04d447U, 0xd20d85fdU, 0xa50ab56bU,
    0x35b5a8faU, 0x42b2986cU, 0xdbbbc9d6U, 0xacbcf940U,
    0x32d86ce3U, 0x45df5c75U, 0xdcd60dcfU, 0xabd13d59U,
    0x26d930acU, 0x51de003aU, 0xc8d75180U, 0xbfd06116U,
    0x21b4f4b5U, 0x56b3c423U, 0xcfba9599U, 0xb8bda50fU,
    0x2802b89eU, 0x5f058808U, 0xc60cd9b2U, 0xb10be924U,
    0x2f6f7c87U, 0x58684c11U, 0xc1611dabU, 0xb6662d3dU,
    0x76dc4190U, 0x01db7106U, 0x98d220bcU, 0xefd5102aU,
    0x71b18589U, 0x06b6b51fU, 0x9fbfe4a5U, 0xe8b8d433U,
    0x7807c9a2U, 0x0f00f934U, 0x9609a88eU, 0xe10e9818U,
    0x7f6a0dbbU, 0x086d3d2dU, 0x91646c97U, 0xe6635c01U,
    0x6b6b51f4U, 0x1c6c6162U, 0x856530d8U, 0xf262004eU,
    0x6c0695edU, 0x1b01a57bU, 0x8208f4c1U, 0xf50fc457U,
    0x65b0d9c6U, 0x12b7e950U, 0x8bbeb8eaU, 0xfcb9887cU,
    0x62dd1ddfU, 0x15da2d49U, 0x8cd37cf3U, 0xfbd44c65U,
    0x4db26158U, 0x3ab551ceU, 0xa3bc0074U, 0xd4bb30e2U,
    0x4adfa541U, 0x3dd895d7U, 0xa4d1c46dU, 0xd3d6f4fbU,
    0x4369e96aU, 0x346ed9fcU, 0xad678846U, 0xda60b8d0U,
    0x44042d73U, 0x33031de5U, 0xaa0a4c5fU, 0xdd0d7cc9U,
    0x5005713cU, 0x270241aaU, 0xbe0b1010U, 0xc90c2086U,
    0x5768b525U, 0x206f85b3U, 0xb966d409U, 0xce61e49fU,
    0x5edef90eU, 0x29d9c998U, 0xb0d09822U, 0xc7d7a8b4U,
    0x59b33d17U, 0x2eb40d81U, 0xb7bd5c3bU, 0xc0ba6cadU,
    0xedb88320U, 0x9abfb3b6U, 0x03b6e20cU, 0x74b1d29aU,
    0xead54739U, 0x9dd277afU, 0x04db2615U, 0x73dc1683U,
    0xe3630b12U, 0x94643b84U, 0x0d6d6a3eU, 0x7a6a5aa8U,
    0xe40ecf0bU, 0x9309ff9dU, 0x0a00ae27U, 0x7d079eb1U,
    0xf00f9344U, 0x8708a3d2U, 0x1e01f268U, 0x6906c2feU,
    0xf762575dU, 0x806567cbU, 0x196c3671U, 0x6e6b06e7U,
    0xfed41b76U, 0x89d32be0U, 0x10da7a5aU, 0x67dd4accU,
    0xf9b9df6fU, 0x8ebeeff9U, 0x17b7be43U, 0x60b08ed5U,
    0xd6d6a3e8U, 0xa1d1937eU, 0x38d8c2c4U, 0x4fdff252U,
    0xd1bb67f1U, 0xa6bc5767U, 0x3fb506ddU, 0x48b2364bU,
    0xd80d2bdaU, 0xaf0a1b4cU, 0x36034af6U, 0x41047a60U,
    0xdf60efc3U, 0xa867df55U, 0x316e8eefU, 0x4669be79U,
    0xcb61b38cU, 0xbc66831aU, 0x256fd2a0U, 0x5268e236U,
    0xcc0c7795U, 0xbb0b4703U, 0x220216b9U, 0x5505262fU,
    0xc5ba3bbeU, 0xb2bd0b28U, 0x2bb45a92U, 0x5cb36a04U,
    0xc2d7ffa7U, 0xb5d0cf31U, 0x2cd99e8bU, 0x5bdeae1dU,
    0x9b64c2b0U, 0xec63f226U, 0x756aa39cU, 0x026d930aU,
    0x9c0906a9U, 0xeb0e363fU, 0x72076785U, 0x05005713U,
    0x95bf4a82U, 0xe2b87a14U, 0x7bb12baeU, 0x0cb61b38U,
    0x92d28e9bU, 0xe5d5be0dU, 0x7cdcefb7U, 0x0bdbdf21U,
    0x86d3d2d4U, 0xf1d4e242U, 0x68ddb3f8U, 0x1fda836eU,
    0x81be16cdU, 0xf6b9265bU, 0x6fb077e1U, 0x18b74777U,
    0x88085ae6U, 0xff0f6a70U, 0x66063bcaU, 0x11010b5cU,
    0x8f659effU, 0xf862ae69U, 0x616bffd3U, 0x166ccf45U,
    0xa00ae278U, 0xd70dd2eeU, 0x4e048354U, 0x3903b3c2U,
    0xa7672661U, 0xd06016f7U, 0x4969474dU, 0x3e6e77dbU,
    0xaed16a4aU, 0xd9d65adcU, 0x40df0b66U, 0x37d83bf0U,
    0xa9bcae53U, 0xdebb9ec5U, 0x47b2cf7fU, 0x30b5ffe9U,
    0xbdbdf21cU, 0xcabac28aU, 0x53b39330U, 0x24b4a3a6U,
    0xbad03605U, 0xcdd70693U, 0x54de5729U, 0x23d967bfU,
    0xb3667a2eU, 0xc4614ab8U, 0x5d681b02U, 0x2a6f2b94U,
    0xb40bbe37U, 0xc30c8ea1U, 0x5a05df1bU, 0x2d02ef8dU,
};

DONGLE_TCM_CODE uint32_t dongle_dualsense_crc32(uint8_t seed, const uint8_t *data, size_t len)
{
    uint32_t crc = UINT32_MAX;
    crc = (crc >> 8U) ^ crc32_table[(crc ^ seed) & 0xffU];
    for (size_t i = 0U; i < len; ++i) {
        crc = (crc >> 8U) ^ crc32_table[(crc ^ data[i]) & 0xffU];
    }
    return ~crc;
}

static uint32_t ds_crc32(uint8_t seed, const uint8_t *data, size_t len)
{
    return dongle_dualsense_crc32(seed, data, len);
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

DONGLE_TCM_CODE bool dongle_dualsense_input(const uint8_t *data, size_t size, uint8_t output[9])
{
    if (!data || !output || !size) return false;
    const uint8_t *p;
    uint8_t buttons0, buttons1, buttons2;
    if (data[0] == 0x31 && (data[1] & 0x02U) == 0U && size == 78) {
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

DONGLE_TCM_CODE bool dongle_dualsense_bt_to_usb_input(const uint8_t *data, size_t size,
                                                      uint8_t usb_out[64])
{
    if (!data || !usb_out || !size) return false;
    if (data[0] == 0x31U && (data[1] & 0x02U) == 0U && size == 78U) {
        if (ds_crc32(0xa1U, data, 74U) != read_le32(data + 74)) return false;
        usb_out[0] = 0x01U;
        memcpy(usb_out + 1, data + 2, 63U);
        usb_out[54] |= 0x08U;
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
        usb_out[54] = 0x08U;
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
        case 0x22U:
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
    usb[2] = 0x40U;  /* valid_flag1: POWER_REDUCTION */
    usb[37] = 0x00U; /* motor/trigger power reduction: 0% attenuation */
    usb[39] = 0x03U; /* valid_flag2: LIGHTBAR_BRIGHTNESS_CONTROL_ENABLE | LIGHTBAR_SETUP_CONTROL_ENABLE */
    usb[42] = 0x02U; /* lightbar_setup: LIGHT_OUT */
    (void)dongle_dualsense_usb_to_bt_output(usb, sizeof(usb), seq, bt_out);
}

void dongle_dualsense_normalize_usb_output(uint8_t usb_report[48],
                                           uint16_t fw_update_ver)
{
    if (!usb_report || usb_report[0] != 0x02U) {
        return;
    }
    const bool has_rumble_flag =
        (usb_report[1] & 0x03U) != 0U || (usb_report[39] & 0x04U) != 0U;
    const bool has_rumble_motors = usb_report[3] != 0U || usb_report[4] != 0U;
    if (has_rumble_flag || has_rumble_motors) {
        usb_report[1] |= 0x02U; /* HAPTICS_SELECT */
        if (fw_update_ver < 0x0224U) {
            usb_report[1] |= 0x03U; /* Legacy COMPATIBLE_VIBRATION + HAPTICS_SELECT */
            usb_report[39] &= (uint8_t)(~0x04U & 0xFFU);
        } else if ((usb_report[1] & 0x01U) == 0U &&
                   (usb_report[39] & 0x04U) == 0U) {
            usb_report[39] |= 0x04U; /* COMPATIBLE_VIBRATION2 */
        }
    }
}

bool dongle_dualsense_from_xinput_output(const uint8_t *xinput, size_t size,
                                         uint8_t ds_usb[48])
{
    if (!xinput || !ds_usb || size < 8U ||
        xinput[0] != 0x00U || xinput[1] != 0x08U) {
        return false;
    }
    memset(ds_usb, 0, 48U);
    ds_usb[0] = 0x02U;
    ds_usb[1] = 0x02U;  /* HAPTICS_SELECT */
    ds_usb[39] = 0x04U; /* COMPATIBLE_VIBRATION2 */
    ds_usb[3] = xinput[4]; /* Weak / right motor */
    ds_usb[4] = xinput[3]; /* Strong / left motor */
    return true;
}

bool dongle_dualsense_from_ps4_output(const uint8_t *ps4, size_t size,
                                      uint8_t ds_usb[48])
{
    if (!ps4 || !ds_usb || size < 9U || ps4[0] != 0x05U) {
        return false;
    }
    const bool has_rumble = (ps4[1] & 0x01U) != 0U;
    const bool has_led = (ps4[1] & 0x02U) != 0U;
    if (!has_rumble && !has_led) {
        return false;
    }
    memset(ds_usb, 0, 48U);
    ds_usb[0] = 0x02U;
    if (has_rumble) {
        ds_usb[1] = 0x02U;
        ds_usb[39] = 0x04U;
        ds_usb[3] = ps4[4];
        ds_usb[4] = ps4[5];
    }
    if (has_led) {
        ds_usb[2] = 0x04U;
        ds_usb[45] = ps4[6];
        ds_usb[46] = ps4[7];
        ds_usb[47] = ps4[8];
    }
    return true;
}

static uint8_t switch_rumble_side_amplitude(const uint8_t b[4])
{
    if ((b[0] == 0U && b[1] == 0U && b[2] == 0U && b[3] == 0U) ||
        (b[0] == 0U && (b[1] & 0xFEU) == 0U && (b[2] & 0x7FU) == 0x40U &&
         b[3] == 0x40U)) {
        return 0U;
    }
    const uint32_t hf_raw = (uint32_t)(b[1] & 0xFEU);
    uint32_t hf = hf_raw >= 0xC8U ? 255U : (hf_raw * 255U) / 0xC8U;
    uint32_t lf = 0U;
    if (b[3] >= 0x40U) {
        const uint32_t lf_steps =
            ((uint32_t)(b[3] - 0x40U) * 2U) + ((b[2] & 0x80U) != 0U ? 1U : 0U);
        lf = lf_steps >= 100U ? 255U : (lf_steps * 255U) / 100U;
    }
    return (uint8_t)(hf > lf ? hf : lf);
}

bool dongle_dualsense_from_switch_rumble(const uint8_t *sw, size_t size,
                                         uint8_t ds_usb[48])
{
    if (!sw || !ds_usb || size < 10U || (sw[0] != 0x01U && sw[0] != 0x10U)) {
        return false;
    }
    memset(ds_usb, 0, 48U);
    ds_usb[0] = 0x02U;
    ds_usb[1] = 0x02U;
    ds_usb[39] = 0x04U;
    ds_usb[4] = switch_rumble_side_amplitude(&sw[2]); /* Left */
    ds_usb[3] = switch_rumble_side_amplitude(&sw[6]); /* Right */
    return true;
}

void dongle_dualsense_merge_usb_output(uint8_t dst[48], const uint8_t src[48])
{
    if (!dst || !src || dst[0] != 0x02U || src[0] != 0x02U) {
        return;
    }
    if ((src[1] & 0x03U) != 0U || (src[39] & 0x04U) != 0U ||
        src[3] != 0U || src[4] != 0U) {
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

void dongle_dualsense_merge_usb_output_coalesced(uint8_t dst[48],
                                                 const uint8_t src[48],
                                                 bool *defer_rumble_stop)
{
    if (!dst || !src || dst[0] != 0x02U || src[0] != 0x02U) {
        return;
    }
    const bool dst_nonzero = dst[3] != 0U || dst[4] != 0U;
    const uint8_t saved_r = dst[3];
    const uint8_t saved_l = dst[4];
    const uint8_t saved_f0 = (uint8_t)(dst[1] & 0x03U);
    const uint8_t saved_f2 = (uint8_t)(dst[39] & 0x04U);
    const bool src_nonzero = src[3] != 0U || src[4] != 0U;
    const bool src_has_rumble = (src[1] & 0x03U) != 0U ||
                                (src[39] & 0x04U) != 0U || src_nonzero;
    const bool src_is_zero_stop = !src_nonzero &&
        src[1] == 0U && src[2] == 0U && src[39] == 0U;
    dongle_dualsense_merge_usb_output(dst, src);
    if (dst_nonzero && (src_has_rumble || src_is_zero_stop) && !src_nonzero) {
        dst[3] = saved_r;
        dst[4] = saved_l;
        if ((dst[1] & 0x03U) == 0U && (dst[39] & 0x04U) == 0U) {
            dst[1] |= (uint8_t)(saved_f0 != 0U ? saved_f0 : 0x02U);
            dst[39] |= saved_f2;
        }
        if (defer_rumble_stop != NULL) {
            *defer_rumble_stop = true;
        }
    } else if (src_nonzero && defer_rumble_stop != NULL) {
        *defer_rumble_stop = false;
    }
}
