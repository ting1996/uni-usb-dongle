#ifndef DONGLE_DEVICE_CATALOG_H
#define DONGLE_DEVICE_CATALOG_H

#include <stdbool.h>
#include <stdint.h>

#define DONGLE_CONTROLLER_SLOT_COUNT 2U
#define DONGLE_NO_SELECTED_SLOT UINT8_MAX
#define DONGLE_BLUETOOTH_ADDRESS_SIZE 6U

typedef enum {
    DONGLE_CATALOG_OK = 0,
    DONGLE_CATALOG_INVALID_ARGUMENT,
    DONGLE_CATALOG_INVALID_ADDRESS,
    DONGLE_CATALOG_DUPLICATE_DEVICE,
    DONGLE_CATALOG_SLOT_EMPTY,
    DONGLE_CATALOG_FULL
} dongle_catalog_result_t;

typedef struct {
    bool paired;
    uint8_t address[DONGLE_BLUETOOTH_ADDRESS_SIZE];
    uint8_t usb_gamepad_index;
} dongle_controller_slot_t;

typedef struct {
    dongle_controller_slot_t controllers[DONGLE_CONTROLLER_SLOT_COUNT];
    uint8_t selected_slot;
} dongle_device_catalog_t;

dongle_catalog_result_t dongle_catalog_init(dongle_device_catalog_t *catalog);

bool dongle_catalog_address_is_valid(
    const uint8_t address[DONGLE_BLUETOOTH_ADDRESS_SIZE]);

dongle_catalog_result_t dongle_catalog_pair_controller(
    dongle_device_catalog_t *catalog,
    const uint8_t address[DONGLE_BLUETOOTH_ADDRESS_SIZE],
    uint8_t *assigned_slot);

dongle_catalog_result_t dongle_catalog_remove_controller(
    dongle_device_catalog_t *catalog,
    uint8_t slot);

dongle_catalog_result_t dongle_catalog_select_controller(
    dongle_device_catalog_t *catalog,
    uint8_t slot);

#endif
