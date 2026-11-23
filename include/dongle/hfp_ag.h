#ifndef DONGLE_HFP_AG_H
#define DONGLE_HFP_AG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    DONGLE_HFP_AG_ACTION_NONE = 0,
    DONGLE_HFP_AG_ACTION_SLC_READY = 1,
    DONGLE_HFP_AG_ACTION_OPEN_SCO_CVSD = 2,
    DONGLE_HFP_AG_ACTION_OPEN_SCO_MSBC = 3
} dongle_hfp_ag_action_t;

typedef void (*dongle_hfp_ag_tx_fn)(void *context, const uint8_t *data, size_t size);

typedef struct {
    bool slc_ready;
    bool bcs_pending;
    bool line_overflow;
    uint16_t hf_features;
    uint8_t hf_codec_mask;
    uint8_t selected_codec;
    uint8_t hf_vgm;
    uint8_t hf_vgs;
    size_t rx_len;
    char rx_line[128];
} dongle_hfp_ag_t;

void dongle_hfp_ag_init(dongle_hfp_ag_t *ag);

dongle_hfp_ag_action_t dongle_hfp_ag_feed(
    dongle_hfp_ag_t *ag,
    const uint8_t *data,
    size_t size,
    dongle_hfp_ag_tx_fn tx_fn,
    void *context);

dongle_hfp_ag_action_t dongle_hfp_ag_request_codec(
    dongle_hfp_ag_t *ag,
    dongle_hfp_ag_tx_fn tx_fn,
    void *context);

#endif
