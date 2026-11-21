#ifndef DONGLE_GAMEPAD_ROUTER_H
#define DONGLE_GAMEPAD_ROUTER_H

#include "dongle/device_catalog.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    DONGLE_GAMEPAD_ROUTER_OK = 0,
    DONGLE_GAMEPAD_ROUTER_INVALID_ARGUMENT,
    DONGLE_GAMEPAD_ROUTER_SLOT_EMPTY,
    DONGLE_GAMEPAD_ROUTER_INVALID_MAPPING,
    DONGLE_GAMEPAD_ROUTER_OUTPUT_REJECTED
} dongle_gamepad_router_result_t;

typedef bool (*dongle_gamepad_report_sink_t)(
    void *context,
    uint8_t gamepad_index,
    const uint8_t *report,
    size_t report_size);

/* The sink must consume or copy the borrowed report before returning. */
dongle_gamepad_router_result_t dongle_gamepad_router_forward(
    const dongle_device_catalog_t *catalog,
    uint8_t controller_slot,
    const uint8_t *report,
    size_t report_size,
    dongle_gamepad_report_sink_t sink,
    void *sink_context);

#endif
