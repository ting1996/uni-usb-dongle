#ifndef DONGLE_DEVICE_BLOCKLIST_H
#define DONGLE_DEVICE_BLOCKLIST_H

#include "dongle/device_catalog.h"
#include "dongle/usb_ota_protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DONGLE_BLOCKLIST_BLOB_SIZE 48U

typedef enum {
    DONGLE_BLOCKLIST_OK = 0,
    DONGLE_BLOCKLIST_INVALID_ARGUMENT,
    DONGLE_BLOCKLIST_INVALID_ADDRESS,
    DONGLE_BLOCKLIST_DUPLICATE,
    DONGLE_BLOCKLIST_NOT_FOUND,
    DONGLE_BLOCKLIST_FULL,
    DONGLE_BLOCKLIST_INVALID_DATA
} dongle_blocklist_result_t;

typedef struct {
    uint8_t count;
    uint8_t addresses[DONGLE_BLOCKLIST_MAX_ENTRIES][DONGLE_BLUETOOTH_ADDRESS_SIZE];
} dongle_device_blocklist_t;

dongle_blocklist_result_t dongle_blocklist_init(dongle_device_blocklist_t *list);

bool dongle_blocklist_contains(
    const dongle_device_blocklist_t *list,
    const uint8_t address[DONGLE_BLUETOOTH_ADDRESS_SIZE]);

dongle_blocklist_result_t dongle_blocklist_add(
    dongle_device_blocklist_t *list,
    const uint8_t address[DONGLE_BLUETOOTH_ADDRESS_SIZE]);

dongle_blocklist_result_t dongle_blocklist_remove(
    dongle_device_blocklist_t *list,
    const uint8_t address[DONGLE_BLUETOOTH_ADDRESS_SIZE]);

dongle_blocklist_result_t dongle_blocklist_encode(
    const dongle_device_blocklist_t *list,
    uint8_t *buffer,
    size_t buffer_size);

dongle_blocklist_result_t dongle_blocklist_decode(
    const uint8_t *buffer,
    size_t buffer_size,
    dongle_device_blocklist_t *list);

#endif
