#ifndef DONGLE_USB_HID_TRANSPORT_H
#define DONGLE_USB_HID_TRANSPORT_H
#include "dongle/hid_transport.h"

void dongle_usb_hid_reset(void);
void dongle_usb_hid_set_feature(const uint8_t *data, size_t size);
void dongle_usb_hid_get_feature(uint8_t *data);
size_t dongle_usb_hid_take(uint8_t *packet, size_t capacity);
bool dongle_usb_hid_reply(const uint8_t *packet, size_t size);
bool dongle_usb_hid_reply_seen(void);
#endif
