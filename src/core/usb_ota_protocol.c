#include "dongle/usb_ota_protocol.h"

#include <string.h>

#define USB_OTA_VERSION 1U
#define USB_OTA_SEQUENCE_OFFSET 6U
#define USB_OTA_LENGTH_OFFSET 10U
#define USB_OTA_CHECKSUM_OFFSET 12U

static const uint8_t usb_ota_magic[4] = { 'U', 'O', 'T', 'A' };

static uint32_t read_u32_le(const uint8_t *buffer)
{
    return (uint32_t)buffer[0] |
           ((uint32_t)buffer[1] << 8U) |
           ((uint32_t)buffer[2] << 16U) |
           ((uint32_t)buffer[3] << 24U);
}

static uint16_t read_u16_le(const uint8_t *buffer)
{
    return (uint16_t)((uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8U));
}

static void write_u32_le(uint8_t *buffer, uint32_t value)
{
    buffer[0] = (uint8_t)value;
    buffer[1] = (uint8_t)(value >> 8U);
    buffer[2] = (uint8_t)(value >> 16U);
    buffer[3] = (uint8_t)(value >> 24U);
}

static void write_u16_le(uint8_t *buffer, uint16_t value)
{
    buffer[0] = (uint8_t)value;
    buffer[1] = (uint8_t)(value >> 8U);
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t size)
{
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8U; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return crc;
}

static uint32_t frame_checksum(const uint8_t *header, const uint8_t *payload,
                               uint16_t payload_size)
{
    uint32_t crc = UINT32_MAX;
    crc = crc32_update(crc, &header[4], USB_OTA_CHECKSUM_OFFSET - 4U);
    if (payload_size > 0U) {
        crc = crc32_update(crc, payload, payload_size);
    }
    return ~crc;
}

dongle_usb_ota_protocol_result_t dongle_usb_ota_encode(
    uint8_t *buffer,
    size_t buffer_size,
    uint8_t command,
    uint32_t sequence,
    const uint8_t *payload,
    uint16_t payload_size,
    size_t *encoded_size)
{
    if (buffer == NULL || encoded_size == NULL ||
        (payload_size > 0U && payload == NULL)) {
        return DONGLE_USB_OTA_PROTOCOL_INVALID_ARGUMENT;
    }
    if (payload_size > DONGLE_USB_OTA_MAX_PAYLOAD_SIZE) {
        return DONGLE_USB_OTA_PROTOCOL_PAYLOAD_TOO_LARGE;
    }

    const size_t total_size = DONGLE_USB_OTA_HEADER_SIZE + payload_size;
    if (buffer_size < total_size) {
        return DONGLE_USB_OTA_PROTOCOL_INVALID_ARGUMENT;
    }

    memcpy(buffer, usb_ota_magic, sizeof(usb_ota_magic));
    buffer[4] = USB_OTA_VERSION;
    buffer[5] = command;
    write_u32_le(&buffer[USB_OTA_SEQUENCE_OFFSET], sequence);
    write_u16_le(&buffer[USB_OTA_LENGTH_OFFSET], payload_size);
    if (payload_size > 0U) {
        memcpy(&buffer[DONGLE_USB_OTA_HEADER_SIZE], payload, payload_size);
    }
    write_u32_le(&buffer[USB_OTA_CHECKSUM_OFFSET],
                 frame_checksum(buffer, &buffer[DONGLE_USB_OTA_HEADER_SIZE],
                                payload_size));
    *encoded_size = total_size;
    return DONGLE_USB_OTA_PROTOCOL_OK;
}

dongle_usb_ota_protocol_result_t dongle_usb_ota_decode(
    const uint8_t *buffer,
    size_t buffer_size,
    dongle_usb_ota_frame_t *frame)
{
    if (buffer == NULL || frame == NULL) {
        return DONGLE_USB_OTA_PROTOCOL_INVALID_ARGUMENT;
    }
    if (buffer_size < DONGLE_USB_OTA_HEADER_SIZE ||
        memcmp(buffer, usb_ota_magic, sizeof(usb_ota_magic)) != 0) {
        return DONGLE_USB_OTA_PROTOCOL_INVALID_FRAME;
    }
    if (buffer[4] != USB_OTA_VERSION) {
        return DONGLE_USB_OTA_PROTOCOL_UNSUPPORTED_VERSION;
    }

    const uint16_t payload_size = read_u16_le(&buffer[USB_OTA_LENGTH_OFFSET]);
    if (payload_size > DONGLE_USB_OTA_MAX_PAYLOAD_SIZE) {
        return DONGLE_USB_OTA_PROTOCOL_PAYLOAD_TOO_LARGE;
    }
    if (buffer_size != DONGLE_USB_OTA_HEADER_SIZE + payload_size) {
        return DONGLE_USB_OTA_PROTOCOL_INVALID_FRAME;
    }
    if (read_u32_le(&buffer[USB_OTA_CHECKSUM_OFFSET]) !=
        frame_checksum(buffer, &buffer[DONGLE_USB_OTA_HEADER_SIZE],
                       payload_size)) {
        return DONGLE_USB_OTA_PROTOCOL_CHECKSUM_MISMATCH;
    }

    frame->command = buffer[5];
    frame->sequence = read_u32_le(&buffer[USB_OTA_SEQUENCE_OFFSET]);
    frame->payload_size = payload_size;
    frame->payload = &buffer[DONGLE_USB_OTA_HEADER_SIZE];
    return DONGLE_USB_OTA_PROTOCOL_OK;
}
