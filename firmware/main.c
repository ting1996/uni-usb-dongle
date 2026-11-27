#include "FreeRTOS.h"
#include "task.h"

#include "board.h"
#include "dongle/device_catalog.h"
#include "dongle/settings_store.h"
#include "dongle/usb_ota_service.h"
#ifdef CONFIG_M62_STATUS_LEDS
#include "dongle/status_led.h"
#endif
#define DBG_TAG "MAIN"
#include "log.h"

#define APP_TASK_STACK_WORDS 2048U
#define APP_TASK_PRIORITY (tskIDLE_PRIORITY + 1U)

static dongle_settings_store_t settings_store;
static dongle_device_catalog_t device_catalog;

static void dongle_app_task(void *argument)
{
    (void)argument;
    if (dongle_catalog_init(&device_catalog) != DONGLE_CATALOG_OK) {
        LOG_E("Unable to initialize device catalog\r\n");
        vTaskDelete(NULL);
        return;
    }

#ifdef CONFIG_M62_STATUS_LEDS
    if (dongle_status_led_start() != pdPASS) {
        LOG_E("Unable to initialize M62 status LEDs\r\n");
    }
#endif

    dongle_store_result_t result = dongle_settings_store_init(&settings_store);
    if (result == DONGLE_STORE_OK) {
        result = dongle_settings_store_load(&settings_store, &device_catalog);
        if (result == DONGLE_STORE_NOT_FOUND) {
            LOG_I("No saved device settings; using defaults\r\n");
        } else if (result != DONGLE_STORE_OK) {
            LOG_E("Unable to load device settings: %d\r\n", result);
        }
    } else {
        LOG_E("Unable to initialize settings storage: %d\r\n", result);
    }

    if (dongle_usb_ota_service_start(&device_catalog, &settings_store) !=
        pdPASS) {
        LOG_E("Unable to initialize native USB OTA service\r\n");
    }

    LOG_I("Dongle firmware scaffold running with FreeRTOS\r\n");
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

int main(void)
{
    board_init();

    if (xTaskCreate(dongle_app_task, "dongle", APP_TASK_STACK_WORDS, NULL,
                    APP_TASK_PRIORITY, NULL) != pdPASS) {
        LOG_E("Unable to create dongle task\r\n");
        return 1;
    }

    vTaskStartScheduler();
    LOG_E("FreeRTOS scheduler returned\r\n");
    return 1;
}

void vApplicationIdleHook(void)
{
    __asm volatile("wfi");
}
