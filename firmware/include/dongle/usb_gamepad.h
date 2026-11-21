#ifndef DONGLE_USB_GAMEPAD_H
#define DONGLE_USB_GAMEPAD_H

#include "FreeRTOS.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DONGLE_USB_GAMEPAD_COUNT 2U
#define DONGLE_USB_GAMEPAD_FIRST_INTERFACE 4U
#define DONGLE_USB_GAMEPAD_REPORT_SIZE 9U
#define DONGLE_USB_GAMEPAD_INPUT_ID 1U
#define DONGLE_USB_GAMEPAD_WIRE_SIZE 10U
#define DONGLE_USB_GAMEPAD_DESCRIPTOR_SIZE 25U
#define DONGLE_USB_GAMEPAD_INPUT_DESCRIPTOR_SIZE 77U
#define DONGLE_USB_MANAGEMENT_DESCRIPTOR_SIZE 23U
#define DONGLE_USB_GAMEPAD_REPORT_DESCRIPTOR_SIZE 100U

/* Six unsigned axes (X,Y,Rx,Ry,Z,Rz), 16 buttons, hat 0..7 / neutral 8.
 * Stick centers are 128, trigger rests are 0. Sink data has no ID prefix;
 * USB input reports are formatted by the active output profile. */
#define DONGLE_USB_GAMEPAD_DESCRIPTOR(interface_number, endpoint, interval) \
    9, 4, interface_number, 0, 1, 3, 0, 0, 0, \
    9, 0x21, 0x11, 0x01, 0, 1, 0x22, \
    DONGLE_USB_GAMEPAD_INPUT_DESCRIPTOR_SIZE, 0, \
    7, 5, endpoint, 3, DONGLE_USB_GAMEPAD_WIRE_SIZE, 0, interval

/* Register after audio interface and before usbd_initialize(). */
BaseType_t dongle_usb_gamepad_register(uint8_t busid);
void dongle_usb_gamepad_attach(uint8_t busid);
uint16_t dongle_usb_gamepad_generic_descriptor(const uint8_t **descriptor);
void dongle_usb_gamepad_event(uint8_t event);

/* Normalized report sink for dongle_gamepad_router_forward(). Copies input. */
bool dongle_usb_gamepad_sink(void *context, uint8_t gamepad_index,
                             const uint8_t *report, size_t report_size);

/* Raw 64-byte DualSense USB input passthrough (report ID 0x01). */
bool dongle_usb_gamepad_dualsense_raw(uint8_t gamepad_index,
                                      const uint8_t report[64]);
void dongle_usb_gamepad_dualsense_clear(uint8_t gamepad_index);

/* Explicit bench diagnostic; automatically releases input after 500 ms. */
bool dongle_usb_gamepad_test(uint8_t gamepad_index, const uint8_t *report,
                             size_t report_size);

/* Flags, current report, queued/completed transfers, current/test-start ticks. */
#define DONGLE_USB_GAMEPAD_DIAGNOSTIC_SIZE 26U
bool dongle_usb_gamepad_diagnostics(uint8_t gamepad_index, uint8_t *payload);

#endif
