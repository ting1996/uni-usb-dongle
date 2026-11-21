#ifndef TEST_USBD_HID_H
#define TEST_USBD_HID_H
#include "usbd_core.h"
struct usbd_interface *usbd_hid_init_intf(uint8_t bus,
    struct usbd_interface *intf, const uint8_t *descriptor, uint32_t length);
#endif
