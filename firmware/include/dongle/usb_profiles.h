#ifndef DONGLE_USB_PROFILES_H
#define DONGLE_USB_PROFILES_H
#include "dongle/output_profile.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct usb_msosv1_descriptor;
#define DONGLE_MANAGEMENT_INTERFACE 4U
#define DONGLE_MANAGEMENT_EP 0x85U
/* Both controller slots share one USB personality. */
void dongle_usb_profile_init(uint8_t saved_profile, uint8_t saved_codec);
dongle_output_profile_t dongle_usb_profile(void);
uint8_t dongle_usb_profile_gamepad_count(void);
uint8_t dongle_usb_profile_gamepad_first_interface(void);
uint8_t dongle_usb_profile_mgmt_interface(void);
uint8_t dongle_usb_profile_gamepad_out_ep(uint8_t slot);
bool dongle_usb_profile_set_connected_controllers(uint8_t connected_mask);
uint8_t dongle_usb_profile_request(uint8_t profile, uint8_t codec, bool persist);
size_t dongle_usb_profile_state(uint8_t out[12]);
bool dongle_usb_profile_pending(void);
void dongle_usb_profile_activate(void);
void dongle_usb_profile_complete(bool success);
const uint8_t *dongle_usb_profile_device(void);
const uint8_t *dongle_usb_profile_config(uint8_t speed, bool other_speed);
uint16_t dongle_usb_profile_report_descriptor(const uint8_t **descriptor);
const struct usb_msosv1_descriptor *dongle_usb_profile_msos(void);
#endif
