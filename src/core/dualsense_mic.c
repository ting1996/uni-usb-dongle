#include "dongle/dualsense_mic.h"
#include "dongle/dualsense.h"

#include <string.h>

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

static void write_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
    p[2] = (uint8_t)(value >> 16U);
    p[3] = (uint8_t)(value >> 24U);
}

void dongle_dualsense_bt_mic_preamp_report(uint8_t seq, bool enable,
                                            bool headset_mic,
                                            uint8_t bt_out[78])
{
    if (bt_out == NULL) {
        return;
    }
    uint8_t usb[48] = { 0x02U };
    usb[1] = 0xC0U; /* MIC_VOLUME_ENABLE | AUDIO_CONTROL_ENABLE */
    usb[2] = 0x03U; /* MIC_MUTE_LED_CONTROL_ENABLE | AUDIO_MUTE_CONTROL_ENABLE */
    usb[7] = enable ? 0x20U : 0x00U;
    usb[8] = headset_mic ? 0x02U : 0x00U;
    usb[9] = 0x00U;
    usb[10] = enable ? 0x00U : 0x10U;
    (void)dongle_dualsense_usb_to_bt_output(usb, sizeof(usb), seq, bt_out);
}

void dongle_dualsense_bt_mic_control_report(
    uint8_t seq, bool enable, bool headset_mic,
    uint8_t bt_out[DONGLE_DUALSENSE_BT_MIC_REPORT_SIZE])
{
    (void)headset_mic;
    if (bt_out == NULL) {
        return;
    }
    memset(bt_out, 0, DONGLE_DUALSENSE_BT_MIC_REPORT_SIZE);
    bt_out[0] = 0x32U;
    bt_out[1] = (uint8_t)((seq & 0x0fU) << 4U);
    bt_out[2] = 0x91U; /* 0x11 | 0x80 audio session header tag */
    bt_out[3] = 0x01U; /* 1-byte payload */
    bt_out[4] = enable ? 0x03U : 0x02U;
    write_le32(bt_out + DONGLE_DUALSENSE_BT_MIC_REPORT_SIZE - 4U,
               dongle_dualsense_crc32(0xA2U, bt_out,
                                      DONGLE_DUALSENSE_BT_MIC_REPORT_SIZE - 4U));
}

bool dongle_dualsense_bt_extract_mic_opus(const uint8_t *hid_report, size_t size,
                                          const uint8_t **opus_frame,
                                          size_t *opus_len)
{
    if (hid_report == NULL || opus_frame == NULL || opus_len == NULL) {
        return false;
    }
    if (hid_report[0] == 0x31U && (hid_report[1] & 0x02U) != 0U &&
        size >= 3U + DONGLE_DUALSENSE_BT_MIC_OPUS_SIZE) {
        *opus_frame = &hid_report[3];
        *opus_len = DONGLE_DUALSENSE_BT_MIC_OPUS_SIZE;
        return true;
    }
    if (size < 82U || size > 547U ||
        hid_report[0] < 0x31U || hid_report[0] > 0x39U) {
        return false;
    }

    const size_t payload_end = size - 4U;
    if (dongle_dualsense_crc32(0xA1U, hid_report, payload_end) !=
        read_le32(hid_report + payload_end)) {
        return false;
    }

    size_t pos = 74U;
    while (pos + 2U < payload_end) {
        const uint8_t tag = hid_report[pos];
        const uint8_t sub_len = hid_report[pos + 1U];
        if (tag == 0U || sub_len == 0U || pos + 2U + sub_len > payload_end) {
            break;
        }
        if (tag == 0x82U || tag == 0x91U || tag == 0x93U) {
            *opus_frame = &hid_report[pos + 2U];
            *opus_len = sub_len;
            return true;
        }
        pos += (size_t)2U + sub_len;
    }
    return false;
}

size_t dongle_pcm_upsample_8k_to_16k(const int16_t *in, size_t in_samples,
                                    int16_t *out, size_t out_cap,
                                    int16_t *prev_sample)
{
    if (in == NULL || out == NULL || prev_sample == NULL || in_samples == 0U ||
        out_cap < in_samples * 2U) {
        return 0U;
    }

    int16_t prev = *prev_sample;
    size_t written = 0U;
    for (size_t i = 0U; i < in_samples; ++i) {
        const int16_t cur = in[i];
        const int32_t mid = ((int32_t)prev + (int32_t)cur) / 2;
        out[written++] = (int16_t)mid;
        out[written++] = cur;
        prev = cur;
    }
    *prev_sample = prev;
    return written;
}
