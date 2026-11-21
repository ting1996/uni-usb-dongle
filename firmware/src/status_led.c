#include "dongle/status_led.h"
#include "dongle/bluetooth.h"

#include "bflb_core.h"
#include "bflb_gpio.h"
#include "bflb_pwm_v2.h"
#include "bflb_clock.h"
#include "task.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define STATUS_LED_TASK_STACK_WORDS 512U
#define STATUS_LED_TASK_PRIORITY (tskIDLE_PRIORITY + 1U)
#define STATUS_LED_OTA_ACTIVE 1U
#define STATUS_LED_OTA_INTERVAL_MS 150U
#define STATUS_LED_OTA_PHASE_COUNT 6U
#define STATUS_LED_POLL_MS 50U
#define STATUS_LED_PWM_PERIOD 1000U

#define STATUS_LED_BLUE_PIN GPIO_PIN_0
#define STATUS_LED_RED_PIN GPIO_PIN_1
#define STATUS_LED_GREEN_PIN GPIO_PIN_30
#define STATUS_LED_WHITE_PIN GPIO_PIN_29

static struct bflb_device_s *gpio_device;
static TaskHandle_t status_led_task_handle;

/* BL616 differential PWM routing: GPIO0/1 are CH0 P/N, GPIO30 is CH3 P.
 * Both CH0 outputs have 50% duty, with independent GPIO muxes for on/off. */
static void set_rgb_mask(uint8_t mask)
{
    const uint8_t pins[] = { STATUS_LED_RED_PIN, STATUS_LED_GREEN_PIN,
                             STATUS_LED_BLUE_PIN };
    for (unsigned i = 0; i < 3; ++i) {
        bflb_gpio_reset(gpio_device, pins[i]);
        bflb_gpio_init(gpio_device, pins[i], GPIO_PULLDOWN | GPIO_SMT_EN | GPIO_DRV_1 |
            ((mask & (1U << i)) ? GPIO_FUNC_PWM0 | GPIO_ALTERNATE : GPIO_OUTPUT));
    }
}

static void status_led_task(void *argument)
{
    (void)argument;
    bool ota_in_progress = false;
    uint8_t phase = 0U, shown = 0xff, previous_connections = 0xff;
    TickType_t phase_started = xTaskGetTickCount();

    for (;;) {
        uint32_t notification = 0U;
        if (xTaskNotifyWait(0U, UINT32_MAX, &notification,
                            pdMS_TO_TICKS(STATUS_LED_POLL_MS)) == pdTRUE) {
            ota_in_progress = notification == STATUS_LED_OTA_ACTIVE;
            phase = 0U;
            phase_started = xTaskGetTickCount();
        }
        const TickType_t now = xTaskGetTickCount();
        if (ota_in_progress && now - phase_started >= pdMS_TO_TICKS(STATUS_LED_OTA_INTERVAL_MS)) {
            phase = (uint8_t)((phase + 1U) % STATUS_LED_OTA_PHASE_COUNT);
            phase_started = now;
        }
        const uint8_t connections = dongle_bluetooth_connected_devices();
        if (connections != previous_connections) {
            printf("[LED] connected mask=%02x (R=controller1 G=controller2 B=headphone) duty=50%%\r\n", connections);
            previous_connections = connections;
        }
        const uint8_t mask = ota_in_progress ?
            ((phase & 1U) ? 0U : (1U << (phase / 2U))) : connections;
        if (mask != shown) { set_rgb_mask(mask); shown = mask; }
    }
}

BaseType_t dongle_status_led_start(void)
{
    if (status_led_task_handle != NULL) return pdPASS;

    gpio_device = bflb_device_get_by_name("gpio");
    struct bflb_device_s *pwm = bflb_device_get_by_name("pwm_v2_0");
    if (gpio_device == NULL || pwm == NULL) return pdFAIL;

    bflb_gpio_init(gpio_device, STATUS_LED_WHITE_PIN,
                    GPIO_OUTPUT | GPIO_PULLDOWN | GPIO_SMT_EN | GPIO_DRV_1);
    bflb_gpio_set(gpio_device, STATUS_LED_WHITE_PIN);
    set_rgb_mask(0);
    const struct bflb_pwm_v2_config_s config = {
        .clk_source = BFLB_SYSTEM_PBCLK, .clk_div = 80,
        .period = STATUS_LED_PWM_PERIOD,
    };
    bflb_pwm_v2_feature_control(pwm, PWM_CMD_IO_SEL, PWM_IO_SEL_DIFF_END);
    bflb_pwm_v2_init(pwm, &config);
    bflb_pwm_v2_channel_set_threshold(pwm, PWM_CH0, 0, STATUS_LED_PWM_PERIOD / 2);
    bflb_pwm_v2_channel_set_threshold(pwm, PWM_CH3, 0, STATUS_LED_PWM_PERIOD / 2);
    bflb_pwm_v2_channel_positive_start(pwm, PWM_CH0);
    bflb_pwm_v2_channel_negative_start(pwm, PWM_CH0);
    bflb_pwm_v2_channel_positive_start(pwm, PWM_CH3);
    bflb_pwm_v2_start(pwm);
    for (unsigned channel = 0; channel <= 3; channel += 3) {
        uint32_t delta, period;
        bflb_pwm_v2_get_duty(pwm, channel, &delta, &period);
        printf("[LED] PWM channel=%u duty=%lu/%lu frequency=%lu Hz\r\n", channel,
                (unsigned long)delta, (unsigned long)period,
                (unsigned long)bflb_pwm_v2_get_frequency(pwm));
    }

    if (xTaskCreate(status_led_task, "status_led", STATUS_LED_TASK_STACK_WORDS,
                    NULL, STATUS_LED_TASK_PRIORITY, &status_led_task_handle) != pdPASS) {
        status_led_task_handle = NULL;
        bflb_gpio_reset(gpio_device, STATUS_LED_WHITE_PIN);
        return pdFAIL;
    }
    return pdPASS;
}

BaseType_t dongle_status_led_set_ota(bool in_progress)
{
    if (status_led_task_handle == NULL) return pdFAIL;
    return xTaskNotify(status_led_task_handle,
                       in_progress ? STATUS_LED_OTA_ACTIVE : 0U,
                       eSetValueWithOverwrite);
}
