#ifndef DONGLE_USB_AUDIO_H
#define DONGLE_USB_AUDIO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "usbd_core.h"
#include "usbd_audio.h"
#define DONGLE_AUDIO_DESCRIPTOR_SIZE \
    (AUDIO_AC_DESCRIPTOR_LEN(2) + 12 + 9 + 12 + 9 + 9 + \
     AUDIO_AS_DESCRIPTOR_LEN(3) + AUDIO_AS_DESCRIPTOR_LEN(1))
#define DONGLE_AUDIO_DESCRIPTOR(interval) \
    AUDIO_AC_DESCRIPTOR_INIT(0, 3, 61, 0, 1, 2), \
    AUDIO_AC_INPUT_TERMINAL_DESCRIPTOR_INIT(1, AUDIO_TERMINAL_STREAMING, 2, 3), \
    AUDIO_AC_OUTPUT_TERMINAL_DESCRIPTOR_INIT(2, AUDIO_OUTTERM_SPEAKER, 1), \
    AUDIO_AC_INPUT_TERMINAL_DESCRIPTOR_INIT(3, AUDIO_INTERM_MIC, 1, AUDIO_CHANNEL_M), \
    AUDIO_AC_FEATURE_UNIT_DESCRIPTOR_INIT(4, 3, 1, 0x03, 0x00), \
    AUDIO_AC_OUTPUT_TERMINAL_DESCRIPTOR_INIT(5, AUDIO_TERMINAL_STREAMING, 4), \
    AUDIO_AS_DESCRIPTOR_INIT(1, 1, 2, 2, 16, 0x03, 0x09, 388, interval, AUDIO_SAMPLE_FREQ_3B(44100), AUDIO_SAMPLE_FREQ_3B(48000), AUDIO_SAMPLE_FREQ_3B(96000)), \
    AUDIO_AS_DESCRIPTOR_INIT(2, 5, 1, 2, 16, 0x84, 0x05, 32, interval, AUDIO_SAMPLE_FREQ_3B(16000))
#define DONGLE_DUALSENSE_AUDIO_DESCRIPTOR_SIZE \
    (AUDIO_AC_DESCRIPTOR_LEN(2) + 12 + 12 + 9 + 12 + 9 + 9 + \
     AUDIO_AS_DESCRIPTOR_LEN(1) + AUDIO_AS_DESCRIPTOR_LEN(1))
#define DONGLE_DUALSENSE_AUDIO_DESCRIPTOR(interval, out_ep) \
    AUDIO_AC_DESCRIPTOR_INIT(0, 3, 73, 2, 1, 2), \
    AUDIO_AC_INPUT_TERMINAL_DESCRIPTOR_INIT(1, AUDIO_TERMINAL_STREAMING, 4, 0x0033), \
    AUDIO_AC_FEATURE_UNIT_DESCRIPTOR_INIT(2, 1, 1, 0x03, 0x00, 0x00, 0x00, 0x00), \
    AUDIO_AC_OUTPUT_TERMINAL_DESCRIPTOR_INIT(3, AUDIO_OUTTERM_SPEAKER, 2), \
    AUDIO_AC_INPUT_TERMINAL_DESCRIPTOR_INIT(4, 0x0402, 1, AUDIO_CHANNEL_M), \
    AUDIO_AC_FEATURE_UNIT_DESCRIPTOR_INIT(5, 4, 1, 0x03, 0x00), \
    AUDIO_AC_OUTPUT_TERMINAL_DESCRIPTOR_INIT(6, AUDIO_TERMINAL_STREAMING, 5), \
    AUDIO_AS_DESCRIPTOR_INIT(1, 1, 4, 2, 16, out_ep, 0x09, 392, interval, AUDIO_SAMPLE_FREQ_3B(48000)), \
    AUDIO_AS_DESCRIPTOR_INIT(2, 6, 1, 2, 16, 0x84, 0x05, 32, interval, AUDIO_SAMPLE_FREQ_3B(16000))
#define DONGLE_HEADPHONE_AUDIO_DESCRIPTOR_SIZE \
    (AUDIO_AC_DESCRIPTOR_LEN(1) + 12 + 9 + AUDIO_AS_DESCRIPTOR_LEN(3))
#define DONGLE_HEADPHONE_AUDIO_DESCRIPTOR(ac_intf, as_intf, interval) \
    AUDIO_AC_DESCRIPTOR_INIT(ac_intf, 2, 30, 4, as_intf), \
    AUDIO_AC_INPUT_TERMINAL_DESCRIPTOR_INIT(11, AUDIO_TERMINAL_STREAMING, 2, 3), \
    AUDIO_AC_OUTPUT_TERMINAL_DESCRIPTOR_INIT(12, AUDIO_OUTTERM_SPEAKER, 11), \
    AUDIO_AS_DESCRIPTOR_INIT(as_intf, 11, 2, 2, 16, 0x03, 0x09, 388, interval, AUDIO_SAMPLE_FREQ_3B(44100), AUDIO_SAMPLE_FREQ_3B(48000), AUDIO_SAMPLE_FREQ_3B(96000))
void dongle_usb_audio_streaming(bool active, uint8_t codec, uint32_t bt_sample_rate_hz, uint16_t media_mtu);
bool dongle_usb_audio_is_streaming(void);
bool dongle_usb_audio_playback_open(void);
bool dongle_usb_audio_mic_open(void);
size_t dongle_usb_audio_mic_write(const uint8_t *data, size_t size);
void dongle_usb_audio_diagnostics(uint8_t out[20]);
void dongle_usb_audio_metrics(uint16_t *out_peak_l, uint16_t *out_peak_r, uint32_t *out_nonzero_packets);
void dongle_usb_audio_mic_metrics(bool *open, bool *muted, uint16_t *peak, uint32_t *sent_bytes, uint32_t *errors);
int dongle_usb_audio_register(uint8_t busid);
void dongle_usb_audio_attach(uint8_t busid);
void dongle_usb_audio_attach_headphone(uint8_t busid);
void dongle_usb_audio_set_rumble_gain(uint16_t gain_pct);
uint16_t dongle_usb_audio_get_rumble_gain(void);
void dongle_usb_audio_event(uint8_t event);
void dongle_usb_audio_set_ota_suspended(bool suspended);
bool dongle_usb_audio_ota_is_suspended(void);
#endif
