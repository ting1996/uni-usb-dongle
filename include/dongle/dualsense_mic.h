#ifndef DONGLE_DUALSENSE_MIC_H
#define DONGLE_DUALSENSE_MIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DONGLE_DUALSENSE_BT_MIC_REPORT_SIZE 142U
#define DONGLE_DUALSENSE_BT_MIC_OPUS_SIZE 71U

/* Builds a signed Bluetooth 0x31 output report enabling or muting the DualSense
 * microphone preamp and mute LED. */
void dongle_dualsense_bt_mic_preamp_report(uint8_t seq, bool enable,
                                            bool headset_mic,
                                            uint8_t bt_out[78]);

/* Builds a signed Bluetooth 0x32 extended output report enabling or disabling
 * DualSense Bluetooth microphone Opus streaming. */
void dongle_dualsense_bt_mic_control_report(uint8_t seq, bool enable,
                                            bool headset_mic,
                                            uint8_t bt_out[DONGLE_DUALSENSE_BT_MIC_REPORT_SIZE]);

/* Extracts an embedded Opus microphone payload from a DualSense Bluetooth HID
 * interrupt input report (either 0x31 bit-1 71-byte Opus packet or >78-byte TLV). */
bool dongle_dualsense_bt_extract_mic_opus(const uint8_t *hid_report, size_t size,
                                          const uint8_t **opus_frame,
                                          size_t *opus_len);

/* Linearly upsamples mono 8 kHz PCM into 16 kHz PCM (2x samples). */
size_t dongle_pcm_upsample_8k_to_16k(const int16_t *in, size_t in_samples,
                                    int16_t *out, size_t out_cap,
                                    int16_t *prev_sample);

#endif
