#include "dongle/gamepad_router.h"

dongle_gamepad_router_result_t dongle_gamepad_router_forward(
    const dongle_device_catalog_t *catalog,
    uint8_t controller_slot,
    const uint8_t *report,
    size_t report_size,
    dongle_gamepad_report_sink_t sink,
    void *sink_context)
{
    if (catalog == NULL || report == NULL || report_size == 0U || sink == NULL) {
        return DONGLE_GAMEPAD_ROUTER_INVALID_ARGUMENT;
    }
    if (controller_slot >= DONGLE_CONTROLLER_SLOT_COUNT) {
        return DONGLE_GAMEPAD_ROUTER_INVALID_ARGUMENT;
    }

    const dongle_controller_slot_t *controller =
        &catalog->controllers[controller_slot];
    if (!controller->paired) {
        return DONGLE_GAMEPAD_ROUTER_SLOT_EMPTY;
    }
    if (controller->usb_gamepad_index != controller_slot) {
        return DONGLE_GAMEPAD_ROUTER_INVALID_MAPPING;
    }
    if (!sink(sink_context, controller->usb_gamepad_index, report, report_size)) {
        return DONGLE_GAMEPAD_ROUTER_OUTPUT_REJECTED;
    }

    return DONGLE_GAMEPAD_ROUTER_OK;
}
