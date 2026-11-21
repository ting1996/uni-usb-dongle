#include "dongle/usb_ota_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            fprintf(stderr, "Check failed at line %d: %s\n", __LINE__, #expression); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

static void test_frame_round_trip(void)
{
    const uint8_t payload[] = { 0x10, 0x20, 0x30, 0x40 };
    uint8_t encoded[DONGLE_USB_OTA_HEADER_SIZE + sizeof(payload)];
    size_t encoded_size = 0U;
    dongle_usb_ota_frame_t decoded;

    CHECK(dongle_usb_ota_encode(encoded, sizeof(encoded),
                                DONGLE_USB_OTA_CMD_DATA, 0x12345678U,
                                payload, sizeof(payload), &encoded_size) ==
          DONGLE_USB_OTA_PROTOCOL_OK);
    CHECK(encoded_size == sizeof(encoded));
    const uint8_t expected[] = {
        0x55, 0x4F, 0x54, 0x41, 0x01, 0x02, 0x78, 0x56, 0x34, 0x12,
        0x04, 0x00, 0x3B, 0x3A, 0x53, 0xF7, 0x10, 0x20, 0x30, 0x40
    };
    CHECK(memcmp(encoded, expected, sizeof(expected)) == 0);
    CHECK(dongle_usb_ota_decode(encoded, encoded_size, &decoded) ==
          DONGLE_USB_OTA_PROTOCOL_OK);
    CHECK(decoded.command == DONGLE_USB_OTA_CMD_DATA);
    CHECK(decoded.sequence == 0x12345678U);
    CHECK(decoded.payload_size == sizeof(payload));
    CHECK(memcmp(decoded.payload, payload, sizeof(payload)) == 0);
}

static void test_corruption_and_truncation_are_rejected(void)
{
    uint8_t encoded[DONGLE_USB_OTA_HEADER_SIZE + 1U];
    const uint8_t payload = 0xA5U;
    size_t encoded_size = 0U;
    dongle_usb_ota_frame_t decoded;

    CHECK(dongle_usb_ota_encode(encoded, sizeof(encoded),
                                DONGLE_USB_OTA_CMD_BEGIN, 0U, &payload, 1U,
                                &encoded_size) == DONGLE_USB_OTA_PROTOCOL_OK);
    encoded[sizeof(encoded) - 1U] ^= 0x01U;
    CHECK(dongle_usb_ota_decode(encoded, encoded_size, &decoded) ==
          DONGLE_USB_OTA_PROTOCOL_CHECKSUM_MISMATCH);
    CHECK(dongle_usb_ota_decode(encoded, encoded_size - 1U, &decoded) ==
          DONGLE_USB_OTA_PROTOCOL_INVALID_FRAME);
}

static void test_invalid_sizes_and_arguments_are_rejected(void)
{
    uint8_t encoded[DONGLE_USB_OTA_HEADER_SIZE + DONGLE_USB_OTA_MAX_PAYLOAD_SIZE];
    dongle_usb_ota_frame_t decoded;
    size_t encoded_size = 0U;

    CHECK(dongle_usb_ota_encode(encoded, sizeof(encoded),
                                DONGLE_USB_OTA_CMD_FINISH, 0U, NULL, 0U,
                                &encoded_size) == DONGLE_USB_OTA_PROTOCOL_OK);
    CHECK(dongle_usb_ota_encode(encoded, sizeof(encoded), 1U, 0U, NULL, 1U,
                                &encoded_size) ==
          DONGLE_USB_OTA_PROTOCOL_INVALID_ARGUMENT);
    CHECK(dongle_usb_ota_encode(encoded, sizeof(encoded), 1U, 0U, encoded,
                                DONGLE_USB_OTA_MAX_PAYLOAD_SIZE + 1U,
                                &encoded_size) ==
          DONGLE_USB_OTA_PROTOCOL_PAYLOAD_TOO_LARGE);
    CHECK(dongle_usb_ota_decode(NULL, 0U, &decoded) ==
          DONGLE_USB_OTA_PROTOCOL_INVALID_ARGUMENT);
    CHECK(dongle_usb_ota_decode(encoded, DONGLE_USB_OTA_HEADER_SIZE, NULL) ==
          DONGLE_USB_OTA_PROTOCOL_INVALID_ARGUMENT);
}

int main(void)
{
    test_frame_round_trip();
    test_corruption_and_truncation_are_rejected();
    test_invalid_sizes_and_arguments_are_rejected();
    return 0;
}
