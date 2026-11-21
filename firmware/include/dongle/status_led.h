#ifndef DONGLE_STATUS_LED_H
#define DONGLE_STATUS_LED_H

#include <FreeRTOS.h>
#include <stdbool.h>

BaseType_t dongle_status_led_start(void);
BaseType_t dongle_status_led_set_ota(bool in_progress);

#endif
