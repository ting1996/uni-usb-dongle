#ifndef DONGLE_DUALSENSE_H
#define DONGLE_DUALSENSE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* HID report bytes, excluding the Bluetooth HIDP DATA header. */
bool dongle_dualsense_input(const uint8_t *data, size_t size, uint8_t output[9]);
/* Converts DualSense Bluetooth HID input (0x31 78B or basic 0x01 10B) into a
 * 64-byte DualSense USB input report (report ID 0x01 + 63-byte payload). */
bool dongle_dualsense_bt_to_usb_input(const uint8_t *data, size_t size,
                                      uint8_t usb_out[64]);
/* Translates a DualSense USB output report (ID 0x02, >=48B) into a signed
 * 78-byte Bluetooth output report (ID 0x31, seq/tag, 0xA2-seeded CRC-32). */
bool dongle_dualsense_usb_to_bt_output(const uint8_t *usb_report, size_t size,
                                       uint8_t seq, uint8_t bt_out[78]);
/* Returns the USB/BT HID feature report length for known DualSense IDs, or 0. */
size_t dongle_dualsense_feature_size(uint8_t report_id);
/* Validates a Bluetooth 0xA3-seeded CRC-32 feature payload and strips the CRC
 * into a zero-padded USB feature report buffer. */
bool dongle_dualsense_bt_feature_to_usb(const uint8_t *bt_data, size_t bt_size,
                                      uint8_t *usb_out, size_t usb_cap,
                                      size_t *usb_len);
/* Copies a USB feature report payload and appends a 0x53-seeded LE32 CRC-32
 * for transmission over Bluetooth HID Control SET_REPORT (0x53). */
size_t dongle_dualsense_usb_to_bt_feature(const uint8_t *usb_report,
                                         size_t usb_size, uint8_t *bt_out,
                                         size_t bt_cap);
/* Builds a Bluetooth 0x31 output report unlocking host lightbar control. */
void dongle_dualsense_bt_unlock_leds(uint8_t seq, uint8_t bt_out[78]);
/* Merges src into a pending 48-byte USB 0x02 output report by valid flags so
 * rapid sequential updates (e.g. triggers followed by rumble) do not drop fields. */
void dongle_dualsense_merge_usb_output(uint8_t dst[48], const uint8_t src[48]);
#endif
