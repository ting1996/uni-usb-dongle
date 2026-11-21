#ifndef DONGLE_SETTINGS_STORE_H
#define DONGLE_SETTINGS_STORE_H

#include "dongle/device_catalog.h"

#include <FreeRTOS.h>
#include <semphr.h>
#include <stdbool.h>

#include "lfs_port.h"

typedef enum {
    DONGLE_STORE_OK = 0,
    DONGLE_STORE_NOT_FOUND,
    DONGLE_STORE_INVALID_ARGUMENT,
    DONGLE_STORE_IO_ERROR,
    DONGLE_STORE_CORRUPT
} dongle_store_result_t;

typedef struct {
    struct lfs_context filesystem_context;
    struct lfs_config filesystem_config;
    SemaphoreHandle_t mutex;
    lfs_t *filesystem;
    bool initialized;
} dongle_settings_store_t;

dongle_store_result_t dongle_settings_store_init(dongle_settings_store_t *store);

dongle_store_result_t dongle_settings_store_load(
    dongle_settings_store_t *store,
    dongle_device_catalog_t *catalog);

dongle_store_result_t dongle_settings_store_save(
    dongle_settings_store_t *store,
    const dongle_device_catalog_t *catalog);

dongle_store_result_t dongle_settings_store_close(dongle_settings_store_t *store);

#endif
