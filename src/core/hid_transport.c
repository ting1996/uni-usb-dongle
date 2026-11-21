#include "dongle/hid_transport.h"
#include <string.h>

void dongle_hid_transport_reset(dongle_hid_transport_t *t)
{
    t->received = 0;
    t->expected = 0;
    t->ready = false;
    t->processing = false;
    t->reply_seen = false;
    memset(t->reply, 0, sizeof(t->reply));
}

bool dongle_hid_transport_receive(dongle_hid_transport_t *t,
                                 const uint8_t *report, size_t size)
{
    if (report == NULL || size != DONGLE_HID_FEATURE_SIZE ||
        t->ready || t->processing) {
        return false;
    }
    const size_t offset = report[0] | ((size_t)report[1] << 8U);
    const size_t length = report[2];
    if (length == 0 || length > DONGLE_HID_FRAGMENT_SIZE) {
        return false;
    }
    if (offset == 0) {
        t->received = 0;
        t->expected = 0;
        memset(t->reply, 0, sizeof(t->reply));
    }
    if (offset != t->received || length > sizeof(t->request) - t->received) {
        return false;
    }
    memcpy(&t->request[offset], &report[3], length);
    t->received += length;
    if (t->received >= DONGLE_USB_OTA_HEADER_SIZE && t->expected == 0) {
        const size_t payload_size = t->request[10] |
            ((size_t)t->request[11] << 8U);
        if (memcmp(t->request, "UOTA", 4) != 0 || t->request[4] != 1 ||
            payload_size > DONGLE_USB_OTA_MAX_PAYLOAD_SIZE) {
            dongle_hid_transport_reset(t);
            return false;
        }
        t->expected = DONGLE_USB_OTA_HEADER_SIZE + payload_size;
    }
    if (t->expected != 0 && t->received >= t->expected) {
        dongle_usb_ota_frame_t frame;
        if (t->received != t->expected ||
            dongle_usb_ota_decode(t->request, t->received, &frame) !=
                DONGLE_USB_OTA_PROTOCOL_OK) {
            dongle_hid_transport_reset(t);
            return false;
        }
        t->ready = true;
    }
    return true;
}

size_t dongle_hid_transport_take(dongle_hid_transport_t *t,
                                uint8_t *packet, size_t capacity)
{
    if (!t->ready || packet == NULL || capacity < t->received) {
        return 0;
    }
    memcpy(packet, t->request, t->received);
    t->ready = false;
    t->processing = true;
    return t->received;
}

bool dongle_hid_transport_reply(dongle_hid_transport_t *t,
                                const uint8_t *packet, size_t size)
{
    if (!t->processing || packet == NULL || size == 0 ||
        size > DONGLE_HID_FRAGMENT_SIZE) {
        return false;
    }
    memset(t->reply, 0, sizeof(t->reply));
    t->reply[2] = (uint8_t)size;
    memcpy(&t->reply[3], packet, size);
    t->reply_seen = false;
    t->processing = false;
    return true;
}

void dongle_hid_transport_read(dongle_hid_transport_t *t,
                               uint8_t report[DONGLE_HID_FEATURE_SIZE])
{
    memcpy(report, t->reply, DONGLE_HID_FEATURE_SIZE);
    if (t->reply[2] != 0) {
        t->reply_seen = true;
    }
}
