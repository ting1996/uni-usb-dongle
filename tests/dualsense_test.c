#include "dongle/dualsense.h"
#include "dongle/dualsense_mic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static void sign_report(uint8_t seed, uint8_t data[78])
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < 75; ++i) {
        crc ^= i ? data[i-1] : seed;
        for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
    }
    crc = ~crc;
    for (unsigned i=0; i<4; ++i) data[74+i] = (uint8_t)(crc >> (8*i));
}
int main(void)
{
    uint8_t out[9];
    const uint8_t basic[10] = {1, 10,20,30,40,0x28,0x81,0x03,50,60};
    const uint8_t expected[9] = {10,20,30,40,50,60,0x12,0x38,8};
    CHECK(dongle_dualsense_input(basic, 10, out));
    CHECK(!memcmp(out, expected, 9));
    uint8_t full[78] = {0x31,0};
    memcpy(full+2, basic+1, 4); full[6]=50; full[7]=60;
    full[9]=0x28; full[10]=0x81; full[11]=3;
    full[16]=0x12; full[17]=0x34; /* IMU gyro sample preserved in passthrough */
    full[34]=0x80; full[38]=0x80; /* Touch contacts */
    full[54]=0x1a; /* Battery */
    sign_report(0xa1U, full);
    CHECK(dongle_dualsense_input(full,78,out));
    CHECK(!memcmp(out,expected,9));

    uint8_t ds_usb[64] = {0};
    CHECK(dongle_dualsense_bt_to_usb_input(full, 78, ds_usb));
    CHECK(ds_usb[0] == 0x01U && ds_usb[15] == 0x12U && ds_usb[16] == 0x34U &&
          ds_usb[33] == 0x80U && ds_usb[53] == 0x1aU);
    full[20] ^= 1; CHECK(!dongle_dualsense_input(full,78,out));
    CHECK(!dongle_dualsense_bt_to_usb_input(full, 78, ds_usb));
    full[20] ^= 1; full[9] = 0x2f; sign_report(0xa1U, full);
    CHECK(!dongle_dualsense_input(full,78,out));
    CHECK(!dongle_dualsense_input(full,77,out));
    CHECK(!dongle_dualsense_input(NULL,0,out));
    CHECK(!dongle_dualsense_input(basic,10,NULL));

    memset(ds_usb, 0, sizeof(ds_usb));
    CHECK(dongle_dualsense_bt_to_usb_input(basic, 10, ds_usb));
    CHECK(ds_usb[0] == 0x01U && ds_usb[1] == 10U && ds_usb[5] == 50U &&
          ds_usb[6] == 60U && ds_usb[8] == 0x28U && ds_usb[9] == 0x81U &&
          ds_usb[10] == 0x03U && ds_usb[33] == 0x80U && ds_usb[37] == 0x80U &&
          ds_usb[53] == 0x1aU);

    uint8_t usb_out[48] = {0x02U, 0x03U, 0x04U, 0x40U, 0x80U};
    usb_out[45] = 0xffU; usb_out[46] = 0x10U; usb_out[47] = 0x20U;
    uint8_t bt_out[78] = {0};
    CHECK(dongle_dualsense_usb_to_bt_output(usb_out, sizeof(usb_out), 0x15U, bt_out));
    CHECK(bt_out[0] == 0x31U && bt_out[1] == 0x50U && bt_out[2] == 0x10U &&
          memcmp(bt_out + 3, usb_out + 1, 47U) == 0);
    uint8_t expected_bt[78];
    memcpy(expected_bt, bt_out, sizeof(bt_out));
    sign_report(0xa2U, expected_bt);
    CHECK(memcmp(bt_out + 74, expected_bt + 74, 4U) == 0);
    CHECK(!dongle_dualsense_usb_to_bt_output(usb_out, 47U, 0U, bt_out));
    usb_out[0] = 0x01U;
    CHECK(!dongle_dualsense_usb_to_bt_output(usb_out, 48U, 0U, bt_out));

    /* Feature report sizes, BT(0xA3)->USB and USB->BT(0x53) CRC helpers. */
    CHECK(dongle_dualsense_feature_size(0x05U) == 41U);
    CHECK(dongle_dualsense_feature_size(0x08U) == 48U);
    CHECK(dongle_dualsense_feature_size(0x09U) == 20U);
    CHECK(dongle_dualsense_feature_size(0x20U) == 64U);
    CHECK(dongle_dualsense_feature_size(0x80U) == 64U);
    CHECK(dongle_dualsense_feature_size(0x81U) == 64U);
    CHECK(dongle_dualsense_feature_size(0x00U) == 0U);
    CHECK(dongle_dualsense_feature_size(0x01U) == 0U);
    CHECK(dongle_dualsense_feature_size(0x99U) == 0U);

    static const struct { uint8_t id; size_t size; } feat_cases[] = {
        { 0x05U, 41U }, { 0x08U, 48U }, { 0x09U, 20U }, { 0x20U, 64U }, { 0x81U, 64U }
    };
    for (size_t c = 0; c < sizeof(feat_cases) / sizeof(feat_cases[0]); ++c) {
        const uint8_t id = feat_cases[c].id;
        const size_t fsize = feat_cases[c].size;
        uint8_t bt_feat[64] = { 0 };
        bt_feat[0] = id;
        for (size_t k = 1; k < fsize - 4U; ++k) {
            bt_feat[k] = (uint8_t)((id ^ (uint8_t)k) + 0x11U);
        }
        uint32_t crc = UINT32_MAX;
        uint8_t seed = 0xa3U;
        crc ^= seed;
        for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
        for (size_t i = 0; i < fsize - 4U; ++i) {
            crc ^= bt_feat[i];
            for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
        }
        crc = ~crc;
        for (unsigned i = 0; i < 4; ++i) bt_feat[fsize - 4U + i] = (uint8_t)(crc >> (8U * i));

        uint8_t usb_feat[64];
        memset(usb_feat, 0xaa, sizeof(usb_feat));
        size_t out_len = 0U;
        CHECK(dongle_dualsense_bt_feature_to_usb(bt_feat, fsize, usb_feat, sizeof(usb_feat), &out_len));
        CHECK(out_len == fsize);
        CHECK(memcmp(usb_feat, bt_feat, fsize - 4U) == 0);
        CHECK(usb_feat[fsize - 4U] == 0U && usb_feat[fsize - 3U] == 0U &&
              usb_feat[fsize - 2U] == 0U && usb_feat[fsize - 1U] == 0U);

        bt_feat[1] ^= 0xffU;
        CHECK(!dongle_dualsense_bt_feature_to_usb(bt_feat, fsize, usb_feat, sizeof(usb_feat), &out_len));
        bt_feat[1] ^= 0xffU;
        CHECK(!dongle_dualsense_bt_feature_to_usb(bt_feat, fsize - 1U, usb_feat, sizeof(usb_feat), &out_len));
        CHECK(!dongle_dualsense_bt_feature_to_usb(bt_feat, fsize, usb_feat, fsize - 1U, &out_len));
    }
    uint8_t bad_id_feat[64] = { 0x99U };
    uint8_t usb_feat_out[64] = { 0 };
    CHECK(!dongle_dualsense_bt_feature_to_usb(bad_id_feat, sizeof(bad_id_feat), usb_feat_out, sizeof(usb_feat_out), NULL));

    /* USB SET_FEATURE -> BT 0x53 CRC-32 for 0x80 (64B and 60B) and 0x08 (48B). */
    uint8_t usb_set_80[64] = { 0x80U, 0x01U, 0x13U };
    usb_set_80[59] = 0x55U;
    uint8_t bt_set[64] = { 0 };
    CHECK(dongle_dualsense_usb_to_bt_feature(usb_set_80, 64U, bt_set, sizeof(bt_set)) == 64U);
    CHECK(bt_set[0] == 0x80U && bt_set[1] == 0x01U && bt_set[2] == 0x13U && bt_set[59] == 0x55U);
    {
        uint32_t crc = UINT32_MAX;
        uint8_t seed = 0x53U;
        crc ^= seed;
        for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
        for (size_t i = 0; i < 60U; ++i) {
            crc ^= bt_set[i];
            for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
        }
        crc = ~crc;
        for (unsigned i = 0; i < 4; ++i) CHECK(bt_set[60U + i] == (uint8_t)(crc >> (8U * i)));
    }
    memset(bt_set, 0, sizeof(bt_set));
    CHECK(dongle_dualsense_usb_to_bt_feature(usb_set_80, 60U, bt_set, sizeof(bt_set)) == 64U);
    CHECK(!dongle_dualsense_usb_to_bt_feature(usb_set_80, 59U, bt_set, sizeof(bt_set)));

    uint8_t usb_set_08[48] = { 0x08U, 0x02U, 0x03U };
    CHECK(dongle_dualsense_usb_to_bt_feature(usb_set_08, sizeof(usb_set_08), bt_set, sizeof(bt_set)) == 48U);
    {
        uint32_t crc = UINT32_MAX;
        uint8_t seed = 0x53U;
        crc ^= seed;
        for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
        for (size_t i = 0; i < 44U; ++i) {
            crc ^= bt_set[i];
            for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
        }
        crc = ~crc;
        for (unsigned i = 0; i < 4; ++i) CHECK(bt_set[44U + i] == (uint8_t)(crc >> (8U * i)));
    }
    CHECK(!dongle_dualsense_usb_to_bt_feature(bad_id_feat, sizeof(bad_id_feat), bt_set, sizeof(bt_set)));

    /* LED + power reduction unlock BT 0x31 report. */
    uint8_t unlock_bt[78] = { 0 };
    dongle_dualsense_bt_unlock_leds(0x27U, unlock_bt);
    CHECK(unlock_bt[0] == 0x31U && unlock_bt[1] == 0x70U && unlock_bt[2] == 0x10U &&
          unlock_bt[4] == 0x40U && unlock_bt[39] == 0x00U &&
          unlock_bt[41] == 0x02U && unlock_bt[44] == 0x02U);
    memcpy(expected_bt, unlock_bt, sizeof(unlock_bt));
    sign_report(0xa2U, expected_bt);
    CHECK(memcmp(unlock_bt + 74, expected_bt + 74, 4U) == 0);

    /* Normalize BT rumble flags for old (<0x0224) and modern (>=0x0224) DualSense FW. */
    uint8_t norm_old[48] = { 0x02U, 0x00U, 0x00U, 0x80U, 0xffU };
    norm_old[39] = 0x04U; /* COMPATIBLE_VIBRATION2 only */
    dongle_dualsense_normalize_usb_output(norm_old, 0x0210U);
    CHECK(norm_old[1] == 0x03U && norm_old[39] == 0x00U);

    uint8_t norm_new[48] = { 0x02U, 0x01U, 0x00U, 0x50U, 0x60U };
    dongle_dualsense_normalize_usb_output(norm_new, 0x0402U);
    CHECK(norm_new[1] == 0x03U && norm_new[39] == 0x00U);

    uint8_t norm_v2[48] = { 0x02U, 0x00U, 0x00U, 0x50U, 0x60U };
    norm_v2[39] = 0x04U;
    dongle_dualsense_normalize_usb_output(norm_v2, 0x0402U);
    CHECK(norm_v2[1] == 0x02U && norm_v2[39] == 0x04U);

    /* Cross-profile rumble translators (XInput, PS4, Switch Pro). */
    const uint8_t xi[8] = { 0x00U, 0x08U, 0x00U, 0xC0U, 0x40U, 0U, 0U, 0U };
    uint8_t ds_conv[48] = { 0 };
    CHECK(dongle_dualsense_from_xinput_output(xi, sizeof(xi), ds_conv));
    CHECK(ds_conv[0] == 0x02U && ds_conv[1] == 0x02U && ds_conv[39] == 0x04U &&
          ds_conv[3] == 0x40U && ds_conv[4] == 0xC0U);

    const uint8_t ps4_out[10] = { 0x05U, 0x03U, 0U, 0U, 0x22U, 0x88U, 0x10U, 0x20U, 0x30U, 0U };
    CHECK(dongle_dualsense_from_ps4_output(ps4_out, sizeof(ps4_out), ds_conv));
    CHECK(ds_conv[1] == 0x02U && ds_conv[2] == 0x04U && ds_conv[39] == 0x04U &&
          ds_conv[3] == 0x22U && ds_conv[4] == 0x88U &&
          ds_conv[45] == 0x10U && ds_conv[46] == 0x20U && ds_conv[47] == 0x30U);

    const uint8_t sw_neutral[10] = { 0x10U, 0x01U, 0x00U, 0x01U, 0x40U, 0x40U, 0x00U, 0x01U, 0x40U, 0x40U };
    CHECK(dongle_dualsense_from_switch_rumble(sw_neutral, sizeof(sw_neutral), ds_conv));
    CHECK(ds_conv[3] == 0U && ds_conv[4] == 0U);
    const uint8_t sw_active[10] = { 0x10U, 0x02U, 0x00U, 0xC8U, 0x40U, 0x40U, 0x00U, 0x01U, 0x40U, 0x72U };
    CHECK(dongle_dualsense_from_switch_rumble(sw_active, sizeof(sw_active), ds_conv));
    CHECK(ds_conv[4] == 255U && ds_conv[3] == 255U);

    /* DualSense mic preamp/control reports, 0x31 bit-1 & TLV extraction & upsampler. */
    uint8_t preamp_bt[78] = { 0 };
    dongle_dualsense_bt_mic_preamp_report(0x03U, true, false, preamp_bt);
    CHECK(preamp_bt[0] == 0x31U && preamp_bt[1] == 0x30U && preamp_bt[2] == 0x10U &&
          preamp_bt[3] == 0xC0U && preamp_bt[4] == 0x03U && preamp_bt[9] == 0x20U &&
          preamp_bt[12] == 0x00U);
    memcpy(expected_bt, preamp_bt, sizeof(preamp_bt));
    sign_report(0xa2U, expected_bt);
    CHECK(memcmp(preamp_bt + 74, expected_bt + 74, 4U) == 0);
    dongle_dualsense_bt_mic_preamp_report(0x04U, false, false, preamp_bt);
    CHECK(preamp_bt[9] == 0x00U && preamp_bt[12] == 0x10U);

    uint8_t mic_ctrl[DONGLE_DUALSENSE_BT_MIC_REPORT_SIZE] = { 0 };
    dongle_dualsense_bt_mic_control_report(0x05U, true, false, mic_ctrl);
    CHECK(mic_ctrl[0] == 0x32U && mic_ctrl[1] == 0x50U && mic_ctrl[2] == 0x91U &&
          mic_ctrl[3] == 0x01U && mic_ctrl[4] == 0x03U);
    {
        const uint32_t crc = dongle_dualsense_crc32(0xA2U, mic_ctrl, 138U);
        CHECK(mic_ctrl[138] == (uint8_t)crc &&
              mic_ctrl[139] == (uint8_t)(crc >> 8U) &&
              mic_ctrl[140] == (uint8_t)(crc >> 16U) &&
              mic_ctrl[141] == (uint8_t)(crc >> 24U));
    }
    dongle_dualsense_bt_mic_control_report(0x06U, false, false, mic_ctrl);
    CHECK(mic_ctrl[1] == 0x60U && mic_ctrl[2] == 0x91U &&
          mic_ctrl[3] == 0x01U && mic_ctrl[4] == 0x02U);

    uint8_t mic_in[78] = { 0 };
    mic_in[0] = 0x31U;
    mic_in[1] = 0x12U; /* seq=1, bit 1 set -> mic Opus packet */
    mic_in[3] = 0x5AU;
    mic_in[3U + DONGLE_DUALSENSE_BT_MIC_OPUS_SIZE - 1U] = 0xC3U;
    sign_report(0xA1U, mic_in);
    const uint8_t *opus_ptr = NULL;
    size_t opus_len = 0U;
    CHECK(dongle_dualsense_bt_extract_mic_opus(mic_in, sizeof(mic_in), &opus_ptr, &opus_len));
    CHECK(opus_len == DONGLE_DUALSENSE_BT_MIC_OPUS_SIZE && opus_ptr == &mic_in[3] &&
          opus_ptr[0] == 0x5AU && opus_ptr[70] == 0xC3U);
    CHECK(!dongle_dualsense_input(mic_in, sizeof(mic_in), out));
    CHECK(!dongle_dualsense_bt_to_usb_input(mic_in, sizeof(mic_in), ds_usb));

    uint8_t ext_in[100] = { 0 };
    ext_in[0] = 0x31U;
    ext_in[74] = 0x82U;
    ext_in[75] = 4U;
    ext_in[76] = 0xAAU; ext_in[77] = 0xBBU; ext_in[78] = 0xCCU; ext_in[79] = 0xDDU;
    uint32_t ext_crc = dongle_dualsense_crc32(0xA1U, ext_in, sizeof(ext_in) - 4U);
    ext_in[96] = (uint8_t)ext_crc;
    ext_in[97] = (uint8_t)(ext_crc >> 8U);
    ext_in[98] = (uint8_t)(ext_crc >> 16U);
    ext_in[99] = (uint8_t)(ext_crc >> 24U);
    CHECK(dongle_dualsense_bt_extract_mic_opus(ext_in, sizeof(ext_in), &opus_ptr, &opus_len));
    CHECK(opus_len == 4U && opus_ptr[0] == 0xAAU && opus_ptr[3] == 0xDDU);

    const int16_t in8[3] = { 100, 200, 300 };
    int16_t out16[6] = { 0 };
    int16_t prev = 0;
    CHECK(dongle_pcm_upsample_8k_to_16k(in8, 3U, out16, 6U, &prev) == 6U);
    CHECK(out16[0] == 50 && out16[1] == 100 && out16[2] == 150 &&
          out16[3] == 200 && out16[4] == 250 && out16[5] == 300);
    CHECK(prev == 300);

    /* Coalescing merge preserves pending adaptive triggers when a rumble update follows. */
    uint8_t pending_out[48] = { 0x02U, 0x0CU, 0x00U };
    for (unsigned i = 0U; i < 11U; ++i) {
        pending_out[11U + i] = (uint8_t)(0x21U + i);
        pending_out[22U + i] = (uint8_t)(0x41U + i);
    }
    pending_out[33] = 0x11U;
    uint8_t rumble_out[48] = { 0x02U, 0x02U, 0x04U, 0x55U, 0xAAU };
    rumble_out[33] = 0x22U;
    rumble_out[39] = 0x04U;
    rumble_out[45] = 0x10U; rumble_out[46] = 0x20U; rumble_out[47] = 0x30U;
    dongle_dualsense_merge_usb_output(pending_out, rumble_out);
    CHECK(pending_out[1] == (0x0CU | 0x02U));
    CHECK(pending_out[2] == 0x04U);
    CHECK(pending_out[3] == 0x55U && pending_out[4] == 0xAAU);
    CHECK(pending_out[39] == 0x04U);
    CHECK(pending_out[33] == 0x22U);
    CHECK(pending_out[45] == 0x10U && pending_out[46] == 0x20U && pending_out[47] == 0x30U);
    for (unsigned i = 0U; i < 11U; ++i) {
        CHECK(pending_out[11U + i] == (uint8_t)(0x21U + i));
        CHECK(pending_out[22U + i] == (uint8_t)(0x41U + i));
    }

    /* A later trigger update overwrites earlier pending trigger data while preserving rumble. */
    uint8_t trig2_out[48] = { 0x02U, 0x04U, 0x00U };
    trig2_out[11] = 0x02U;
    trig2_out[12] = 0x99U;
    trig2_out[33] = 0x33U;
    dongle_dualsense_merge_usb_output(pending_out, trig2_out);
    CHECK(pending_out[1] == (0x0CU | 0x02U));
    CHECK(pending_out[3] == 0x55U && pending_out[4] == 0xAAU);
    CHECK(pending_out[11] == 0x02U && pending_out[12] == 0x99U && pending_out[13] == 0x00U);
    CHECK(pending_out[22] == 0x41U);
    CHECK(pending_out[33] == 0x33U);
    CHECK(pending_out[39] == 0x04U);

    puts("DualSense basic/full/BT<->USB input, output, feature, merge, LED unlock and CRC validated");
    return 0;
}
