#ifndef DONGLE_BLUETOOTH_H
#define DONGLE_BLUETOOTH_H
#include "dongle/settings_store.h"
#include <stddef.h>
#include <stdint.h>
/* All catalog access uses the same mutex as the USB protocol service. */
int dongle_bluetooth_start(dongle_device_catalog_t *catalog,
    dongle_settings_store_t *store, SemaphoreHandle_t catalog_mutex);
size_t dongle_bluetooth_command(uint8_t command, const uint8_t *data,
    size_t size, uint8_t response[44]);
uint8_t dongle_bluetooth_connected(void);
/* Bits 0/1: controller slots; bit 2: authenticated headphone ACL. */
uint8_t dongle_bluetooth_connected_devices(void);
bool dongle_bluetooth_ready(void);
void dongle_bluetooth_removed(const uint8_t address[6]);
void dongle_bluetooth_storage_bind(dongle_settings_store_t *store);
void dongle_bluetooth_audio_metrics(uint8_t out[12]);
void dongle_bluetooth_local_address(uint8_t out[6]);
size_t dongle_bluetooth_dualsense_feature(
    uint8_t slot, uint8_t report_id, uint8_t *out, size_t capacity);
void dongle_bluetooth_send_dualsense_output(
    uint8_t slot, const uint8_t *usb_report, size_t size);
bool dongle_bluetooth_send_dualsense_feature(
    uint8_t slot, const uint8_t *usb_report, size_t size);
bool dongle_bluetooth_dualsense_metrics(uint8_t slot, uint8_t out[30]);
#endif
