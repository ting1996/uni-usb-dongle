#ifndef DONGLE_MIC_PIPELINE_H
#define DONGLE_MIC_PIPELINE_H

#include "dongle/usb_ota_protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void dongle_mic_pipeline_init(uint8_t saved_source);
bool dongle_mic_pipeline_set_source(uint8_t source);
uint8_t dongle_mic_pipeline_get_source(void);
void dongle_mic_pipeline_set_hfp_state(bool slc_ready, bool sco_open, bool codec_msbc);
void dongle_mic_pipeline_set_ds_connected(uint8_t connected_mask);
bool dongle_mic_pipeline_desired_hfp_sco(void);
int8_t dongle_mic_pipeline_desired_ds_slot(void);
void dongle_mic_pipeline_feed_dualsense_opus(uint8_t slot, const uint8_t *opus, size_t len);
size_t dongle_mic_pipeline_metrics(uint8_t out[DONGLE_MIC_STATE_PAYLOAD_SIZE]);

#endif
