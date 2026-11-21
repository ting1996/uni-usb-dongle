#include "dongle/gamepad_router.h"

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

typedef struct {
    uint8_t gamepad_index;
    uint8_t report[8];
    size_t report_size;
    size_t calls;
    bool accept;
} report_capture_t;

static bool capture_report(
    void *context,
    uint8_t gamepad_index,
    const uint8_t *report,
    size_t report_size)
{
    report_capture_t *capture = context;
    if (!capture->accept || report_size > sizeof(capture->report)) {
        return false;
    }

    capture->gamepad_index = gamepad_index;
    capture->report_size = report_size;
    memcpy(capture->report, report, report_size);
    ++capture->calls;
    return true;
}

static void pair_two_controllers(dongle_device_catalog_t *catalog)
{
    const uint8_t first_address[DONGLE_BLUETOOTH_ADDRESS_SIZE] = { 1, 2, 3, 4, 5, 6 };
    const uint8_t second_address[DONGLE_BLUETOOTH_ADDRESS_SIZE] = { 7, 8, 9, 10, 11, 12 };
    uint8_t slot;

    CHECK(dongle_catalog_init(catalog) == DONGLE_CATALOG_OK);
    CHECK(dongle_catalog_pair_controller(catalog, first_address, &slot) ==
          DONGLE_CATALOG_OK);
    CHECK(dongle_catalog_pair_controller(catalog, second_address, &slot) ==
          DONGLE_CATALOG_OK);
}

static void test_reports_route_to_stable_indexes_independent_of_selection(void)
{
    dongle_device_catalog_t catalog;
    const uint8_t first_report[] = { 0x01, 0x10, 0x20, 0x30 };
    const uint8_t second_report[] = { 0x02, 0x40, 0x50, 0x60 };
    report_capture_t first_capture = { .accept = true };
    report_capture_t second_capture = { .accept = true };

    pair_two_controllers(&catalog);
    CHECK(catalog.selected_slot == DONGLE_NO_SELECTED_SLOT);
    CHECK(dongle_gamepad_router_forward(&catalog, 0U, first_report,
                                        sizeof(first_report), capture_report,
                                        &first_capture) ==
          DONGLE_GAMEPAD_ROUTER_OK);
    CHECK(dongle_gamepad_router_forward(&catalog, 1U, second_report,
                                        sizeof(second_report), capture_report,
                                        &second_capture) ==
          DONGLE_GAMEPAD_ROUTER_OK);

    CHECK(first_capture.gamepad_index == 0U);
    CHECK(second_capture.gamepad_index == 1U);
    CHECK(first_capture.gamepad_index != second_capture.gamepad_index);
    CHECK(first_capture.report_size == sizeof(first_report));
    CHECK(second_capture.report_size == sizeof(second_report));
    CHECK(memcmp(first_capture.report, first_report, sizeof(first_report)) == 0);
    CHECK(memcmp(second_capture.report, second_report, sizeof(second_report)) == 0);
}

static void test_unpaired_or_misconfigured_slots_are_not_forwarded(void)
{
    dongle_device_catalog_t catalog;
    const uint8_t report[] = { 0x01, 0x02 };
    report_capture_t capture = { .accept = true };

    CHECK(dongle_catalog_init(&catalog) == DONGLE_CATALOG_OK);
    CHECK(dongle_gamepad_router_forward(&catalog, 0U, report, sizeof(report),
                                        capture_report, &capture) ==
          DONGLE_GAMEPAD_ROUTER_SLOT_EMPTY);
    CHECK(capture.calls == 0U);

    pair_two_controllers(&catalog);
    catalog.controllers[1].usb_gamepad_index = 0U;
    CHECK(dongle_gamepad_router_forward(&catalog, 1U, report, sizeof(report),
                                        capture_report, &capture) ==
          DONGLE_GAMEPAD_ROUTER_INVALID_MAPPING);
    CHECK(capture.calls == 0U);
}

static void test_invalid_arguments_and_rejected_output_are_reported(void)
{
    dongle_device_catalog_t catalog;
    const uint8_t address[DONGLE_BLUETOOTH_ADDRESS_SIZE] = { 1, 2, 3, 4, 5, 6 };
    const uint8_t report[] = { 0x01 };
    uint8_t slot;
    report_capture_t capture = { .accept = false };

    CHECK(dongle_catalog_init(&catalog) == DONGLE_CATALOG_OK);
    CHECK(dongle_catalog_pair_controller(&catalog, address, &slot) ==
          DONGLE_CATALOG_OK);
    CHECK(dongle_gamepad_router_forward(NULL, slot, report, sizeof(report),
                                        capture_report, &capture) ==
          DONGLE_GAMEPAD_ROUTER_INVALID_ARGUMENT);
    CHECK(dongle_gamepad_router_forward(&catalog, slot, NULL, sizeof(report),
                                        capture_report, &capture) ==
          DONGLE_GAMEPAD_ROUTER_INVALID_ARGUMENT);
    CHECK(dongle_gamepad_router_forward(&catalog, slot, report, 0U,
                                        capture_report, &capture) ==
          DONGLE_GAMEPAD_ROUTER_INVALID_ARGUMENT);
    CHECK(dongle_gamepad_router_forward(&catalog, slot, report, sizeof(report),
                                        NULL, &capture) ==
          DONGLE_GAMEPAD_ROUTER_INVALID_ARGUMENT);
    CHECK(dongle_gamepad_router_forward(&catalog, slot, report, sizeof(report),
                                        capture_report, &capture) ==
          DONGLE_GAMEPAD_ROUTER_OUTPUT_REJECTED);
    CHECK(capture.calls == 0U);
}

int main(void)
{
    test_reports_route_to_stable_indexes_independent_of_selection();
    test_unpaired_or_misconfigured_slots_are_not_forwarded();
    test_invalid_arguments_and_rejected_output_are_reported();
    return 0;
}
