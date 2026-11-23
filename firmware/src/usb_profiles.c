#include "dongle/usb_profiles.h"
#include "dongle/usb_gamepad.h"
#include "dongle/usb_audio.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

static volatile uint8_t active, requested, codec_requested, transition, failure;
static volatile uint8_t saved_default;
static volatile bool persisted_default;
static volatile uint8_t active_gamepads = 1U, desired_gamepads = 1U;
static TickType_t apply_at;
static uint8_t config_fs[384], config_hs[384], config_other_fs[384], config_other_hs[384];
_Static_assert(9U + DONGLE_AUDIO_DESCRIPTOR_SIZE + 25U + 2U * 40U <= sizeof(config_fs),
               "profile config descriptor buffer too small");
static uint8_t device_descriptor[18] = {
    18,1,0,2,0xef,2,1,64,0xff,0xff,0xff,0xff,0x04,0x01,1,2,3,1
};

static const uint8_t ps4_report[] = {
    0x05,1,0x09,5,0xa1,1,0x85,1,
    0x09,0x30,0x09,0x31,0x09,0x32,0x09,0x35,
    0x15,0,0x26,0xff,0,0x75,8,0x95,4,0x81,2,
    0x09,0x39,0x15,0,0x25,7,0x75,4,0x95,1,0x81,0x42,
    0x05,9,0x19,1,0x29,14,0x15,0,0x25,1,0x75,1,0x95,14,0x81,2,
    0x06,0,0xff,0x09,0x20,0x75,6,0x95,1,0x25,0x3f,0x81,2,
    0x05,1,0x09,0x33,0x09,0x34,0x15,0,0x26,0xff,0,0x75,8,0x95,2,0x81,2,
    0x06,0,0xff,0x09,0x21,0x95,54,0x81,2,
    0x85,5,0x09,0x22,0x95,31,0x91,2,
    0x85,2,0x09,0x23,0x95,36,0xb1,2,
    0x85,3,0x09,0x24,0x95,47,0xb1,2,
    0x85,0x12,0x09,0x25,0x95,15,0xb1,2,
    0x85,0xa3,0x09,0x26,0x95,48,0xb1,2,0xc0
};
static const uint8_t switch_report[] = {
    0x05,1,0x09,5,0xa1,1,0x06,0,0xff,0x15,0,0x26,0xff,0,0x75,8,
    0x85,0x30,0x09,0x30,0x95,63,0x81,2,
    0x85,0x21,0x09,0x21,0x95,63,0x81,2,
    0x85,0x81,0x09,0x81,0x95,63,0x81,2,
    0x85,1,0x09,1,0x95,63,0x91,2,
    0x85,0x10,0x09,0x10,0x95,63,0x91,2,
    0x85,0x80,0x09,0x80,0x95,63,0x91,2,0xc0
};
static const uint8_t dualsense_report[] = {
    0x05,1,0x09,5,0xa1,1,
    0x85,1,
    0x09,0x30,0x09,0x31,0x09,0x32,0x09,0x35,
    0x09,0x33,0x09,0x34,
    0x15,0,0x26,0xff,0,0x75,8,0x95,6,0x81,2,
    0x06,0,0xff,0x09,0x20,0x95,1,0x81,2,
    0x05,1,0x09,0x39,0x15,0,0x25,7,0x35,0,0x46,0x3b,1,0x65,0x14,0x75,4,0x95,1,0x81,0x42,
    0x35,0,0x45,0,0x65,0,
    0x05,9,0x19,1,0x29,0x0f,0x15,0,0x25,1,0x75,1,0x95,0x0f,0x81,2,
    0x06,0,0xff,0x09,0x21,0x25,0x1f,0x75,5,0x95,1,0x81,2,
    0x09,0x22,0x26,0xff,0,0x75,8,0x95,0x35,0x81,2,
    0x85,2,0x09,0x23,0x95,0x2f,0x91,2,
    0x85,5,0x09,0x33,0x95,0x28,0xb1,2,
    0x85,8,0x09,0x34,0x95,0x2f,0xb1,2,
    0x85,9,0x09,0x24,0x95,0x13,0xb1,2,
    0x85,0x20,0x09,0x26,0x95,0x3f,0xb1,2,
    0x85,0x22,0x09,0x27,0x95,0x3f,0xb1,2,
    0x85,0x80,0x09,0x28,0x95,0x3f,0xb1,2,
    0x85,0x81,0x09,0x29,0x95,0x3f,0xb1,2,
    0x85,0x85,0x09,0x2d,0x95,0x02,0xb1,2,
    0xc0
};
/* XUSB compatible IDs for the gamepad interfaces (MI_03, MI_04), not the audio
 * or HID management interfaces. Format follows Microsoft OS descriptors v1. */
static const uint8_t msos_string[] = {18,3,'M',0,'S',0,'F',0,'T',0,'1',0,'0',0,'0',0,0x20,0};
static const uint8_t compatible_ids_1[] = {
    40,0,0,0,0,1,4,0,1,0,0,0,0,0,0,0,
    3,1,'X','U','S','B','1','0',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};
static const uint8_t compatible_ids_2[] = {
    64,0,0,0,0,1,4,0,2,0,0,0,0,0,0,0,
    3,1,'X','U','S','B','1','0',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    4,1,'X','U','S','B','1','0',0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};
static const struct usb_msosv1_descriptor msos_1 = {
    .string = msos_string, .vendor_code = 0x20, .compat_id = compatible_ids_1,
};
static const struct usb_msosv1_descriptor msos_2 = {
    .string = msos_string, .vendor_code = 0x20, .compat_id = compatible_ids_2,
};

extern uint16_t dongle_usb_gamepad_generic_descriptor(const uint8_t **descriptor);

static void apply_device_identity(uint8_t profile)
{
    uint16_t vid = 0xffffU;
    uint16_t pid = 0xffffU;
    uint16_t bcd = 0x0104U;
    if (profile == DONGLE_PROFILE_XINPUT) { vid = 0x045eU; pid = 0x028eU; }
    if (profile == DONGLE_PROFILE_PS4) { vid = 0x054cU; pid = 0x09ccU; }
    if (profile == DONGLE_PROFILE_SWITCH) { vid = 0x057eU; pid = 0x2009U; }
    if (profile == DONGLE_PROFILE_DUALSENSE) { vid = 0x054cU; pid = 0x0ce6U; bcd = 0x0100U; }
    device_descriptor[8] = (uint8_t)(vid & 0xffU);
    device_descriptor[9] = (uint8_t)(vid >> 8U);
    device_descriptor[10] = (uint8_t)(pid & 0xffU);
    device_descriptor[11] = (uint8_t)(pid >> 8U);
    device_descriptor[12] = (uint8_t)(bcd & 0xffU);
    device_descriptor[13] = (uint8_t)(bcd >> 8U);
}

void dongle_usb_profile_init(uint8_t saved_profile, uint8_t saved_codec)
{
    if (saved_profile < DONGLE_PROFILE_COUNT) {
        active = saved_profile;
        requested = saved_profile;
        saved_default = saved_profile;
        persisted_default = true;
    } else {
        active = DONGLE_PROFILE_GENERIC;
        requested = DONGLE_PROFILE_GENERIC;
        saved_default = DONGLE_PROFILE_GENERIC;
        persisted_default = false;
    }
    codec_requested = saved_codec <= 1U ? saved_codec : 0U;
    transition = 0U;
    failure = 0U;
    apply_device_identity(active);
}

dongle_output_profile_t dongle_usb_profile(void) { return active; }
uint8_t dongle_usb_profile_gamepad_count(void) { return active_gamepads == 2U ? 2U : 1U; }
uint8_t dongle_usb_profile_gamepad_first_interface(void) { return 3U; }
uint8_t dongle_usb_profile_mgmt_interface(void)
{
    return (uint8_t)(3U + dongle_usb_profile_gamepad_count());
}
uint8_t dongle_usb_profile_gamepad_out_ep(uint8_t slot)
{
    if (slot == 0U && dongle_usb_profile_gamepad_count() == 1U &&
        active != DONGLE_PROFILE_GENERIC) {
        return 0x02U;
    }
    return (uint8_t)(0x01U + slot);
}
uint16_t dongle_usb_profile_report_descriptor(const uint8_t **descriptor)
{
    if (active == DONGLE_PROFILE_PS4) { *descriptor = ps4_report; return sizeof(ps4_report); }
    if (active == DONGLE_PROFILE_SWITCH) { *descriptor = switch_report; return sizeof(switch_report); }
    if (active == DONGLE_PROFILE_DUALSENSE) { *descriptor = dualsense_report; return sizeof(dualsense_report); }
    return dongle_usb_gamepad_generic_descriptor(descriptor);
}
bool dongle_usb_profile_set_connected_controllers(uint8_t connected_mask)
{
    const uint8_t target = (connected_mask & 0x03U) == 0x03U ? 2U : 1U;
    if (target != desired_gamepads) {
        desired_gamepads = target;
        if (transition != 1 && transition != 2) {
            requested = active;
            transition = (active == requested && active_gamepads == desired_gamepads) ? 0 : 1;
            apply_at = xTaskGetTickCount() + pdMS_TO_TICKS(300);
        }
    }
    return transition == 1;
}
uint8_t dongle_usb_profile_request(uint8_t profile, uint8_t codec, bool persist)
{
    if (profile >= DONGLE_PROFILE_COUNT || codec > 3) return 1;
    if (codec > 1) return 8; /* AAC/LDAC encoders are not integrated. */
    if (transition == 1 || transition == 2) return 3;
    requested = profile; codec_requested = codec; failure = 0;
    if (persist) {
        saved_default = profile;
        persisted_default = true;
    }
    transition = (active == requested && active_gamepads == desired_gamepads) ? 0 : 1;
    apply_at = xTaskGetTickCount() + pdMS_TO_TICKS(300);
    return 0;
}
size_t dongle_usb_profile_state(uint8_t out[12])
{
    const uint8_t flags = (uint8_t)((persisted_default ? 0x01U : 0U) |
                                    (active_gamepads == 2U ? 0x02U : 0U));
    const uint8_t state[12] = {0,1,active,requested,0x1f,codec_requested,
        dongle_usb_audio_is_streaming() ? 1 : 0xff,3,transition,failure,
        saved_default,flags};
    memcpy(out, state, sizeof(state)); return sizeof(state);
}
bool dongle_usb_profile_pending(void)
{ return transition == 1 && (int32_t)(xTaskGetTickCount() - apply_at) >= 0; }
void dongle_usb_profile_complete(bool success)
{ transition = success ? 0 : 3; failure = success ? 0 : 5; }
void dongle_usb_profile_activate(void)
{
    transition = 2; active = requested;
    active_gamepads = desired_gamepads == 2U ? 2U : 1U;
    apply_device_identity(active);
}
const uint8_t *dongle_usb_profile_device(void) { return device_descriptor; }
const struct usb_msosv1_descriptor *dongle_usb_profile_msos(void)
{ return dongle_usb_profile_gamepad_count() == 2U ? &msos_2 : &msos_1; }
const uint8_t *dongle_usb_profile_config(uint8_t speed, bool other_speed)
{
    bool hs = speed == USB_SPEED_HIGH;
    uint8_t *out = other_speed ? (hs ? config_other_fs : config_other_hs) :
                                (hs ? config_hs : config_fs);
    if (other_speed) hs = !hs;
    const uint8_t audio_fs[] = { DONGLE_AUDIO_DESCRIPTOR(1) };
    const uint8_t audio_hs[] = { DONGLE_AUDIO_DESCRIPTOR(4) };
    size_t n = 9;
    memcpy(out + n, hs ? audio_hs : audio_fs, sizeof(audio_fs)); n += sizeof(audio_fs);
    const uint8_t *report;
    uint16_t length = dongle_usb_profile_report_descriptor(&report);
    const uint8_t gp_count = dongle_usb_profile_gamepad_count();
    for (unsigned i = 0; i < gp_count; ++i) {
        const uint8_t intf_num = (uint8_t)(3U + i);
        const uint8_t ep_in = (uint8_t)(0x81U + i);
        const uint8_t ep_out = dongle_usb_profile_gamepad_out_ep((uint8_t)i);
        if (active == DONGLE_PROFILE_XINPUT) {
            const uint8_t xusb[] = {
                9,4,intf_num,0,2,0xff,0x5d,1,0,
                17,0x21,0,1,1,0x25,ep_in,0x14,0,0,0,0,0x13,ep_out,8,0,0,
                7,5,ep_in,3,32,0,(uint8_t)(hs ? 6 : 4), 7,5,ep_out,3,32,0,(uint8_t)(hs ? 6 : 4)
            };
            memcpy(out + n, xusb, sizeof(xusb)); n += sizeof(xusb);
        } else {
            bool extra = active != DONGLE_PROFILE_GENERIC;
            const uint8_t hid[] = {
                9,4,intf_num,0,(uint8_t)(extra ? 2 : 1),3,0,0,0,
                9,0x21,0x11,1,0,1,0x22,(uint8_t)(length & 255U),(uint8_t)(length >> 8U),
                7,5,ep_in,3,(uint8_t)(extra ? 64 : 10),0,(uint8_t)(hs ? 6 : 4),
                7,5,ep_out,3,64,0,(uint8_t)(hs ? 6 : 4)
            };
            size_t size = sizeof(hid) - (extra ? 0U : 7U);
            memcpy(out + n, hid, size); n += size;
        }
    }
    const uint8_t mgmt_intf = (uint8_t)(3U + gp_count);
    const uint8_t management[] = {
        9,4,mgmt_intf,0,1,3,0,0,0, 9,0x21,0x11,1,0,1,0x22,23,0,
        7,5,0x85,3,64,0,(uint8_t)(hs ? 6 : 4)
    };
    memcpy(out + n, management, sizeof(management)); n += sizeof(management);
    const uint8_t header[] = {9,(uint8_t)(other_speed ? 7 : 2),(uint8_t)(n & 255U),(uint8_t)(n >> 8U),(uint8_t)(4U + gp_count),1,0,0x80,50};
    memcpy(out, header, sizeof(header));
    return out;
}
