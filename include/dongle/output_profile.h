#ifndef DONGLE_OUTPUT_PROFILE_H
#define DONGLE_OUTPUT_PROFILE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    DONGLE_PROFILE_GENERIC = 0,
    DONGLE_PROFILE_XINPUT = 1,
    DONGLE_PROFILE_PS4 = 2,
    DONGLE_PROFILE_SWITCH = 3,
    DONGLE_PROFILE_DUALSENSE = 4,
} dongle_output_profile_t;
#define DONGLE_PROFILE_COUNT 5U

typedef enum {
    DONGLE_DS_POLL_250HZ = 0,
    DONGLE_DS_POLL_500HZ = 1,
    DONGLE_DS_POLL_750HZ = 2,
} dongle_dualsense_poll_rate_t;
#define DONGLE_DS_POLL_RATE_COUNT 3U
#define DONGLE_OUTPUT_MAX_REPORT 64U
#define DONGLE_DUALSENSE_USB_INPUT_SIZE 64U
#define DONGLE_DUALSENSE_USB_OUTPUT_SIZE 48U
#define DONGLE_DUALSENSE_BT_OUTPUT_SIZE 78U

/* Input is the normalized nine-byte DualSense mapping used by the router.
 * Output includes the protocol's report ID/header. Counter is per USB slot. */
size_t dongle_output_encode(dongle_output_profile_t profile, const uint8_t input[9],
                           uint8_t counter, uint8_t output[64]);
/* Switch USB handshake and subcommands. Unknown subcommands receive a NACK.
 * No motion or rumble capabilities are fabricated by these input profiles. */
size_t dongle_switch_reply(const uint8_t *request, size_t size,
                          const uint8_t input[9], uint8_t counter,
                          const uint8_t address[6], uint8_t output[64]);
#endif
