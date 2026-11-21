#include "dongle/settings_codec.h"

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

static void test_round_trip_preserves_empty_slot_and_selection(void)
{
    dongle_device_catalog_t original;
    dongle_device_catalog_t decoded;
    uint8_t encoded[DONGLE_SETTINGS_BLOB_SIZE];
    const uint8_t address[DONGLE_BLUETOOTH_ADDRESS_SIZE] = { 1, 2, 3, 4, 5, 6 };
    uint8_t slot = DONGLE_NO_SELECTED_SLOT;

    CHECK(dongle_catalog_init(&original) == DONGLE_CATALOG_OK);
    CHECK(dongle_catalog_pair_controller(&original, address, &slot) == DONGLE_CATALOG_OK);
    CHECK(dongle_catalog_select_controller(&original, slot) == DONGLE_CATALOG_OK);
    CHECK(dongle_catalog_remove_controller(&original, slot) == DONGLE_CATALOG_OK);
    CHECK(dongle_catalog_pair_controller(&original, address, &slot) == DONGLE_CATALOG_OK);
    CHECK(slot == 0U);
    CHECK(dongle_catalog_select_controller(&original, slot) == DONGLE_CATALOG_OK);

    const uint8_t second_address[DONGLE_BLUETOOTH_ADDRESS_SIZE] = { 7, 8, 9, 10, 11, 12 };
    CHECK(dongle_catalog_pair_controller(&original, second_address, &slot) == DONGLE_CATALOG_OK);
    CHECK(dongle_catalog_remove_controller(&original, 0U) == DONGLE_CATALOG_OK);
    CHECK(original.controllers[1].paired);

    CHECK(dongle_settings_encode(&original, encoded, sizeof(encoded)) ==
          DONGLE_SETTINGS_CODEC_OK);
    CHECK(dongle_settings_decode(encoded, sizeof(encoded), &decoded) ==
          DONGLE_SETTINGS_CODEC_OK);
    CHECK(decoded.selected_slot == DONGLE_NO_SELECTED_SLOT);
    CHECK(!decoded.controllers[0].paired);
    CHECK(decoded.controllers[1].paired);
    CHECK(decoded.controllers[1].usb_gamepad_index == 1U);
    CHECK(memcmp(decoded.controllers[1].address, second_address, sizeof(second_address)) == 0);
}

static void test_corrupt_data_is_rejected_without_changing_output(void)
{
    dongle_device_catalog_t catalog;
    dongle_device_catalog_t before;
    uint8_t encoded[DONGLE_SETTINGS_BLOB_SIZE];
    const uint8_t address[DONGLE_BLUETOOTH_ADDRESS_SIZE] = { 1, 2, 3, 4, 5, 6 };
    uint8_t slot = DONGLE_NO_SELECTED_SLOT;

    CHECK(dongle_catalog_init(&catalog) == DONGLE_CATALOG_OK);
    CHECK(dongle_catalog_pair_controller(&catalog, address, &slot) == DONGLE_CATALOG_OK);
    CHECK(dongle_settings_encode(&catalog, encoded, sizeof(encoded)) ==
          DONGLE_SETTINGS_CODEC_OK);
    encoded[8] ^= 0x01U;

    CHECK(dongle_catalog_init(&before) == DONGLE_CATALOG_OK);
    CHECK(dongle_settings_decode(encoded, sizeof(encoded), &before) ==
          DONGLE_SETTINGS_CODEC_INVALID_DATA);
    CHECK(before.selected_slot == DONGLE_NO_SELECTED_SLOT);
    CHECK(!before.controllers[0].paired);
}

static void test_invalid_catalog_and_buffer_sizes_are_rejected(void)
{
    dongle_device_catalog_t catalog;
    uint8_t encoded[DONGLE_SETTINGS_BLOB_SIZE];

    CHECK(dongle_catalog_init(&catalog) == DONGLE_CATALOG_OK);
    catalog.selected_slot = 0U;
    CHECK(dongle_settings_encode(&catalog, encoded, sizeof(encoded)) ==
          DONGLE_SETTINGS_CODEC_INVALID_DATA);
    CHECK(dongle_settings_encode(&catalog, encoded, sizeof(encoded) - 1U) ==
          DONGLE_SETTINGS_CODEC_INVALID_ARGUMENT);
    CHECK(dongle_settings_decode(encoded, sizeof(encoded) - 1U, &catalog) ==
          DONGLE_SETTINGS_CODEC_INVALID_ARGUMENT);
}

int main(void)
{
    test_round_trip_preserves_empty_slot_and_selection();
    test_corrupt_data_is_rejected_without_changing_output();
    test_invalid_catalog_and_buffer_sizes_are_rejected();
    return 0;
}
