#include "dongle/settings_codec.h"

#include <string.h>

#define SETTINGS_HEADER_SIZE 6U
#define SETTINGS_SLOT_SIZE (1U + DONGLE_BLUETOOTH_ADDRESS_SIZE)
#define SETTINGS_CRC_SIZE 4U
#define SETTINGS_CRC_OFFSET (DONGLE_SETTINGS_BLOB_SIZE - SETTINGS_CRC_SIZE)

static const uint8_t settings_magic[4] = { 'U', 'D', 'N', 'G' };

static uint32_t settings_crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_MAX;

    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8U; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }

    return ~crc;
}

static void write_crc32(uint8_t *buffer, uint32_t crc)
{
    buffer[SETTINGS_CRC_OFFSET] = (uint8_t)crc;
    buffer[SETTINGS_CRC_OFFSET + 1U] = (uint8_t)(crc >> 8U);
    buffer[SETTINGS_CRC_OFFSET + 2U] = (uint8_t)(crc >> 16U);
    buffer[SETTINGS_CRC_OFFSET + 3U] = (uint8_t)(crc >> 24U);
}

static uint32_t read_crc32(const uint8_t *buffer)
{
    return (uint32_t)buffer[SETTINGS_CRC_OFFSET] |
           ((uint32_t)buffer[SETTINGS_CRC_OFFSET + 1U] << 8U) |
           ((uint32_t)buffer[SETTINGS_CRC_OFFSET + 2U] << 16U) |
           ((uint32_t)buffer[SETTINGS_CRC_OFFSET + 3U] << 24U);
}

static bool catalog_is_valid(const dongle_device_catalog_t *catalog)
{
    if (catalog->selected_slot != DONGLE_NO_SELECTED_SLOT &&
        (catalog->selected_slot >= DONGLE_CONTROLLER_SLOT_COUNT ||
         !catalog->controllers[catalog->selected_slot].paired)) {
        return false;
    }

    for (uint8_t slot = 0; slot < DONGLE_CONTROLLER_SLOT_COUNT; ++slot) {
        const dongle_controller_slot_t *controller = &catalog->controllers[slot];
        if (controller->usb_gamepad_index != slot) {
            return false;
        }
        if (!controller->paired) {
            for (size_t i = 0; i < DONGLE_BLUETOOTH_ADDRESS_SIZE; ++i) {
                if (controller->address[i] != 0U) {
                    return false;
                }
            }
            continue;
        }
        if (!dongle_catalog_address_is_valid(controller->address)) {
            return false;
        }

        for (uint8_t other = 0; other < slot; ++other) {
            if (catalog->controllers[other].paired &&
                memcmp(controller->address, catalog->controllers[other].address,
                       DONGLE_BLUETOOTH_ADDRESS_SIZE) == 0) {
                return false;
            }
        }
    }

    return true;
}

dongle_settings_codec_result_t dongle_settings_encode(
    const dongle_device_catalog_t *catalog,
    uint8_t *buffer,
    size_t buffer_size)
{
    if (catalog == NULL || buffer == NULL || buffer_size < DONGLE_SETTINGS_BLOB_SIZE) {
        return DONGLE_SETTINGS_CODEC_INVALID_ARGUMENT;
    }
    if (!catalog_is_valid(catalog)) {
        return DONGLE_SETTINGS_CODEC_INVALID_DATA;
    }

    memset(buffer, 0, DONGLE_SETTINGS_BLOB_SIZE);
    memcpy(buffer, settings_magic, sizeof(settings_magic));
    buffer[4] = 1U;
    buffer[5] = catalog->selected_slot;

    size_t offset = SETTINGS_HEADER_SIZE;
    for (uint8_t slot = 0; slot < DONGLE_CONTROLLER_SLOT_COUNT; ++slot) {
        const dongle_controller_slot_t *controller = &catalog->controllers[slot];
        buffer[offset++] = controller->paired ? 1U : 0U;
        memcpy(&buffer[offset], controller->address, DONGLE_BLUETOOTH_ADDRESS_SIZE);
        offset += DONGLE_BLUETOOTH_ADDRESS_SIZE;
    }

    write_crc32(buffer, settings_crc32(buffer, SETTINGS_CRC_OFFSET));
    return DONGLE_SETTINGS_CODEC_OK;
}

dongle_settings_codec_result_t dongle_settings_decode(
    const uint8_t *buffer,
    size_t buffer_size,
    dongle_device_catalog_t *catalog)
{
    if (buffer == NULL || catalog == NULL || buffer_size != DONGLE_SETTINGS_BLOB_SIZE) {
        return DONGLE_SETTINGS_CODEC_INVALID_ARGUMENT;
    }
    if (memcmp(buffer, settings_magic, sizeof(settings_magic)) != 0 ||
        buffer[4] != 1U ||
        read_crc32(buffer) != settings_crc32(buffer, SETTINGS_CRC_OFFSET)) {
        return DONGLE_SETTINGS_CODEC_INVALID_DATA;
    }

    dongle_device_catalog_t decoded;
    if (dongle_catalog_init(&decoded) != DONGLE_CATALOG_OK) {
        return DONGLE_SETTINGS_CODEC_INVALID_DATA;
    }

    decoded.selected_slot = buffer[5];
    size_t offset = SETTINGS_HEADER_SIZE;
    for (uint8_t slot = 0; slot < DONGLE_CONTROLLER_SLOT_COUNT; ++slot) {
        dongle_controller_slot_t *controller = &decoded.controllers[slot];
        const uint8_t paired = buffer[offset++];
        if (paired > 1U) {
            return DONGLE_SETTINGS_CODEC_INVALID_DATA;
        }
        controller->paired = paired == 1U;
        memcpy(controller->address, &buffer[offset], DONGLE_BLUETOOTH_ADDRESS_SIZE);
        offset += DONGLE_BLUETOOTH_ADDRESS_SIZE;
    }

    if (!catalog_is_valid(&decoded)) {
        return DONGLE_SETTINGS_CODEC_INVALID_DATA;
    }

    *catalog = decoded;
    return DONGLE_SETTINGS_CODEC_OK;
}
