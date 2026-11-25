#ifndef DONGLE_A2DP_SOURCE_H
#define DONGLE_A2DP_SOURCE_H

#include "dongle/a2dp_codec.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DONGLE_A2DP_MAX_REMOTE_SEPS 16U
#define DONGLE_A2DP_MAX_CODEC_CAP_LEN 16U

/* AVDTP 1.3 Signal Identifiers */
#define AVDTP_SIG_DISCOVER             0x01U
#define AVDTP_SIG_GET_CAPABILITIES     0x02U
#define AVDTP_SIG_SET_CONFIGURATION    0x03U
#define AVDTP_SIG_GET_CONFIGURATION    0x04U
#define AVDTP_SIG_RECONFIGURE          0x05U
#define AVDTP_SIG_OPEN                 0x06U
#define AVDTP_SIG_START                0x07U
#define AVDTP_SIG_CLOSE                0x08U
#define AVDTP_SIG_SUSPEND              0x09U
#define AVDTP_SIG_ABORT                0x0AU
#define AVDTP_SIG_GET_ALL_CAPABILITIES 0x0CU
#define AVDTP_SIG_DELAYREPORT          0x0DU

typedef enum {
    DONGLE_A2DP_STATE_IDLE = 0,
    DONGLE_A2DP_STATE_DISCOVERING,
    DONGLE_A2DP_STATE_GET_CAPS,
    DONGLE_A2DP_STATE_CONFIGURING,
    DONGLE_A2DP_STATE_OPENING,
    DONGLE_A2DP_STATE_OPEN_MEDIA,
    DONGLE_A2DP_STATE_OPEN,
    DONGLE_A2DP_STATE_STARTING,
    DONGLE_A2DP_STATE_STREAMING,
    DONGLE_A2DP_STATE_SUSPENDING,
    DONGLE_A2DP_STATE_CLOSING,
    DONGLE_A2DP_STATE_ABORTING
} dongle_a2dp_stream_state_t;

typedef struct {
    uint8_t seid;
    uint8_t media_type;
    uint8_t tsep; /* 0 = Source, 1 = Sink */
    bool in_use;
    bool caps_valid;
    uint8_t codec_type; /* 0x00 SBC, 0x02 AAC, 0xFF Vendor */
    uint8_t codec_info[DONGLE_A2DP_MAX_CODEC_CAP_LEN];
    uint8_t codec_info_len;
    dongle_audio_codec_t resolved_codec;
} dongle_a2dp_remote_sep_t;

typedef struct {
    int (*send_signal)(void *user, const uint8_t *data, size_t len);
    int (*open_media)(void *user);
    void (*close_media)(void *user);
    void (*on_state)(void *user, dongle_a2dp_stream_state_t state,
                     uint8_t active_codec, uint32_t sample_rate_hz);
    void (*on_error)(void *user, uint8_t signal_id, uint8_t error_code);
} dongle_a2dp_source_callbacks_t;

typedef struct {
    dongle_a2dp_source_callbacks_t cb;
    void *user;

    dongle_a2dp_stream_state_t state;
    uint8_t tx_label;
    uint8_t pending_signal;
    uint8_t pending_label;

    dongle_audio_codec_t preferred_codec;
    uint32_t usb_sample_rate_hz;

    dongle_a2dp_remote_sep_t remote_seps[DONGLE_A2DP_MAX_REMOTE_SEPS];
    uint8_t remote_sep_count;
    uint8_t caps_index;

    uint8_t local_seid;
    uint8_t remote_seid;
    dongle_audio_codec_t active_codec;
    uint32_t active_sample_rate_hz;
    uint8_t active_config[DONGLE_A2DP_MAX_CODEC_CAP_LEN];
    uint8_t active_config_len;

    bool media_connected;
    bool start_after_open;
    bool reconfiguring;
    uint16_t delay_ms_tenths;
} dongle_a2dp_source_t;

void dongle_a2dp_source_init(dongle_a2dp_source_t *ctx,
                              const dongle_a2dp_source_callbacks_t *cb,
                              void *user);

void dongle_a2dp_source_reset(dongle_a2dp_source_t *ctx);

void dongle_a2dp_source_set_preferences(dongle_a2dp_source_t *ctx,
                                         dongle_audio_codec_t preferred_codec,
                                         uint32_t usb_sample_rate_hz);

/* Start initiator flow: DISCOVER -> GET_ALL_CAPABILITIES -> SET_CONFIGURATION -> OPEN. */
int dongle_a2dp_source_connect(dongle_a2dp_source_t *ctx, bool start_after_open);

/* Called by transport when the AVDTP media L2CAP channel finishes connecting. */
int dongle_a2dp_source_media_connected(dongle_a2dp_source_t *ctx);

/* Stream control */
int dongle_a2dp_source_start(dongle_a2dp_source_t *ctx);
int dongle_a2dp_source_suspend(dongle_a2dp_source_t *ctx);
int dongle_a2dp_source_close(dongle_a2dp_source_t *ctx);
int dongle_a2dp_source_reconfigure(dongle_a2dp_source_t *ctx,
                                   dongle_audio_codec_t preferred_codec,
                                   uint32_t usb_sample_rate_hz);

/* Feed an incoming AVDTP signaling PDU from L2CAP PSM 0x0019. */
void dongle_a2dp_source_recv_signal(dongle_a2dp_source_t *ctx,
                                    const uint8_t *data,
                                    size_t len);

/* Build A2DP Source local capability bytes for a codec (for testing/inspection). */
size_t dongle_a2dp_source_local_codec_caps(dongle_audio_codec_t codec,
                                           uint8_t *out,
                                           size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
