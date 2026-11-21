#ifndef DONGLE_USB_OTA_SERVICE_H
#define DONGLE_USB_OTA_SERVICE_H

#include <FreeRTOS.h>
#include <stdbool.h>

#include "dongle/device_catalog.h"
#include "dongle/settings_store.h"

BaseType_t dongle_usb_ota_service_start(
    dongle_device_catalog_t *catalog,
    dongle_settings_store_t *settings_store);

bool dongle_usb_ota_active(void);

#endif
