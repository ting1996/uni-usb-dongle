#ifndef DONGLE_HID_TRANSPORT_H
#define DONGLE_HID_TRANSPORT_H

#include "dongle/usb_ota_protocol.h"
#include <stdbool.h>

#define DONGLE_HID_REPORT_ID 2U
#define DONGLE_HID_FEATURE_SIZE 63U
#define DONGLE_HID_FRAGMENT_SIZE 60U
#define DONGLE_HID_FRAME_CAPACITY \
    (DONGLE_USB_OTA_HEADER_SIZE + DONGLE_USB_OTA_MAX_PAYLOAD_SIZE)

/* Caller serializes access between the USB callback and application task.
 * Feature data: little-endian frame offset (2), length (1), data (0..60).
 * One complete request at a time. Replies fit one feature report and remain
 * readable until the next request starts, so polling is repeatable. */
typedef struct {
    uint8_t request[DONGLE_HID_FRAME_CAPACITY];
    uint8_t reply[DONGLE_HID_FEATURE_SIZE];
    size_t received;
    size_t expected;
    bool ready;
    bool processing;
    bool reply_seen;
} dongle_hid_transport_t;

void dongle_hid_transport_reset(dongle_hid_transport_t *transport);
bool dongle_hid_transport_receive(dongle_hid_transport_t *transport,
                                 const uint8_t *report, size_t size);
size_t dongle_hid_transport_take(dongle_hid_transport_t *transport,
                                uint8_t *packet, size_t capacity);
bool dongle_hid_transport_reply(dongle_hid_transport_t *transport,
                                const uint8_t *packet, size_t size);
void dongle_hid_transport_read(dongle_hid_transport_t *transport,
                               uint8_t report[DONGLE_HID_FEATURE_SIZE]);
#endif
