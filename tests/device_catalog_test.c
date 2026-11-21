#include "dongle/device_catalog.h"

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

static void test_two_controllers_have_distinct_stable_gamepad_indexes(void)
{
    dongle_device_catalog_t catalog;
    const uint8_t first_address[DONGLE_BLUETOOTH_ADDRESS_SIZE] = { 1, 2, 3, 4, 5, 6 };
    const uint8_t second_address[DONGLE_BLUETOOTH_ADDRESS_SIZE] = { 7, 8, 9, 10, 11, 12 };
    uint8_t first_slot = DONGLE_NO_SELECTED_SLOT;
    uint8_t second_slot = DONGLE_NO_SELECTED_SLOT;

    CHECK(dongle_catalog_init(&catalog) == DONGLE_CATALOG_OK);
    CHECK(dongle_catalog_pair_controller(&catalog, first_address, &first_slot) ==
          DONGLE_CATALOG_OK);
    CHECK(dongle_catalog_pair_controller(&catalog, second_address, &second_slot) ==
          DONGLE_CATALOG_OK);
    CHECK(first_slot != second_slot);
    CHECK(catalog.controllers[first_slot].usb_gamepad_index == first_slot);
    CHECK(catalog.controllers[second_slot].usb_gamepad_index == second_slot);
}

static void test_duplicate_pairing_and_full_catalog_do_not_mutate_slots(void)
{
    dongle_device_catalog_t catalog;
    const uint8_t first_address[DONGLE_BLUETOOTH_ADDRESS_SIZE] = { 1, 2, 3, 4, 5, 6 };
    const uint8_t second_address[DONGLE_BLUETOOTH_ADDRESS_SIZE] = { 7, 8, 9, 10, 11, 12 };
    const uint8_t third_address[DONGLE_BLUETOOTH_ADDRESS_SIZE] = { 13, 14, 15, 16, 17, 18 };
    uint8_t slot = DONGLE_NO_SELECTED_SLOT;

    CHECK(dongle_catalog_init(&catalog) == DONGLE_CATALOG_OK);
    CHECK(dongle_catalog_pair_controller(&catalog, first_address, &slot) ==
          DONGLE_CATALOG_OK);
    CHECK(dongle_catalog_pair_controller(&catalog, second_address, &slot) ==
          DONGLE_CATALOG_OK);

    const dongle_device_catalog_t before = catalog;
    CHECK(dongle_catalog_pair_controller(&catalog, first_address, &slot) ==
          DONGLE_CATALOG_DUPLICATE_DEVICE);
    CHECK(dongle_catalog_pair_controller(&catalog, third_address, &slot) ==
          DONGLE_CATALOG_FULL);
    CHECK(memcmp(&catalog, &before, sizeof(catalog)) == 0);
}

static void test_remove_and_select_validate_slots(void)
{
    dongle_device_catalog_t catalog;
    const uint8_t address[DONGLE_BLUETOOTH_ADDRESS_SIZE] = { 1, 2, 3, 4, 5, 6 };
    uint8_t slot = DONGLE_NO_SELECTED_SLOT;

    CHECK(dongle_catalog_init(&catalog) == DONGLE_CATALOG_OK);
    CHECK(dongle_catalog_select_controller(&catalog, 0) == DONGLE_CATALOG_SLOT_EMPTY);
    CHECK(dongle_catalog_pair_controller(&catalog, address, &slot) == DONGLE_CATALOG_OK);
    CHECK(dongle_catalog_select_controller(&catalog, slot) == DONGLE_CATALOG_OK);
    CHECK(catalog.selected_slot == slot);
    CHECK(dongle_catalog_remove_controller(&catalog, slot) == DONGLE_CATALOG_OK);
    CHECK(catalog.selected_slot == DONGLE_NO_SELECTED_SLOT);
    CHECK(dongle_catalog_remove_controller(&catalog, slot) == DONGLE_CATALOG_SLOT_EMPTY);
    CHECK(dongle_catalog_select_controller(&catalog, DONGLE_CONTROLLER_SLOT_COUNT) ==
          DONGLE_CATALOG_INVALID_ARGUMENT);
}

static void test_invalid_arguments_and_addresses_are_rejected(void)
{
    dongle_device_catalog_t catalog;
    const uint8_t zero_address[DONGLE_BLUETOOTH_ADDRESS_SIZE] = { 0 };
    uint8_t slot = DONGLE_NO_SELECTED_SLOT;

    CHECK(dongle_catalog_init(NULL) == DONGLE_CATALOG_INVALID_ARGUMENT);
    CHECK(dongle_catalog_init(&catalog) == DONGLE_CATALOG_OK);
    CHECK(dongle_catalog_pair_controller(NULL, zero_address, &slot) ==
          DONGLE_CATALOG_INVALID_ARGUMENT);
    CHECK(dongle_catalog_pair_controller(&catalog, NULL, &slot) ==
          DONGLE_CATALOG_INVALID_ARGUMENT);
    CHECK(dongle_catalog_pair_controller(&catalog, zero_address, &slot) ==
          DONGLE_CATALOG_INVALID_ADDRESS);
    CHECK(dongle_catalog_remove_controller(&catalog, DONGLE_CONTROLLER_SLOT_COUNT) ==
          DONGLE_CATALOG_INVALID_ARGUMENT);
}

int main(void)
{
    test_two_controllers_have_distinct_stable_gamepad_indexes();
    test_duplicate_pairing_and_full_catalog_do_not_mutate_slots();
    test_remove_and_select_validate_slots();
    test_invalid_arguments_and_addresses_are_rejected();
    return 0;
}
