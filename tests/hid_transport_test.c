#include "dongle/hid_transport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%d: %s\n", __LINE__, #c); exit(1); } } while (0)

static void fragment(uint8_t *report, size_t offset, const uint8_t *data, size_t n)
{
    memset(report, 0, DONGLE_HID_FEATURE_SIZE);
    report[0] = (uint8_t)offset;
    report[1] = (uint8_t)(offset >> 8);
    report[2] = (uint8_t)n;
    memcpy(&report[3], data, n);
}

int main(void)
{
    dongle_hid_transport_t t = { 0 };
    uint8_t payload[1024], packet[DONGLE_HID_FRAME_CAPACITY], copy[sizeof(packet)];
    uint8_t report[DONGLE_HID_FEATURE_SIZE], again[DONGLE_HID_FEATURE_SIZE];
    for (size_t i = 0; i < sizeof(payload); ++i) { payload[i] = (uint8_t)i; }
    size_t length = 0;
    CHECK(dongle_usb_ota_encode(packet, sizeof(packet), 2, 512, payload,
        sizeof(payload), &length) == DONGLE_USB_OTA_PROTOCOL_OK);
    CHECK(length == sizeof(packet));
    dongle_hid_transport_read(&t, report);
    CHECK(report[2] == 0);
    CHECK(dongle_hid_transport_take(&t, copy, sizeof(copy)) == 0);

    for (size_t offset = 0; offset < length;) {
        const size_t n = length - offset > 60 ? 60 : length - offset;
        fragment(report, offset, &packet[offset], n);
        CHECK(dongle_hid_transport_receive(&t, report, sizeof(report)));
        offset += n;
        CHECK(t.ready == (offset == length));
    }
    CHECK(!dongle_hid_transport_receive(&t, report, sizeof(report)));
    CHECK(dongle_hid_transport_take(&t, copy, 10) == 0);
    CHECK(dongle_hid_transport_take(&t, copy, sizeof(copy)) == length);
    CHECK(memcmp(copy, packet, length) == 0);
    CHECK(!dongle_hid_transport_receive(&t, report, sizeof(report)));
    CHECK(!dongle_hid_transport_reply(&t, packet, 61));
    const uint8_t reply[] = { 0, 0, 2, 0, 0 };
    CHECK(dongle_usb_ota_encode(copy, sizeof(copy), 0x82, 512, reply,
        sizeof(reply), &length) == DONGLE_USB_OTA_PROTOCOL_OK);
    CHECK(dongle_hid_transport_reply(&t, copy, length));
    CHECK(!t.reply_seen);
    dongle_hid_transport_read(&t, report);
    dongle_hid_transport_read(&t, again);
    CHECK(t.reply_seen && memcmp(report, again, sizeof(report)) == 0);
    CHECK(report[2] == length && memcmp(&report[3], copy, length) == 0);

    /* An interrupted request can be replaced by a fresh offset-zero frame. */
    fragment(report, 0, packet, 60);
    CHECK(dongle_hid_transport_receive(&t, report, sizeof(report)));
    dongle_hid_transport_read(&t, again);
    CHECK(again[2] == 0); /* Do not return an old command's reply. */
    fragment(report, 61, &packet[60], 60);
    CHECK(!dongle_hid_transport_receive(&t, report, sizeof(report)));
    fragment(report, 0, copy, length);
    CHECK(dongle_hid_transport_receive(&t, report, sizeof(report)));
    CHECK(t.ready);
    dongle_hid_transport_reset(&t);
    CHECK(!t.ready && !t.processing && t.received == 0);

    /* Corruption and malformed reports must never reach command dispatch. */
    copy[15] ^= 1;
    fragment(report, 0, copy, length);
    CHECK(!dongle_hid_transport_receive(&t, report, sizeof(report)));
    CHECK(!t.ready);
    report[2] = 61;
    CHECK(!dongle_hid_transport_receive(&t, report, sizeof(report)));
    CHECK(!dongle_hid_transport_receive(&t, report, 62));
    CHECK(!dongle_hid_transport_receive(&t, NULL, 63));
    puts("HID transport tests passed");
    return 0;
}
