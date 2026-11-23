#include "dongle/device_blocklist.h"
#include "codec_crc32.h"

#include <string.h>

#define BLOCKLIST_HEADER_SIZE 8U
#define BLOCKLIST_CRC_OFFSET (DONGLE_BLOCKLIST_BLOB_SIZE - 4U)

static const uint8_t blocklist_magic[4] = { 'U', 'B', 'L', 'K' };

static bool blocklist_is_valid(const dongle_device_blocklist_t *list)
{
    if (list == NULL || list->count > DONGLE_BLOCKLIST_MAX_ENTRIES) {
        return false;
    }
    for (uint8_t i = 0U; i < DONGLE_BLOCKLIST_MAX_ENTRIES; ++i) {
        if (i < list->count) {
            if (!dongle_catalog_address_is_valid(list->addresses[i])) {
                return false;
            }
            for (uint8_t j = 0U; j < i; ++j) {
                if (memcmp(list->addresses[i], list->addresses[j],
                           DONGLE_BLUETOOTH_ADDRESS_SIZE) == 0) {
                    return false;
                }
            }
        } else {
            for (size_t b = 0U; b < DONGLE_BLUETOOTH_ADDRESS_SIZE; ++b) {
                if (list->addresses[i][b] != 0U) {
                    return false;
                }
            }
        }
    }
    return true;
}

dongle_blocklist_result_t dongle_blocklist_init(dongle_device_blocklist_t *list)
{
    if (list == NULL) {
        return DONGLE_BLOCKLIST_INVALID_ARGUMENT;
    }
    memset(list, 0, sizeof(*list));
    return DONGLE_BLOCKLIST_OK;
}

bool dongle_blocklist_contains(
    const dongle_device_blocklist_t *list,
    const uint8_t address[DONGLE_BLUETOOTH_ADDRESS_SIZE])
{
    if (list == NULL || address == NULL ||
        list->count > DONGLE_BLOCKLIST_MAX_ENTRIES ||
        !dongle_catalog_address_is_valid(address)) {
        return false;
    }
    for (uint8_t i = 0U; i < list->count; ++i) {
        if (memcmp(list->addresses[i], address,
                   DONGLE_BLUETOOTH_ADDRESS_SIZE) == 0) {
            return true;
        }
    }
    return false;
}

dongle_blocklist_result_t dongle_blocklist_add(
    dongle_device_blocklist_t *list,
    const uint8_t address[DONGLE_BLUETOOTH_ADDRESS_SIZE])
{
    if (list == NULL || address == NULL ||
        list->count > DONGLE_BLOCKLIST_MAX_ENTRIES) {
        return DONGLE_BLOCKLIST_INVALID_ARGUMENT;
    }
    if (!dongle_catalog_address_is_valid(address)) {
        return DONGLE_BLOCKLIST_INVALID_ADDRESS;
    }
    if (dongle_blocklist_contains(list, address)) {
        return DONGLE_BLOCKLIST_DUPLICATE;
    }
    if (list->count >= DONGLE_BLOCKLIST_MAX_ENTRIES) {
        return DONGLE_BLOCKLIST_FULL;
    }
    memcpy(list->addresses[list->count], address, DONGLE_BLUETOOTH_ADDRESS_SIZE);
    ++list->count;
    return DONGLE_BLOCKLIST_OK;
}

dongle_blocklist_result_t dongle_blocklist_remove(
    dongle_device_blocklist_t *list,
    const uint8_t address[DONGLE_BLUETOOTH_ADDRESS_SIZE])
{
    if (list == NULL || address == NULL ||
        list->count > DONGLE_BLOCKLIST_MAX_ENTRIES) {
        return DONGLE_BLOCKLIST_INVALID_ARGUMENT;
    }
    if (!dongle_catalog_address_is_valid(address)) {
        return DONGLE_BLOCKLIST_INVALID_ADDRESS;
    }
    for (uint8_t i = 0U; i < list->count; ++i) {
        if (memcmp(list->addresses[i], address,
                   DONGLE_BLUETOOTH_ADDRESS_SIZE) == 0) {
            for (uint8_t j = i; (uint8_t)(j + 1U) < list->count; ++j) {
                memcpy(list->addresses[j], list->addresses[j + 1U],
                       DONGLE_BLUETOOTH_ADDRESS_SIZE);
            }
            --list->count;
            memset(list->addresses[list->count], 0, DONGLE_BLUETOOTH_ADDRESS_SIZE);
            return DONGLE_BLOCKLIST_OK;
        }
    }
    return DONGLE_BLOCKLIST_NOT_FOUND;
}

dongle_blocklist_result_t dongle_blocklist_encode(
    const dongle_device_blocklist_t *list,
    uint8_t *buffer,
    size_t buffer_size)
{
    if (list == NULL || buffer == NULL ||
        buffer_size < DONGLE_BLOCKLIST_BLOB_SIZE) {
        return DONGLE_BLOCKLIST_INVALID_ARGUMENT;
    }
    if (!blocklist_is_valid(list)) {
        return DONGLE_BLOCKLIST_INVALID_DATA;
    }

    memset(buffer, 0, DONGLE_BLOCKLIST_BLOB_SIZE);
    memcpy(buffer, blocklist_magic, sizeof(blocklist_magic));
    buffer[4] = 1U;
    buffer[5] = list->count;
    for (uint8_t i = 0U; i < list->count; ++i) {
        memcpy(&buffer[BLOCKLIST_HEADER_SIZE + i * DONGLE_BLUETOOTH_ADDRESS_SIZE],
               list->addresses[i], DONGLE_BLUETOOTH_ADDRESS_SIZE);
    }
    dongle_codec_write_le32(&buffer[BLOCKLIST_CRC_OFFSET],
                            dongle_codec_crc32(buffer, BLOCKLIST_CRC_OFFSET));
    return DONGLE_BLOCKLIST_OK;
}

dongle_blocklist_result_t dongle_blocklist_decode(
    const uint8_t *buffer,
    size_t buffer_size,
    dongle_device_blocklist_t *list)
{
    if (buffer == NULL || list == NULL ||
        buffer_size != DONGLE_BLOCKLIST_BLOB_SIZE) {
        return DONGLE_BLOCKLIST_INVALID_ARGUMENT;
    }
    if (memcmp(buffer, blocklist_magic, sizeof(blocklist_magic)) != 0 ||
        buffer[4] != 1U ||
        dongle_codec_read_le32(&buffer[BLOCKLIST_CRC_OFFSET]) !=
            dongle_codec_crc32(buffer, BLOCKLIST_CRC_OFFSET)) {
        return DONGLE_BLOCKLIST_INVALID_DATA;
    }

    dongle_device_blocklist_t decoded;
    (void)dongle_blocklist_init(&decoded);
    decoded.count = buffer[5];
    if (decoded.count > DONGLE_BLOCKLIST_MAX_ENTRIES) {
        return DONGLE_BLOCKLIST_INVALID_DATA;
    }
    for (uint8_t i = 0U; i < DONGLE_BLOCKLIST_MAX_ENTRIES; ++i) {
        memcpy(decoded.addresses[i],
               &buffer[BLOCKLIST_HEADER_SIZE + i * DONGLE_BLUETOOTH_ADDRESS_SIZE],
               DONGLE_BLUETOOTH_ADDRESS_SIZE);
    }
    if (!blocklist_is_valid(&decoded)) {
        return DONGLE_BLOCKLIST_INVALID_DATA;
    }
    *list = decoded;
    return DONGLE_BLOCKLIST_OK;
}
