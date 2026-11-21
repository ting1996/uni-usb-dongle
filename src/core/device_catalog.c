#include "dongle/device_catalog.h"

#include <stddef.h>
#include <string.h>

bool dongle_catalog_address_is_valid(
    const uint8_t address[DONGLE_BLUETOOTH_ADDRESS_SIZE])
{
    if (address == NULL) {
        return false;
    }

    bool all_zero = true;
    bool all_broadcast = true;

    for (size_t i = 0; i < DONGLE_BLUETOOTH_ADDRESS_SIZE; ++i) {
        all_zero = all_zero && (address[i] == 0U);
        all_broadcast = all_broadcast && (address[i] == UINT8_MAX);
    }

    return !all_zero && !all_broadcast;
}

dongle_catalog_result_t dongle_catalog_init(dongle_device_catalog_t *catalog)
{
    if (catalog == NULL) {
        return DONGLE_CATALOG_INVALID_ARGUMENT;
    }

    memset(catalog, 0, sizeof(*catalog));
    catalog->selected_slot = DONGLE_NO_SELECTED_SLOT;
    for (uint8_t slot = 0; slot < DONGLE_CONTROLLER_SLOT_COUNT; ++slot) {
        catalog->controllers[slot].usb_gamepad_index = slot;
    }
    return DONGLE_CATALOG_OK;
}

dongle_catalog_result_t dongle_catalog_pair_controller(
    dongle_device_catalog_t *catalog,
    const uint8_t address[DONGLE_BLUETOOTH_ADDRESS_SIZE],
    uint8_t *assigned_slot)
{
    if (catalog == NULL || address == NULL || assigned_slot == NULL) {
        return DONGLE_CATALOG_INVALID_ARGUMENT;
    }
    if (!dongle_catalog_address_is_valid(address)) {
        return DONGLE_CATALOG_INVALID_ADDRESS;
    }

    uint8_t free_slot = DONGLE_NO_SELECTED_SLOT;
    for (uint8_t slot = 0; slot < DONGLE_CONTROLLER_SLOT_COUNT; ++slot) {
        const dongle_controller_slot_t *controller = &catalog->controllers[slot];
        if (controller->paired && memcmp(controller->address, address,
                                          DONGLE_BLUETOOTH_ADDRESS_SIZE) == 0) {
            return DONGLE_CATALOG_DUPLICATE_DEVICE;
        }
        if (!controller->paired && free_slot == DONGLE_NO_SELECTED_SLOT) {
            free_slot = slot;
        }
    }

    if (free_slot == DONGLE_NO_SELECTED_SLOT) {
        return DONGLE_CATALOG_FULL;
    }

    dongle_controller_slot_t *controller = &catalog->controllers[free_slot];
    memcpy(controller->address, address, DONGLE_BLUETOOTH_ADDRESS_SIZE);
    controller->paired = true;
    *assigned_slot = free_slot;
    return DONGLE_CATALOG_OK;
}

dongle_catalog_result_t dongle_catalog_remove_controller(
    dongle_device_catalog_t *catalog,
    uint8_t slot)
{
    if (catalog == NULL || slot >= DONGLE_CONTROLLER_SLOT_COUNT) {
        return DONGLE_CATALOG_INVALID_ARGUMENT;
    }
    if (!catalog->controllers[slot].paired) {
        return DONGLE_CATALOG_SLOT_EMPTY;
    }

    memset(catalog->controllers[slot].address, 0, DONGLE_BLUETOOTH_ADDRESS_SIZE);
    catalog->controllers[slot].paired = false;
    if (catalog->selected_slot == slot) {
        catalog->selected_slot = DONGLE_NO_SELECTED_SLOT;
    }
    return DONGLE_CATALOG_OK;
}

dongle_catalog_result_t dongle_catalog_select_controller(
    dongle_device_catalog_t *catalog,
    uint8_t slot)
{
    if (catalog == NULL || slot >= DONGLE_CONTROLLER_SLOT_COUNT) {
        return DONGLE_CATALOG_INVALID_ARGUMENT;
    }
    if (!catalog->controllers[slot].paired) {
        return DONGLE_CATALOG_SLOT_EMPTY;
    }

    catalog->selected_slot = slot;
    return DONGLE_CATALOG_OK;
}
