#include "dongle/bluetooth.h"
#include "dongle/dualsense.h"
#include "dongle/dualsense_mic.h"
#include "dongle/hfp_ag.h"
#include "dongle/mic_pipeline.h"
#include "dongle/output_profile.h"
#include "dongle/usb_gamepad.h"
#include "dongle/usb_audio.h"
#include "dongle/audio_stream.h"
#include "dongle/usb_ota_protocol.h"
#include "dongle/usb_ota_service.h"
#include "task.h"
#include "queue.h"
#include "portmacro.h"
#include "bluetooth.h"
#include "bt_uuid.h"
#include "conn.h"
#include "conn_internal.h"
#include "hci_host.h"
#include "hci_driver.h"
#include "hci_core.h"
#include "keys.h"
#include "l2cap.h"
#include "l2cap_internal.h"
#include <misc/byteorder.h>
#include "a2dp.h"
#include "sdp.h"
#include "rfcomm.h"
#include "sbc/enc/sbc_encoder.h"
#ifdef CONFIG_BT_AVRCP
#include "avrcp.h"
#endif
#include "btble_lib_api.h"
#include "rfparam_adapter.h"
#include "bl616_glb.h"
#include "bflb_efuse.h"
#include "easyflash.h"
#include <string.h>
#include <errno.h>
#include <stdio.h>

#define PEERS 2
#define FOUND 10
#define HID_MTU 672
/* The SDK's buffer registry references these application pools by name. */
struct net_buf_pool hid_tx_pool;
struct net_buf_pool hid_rx_pool;
typedef struct {
    struct bt_conn *conn;
    bt_addr_t address;
    struct bt_l2cap_br_chan control, interrupt;
    uint8_t role; /* 0 controller; 1 A2DP headphone */
    uint8_t slot;
    bool security_pending, authenticated, profile_pending, outgoing, connecting;
    bool control_open, interrupt_open, opening, saved, input_seen, input_rejected;
    bool audio_connected, audio_start_requested, reconnect;
    /* Central-role acquisition: 0 done, 1 first request due, 2 first request
     * in flight, 3 retry due after authentication, 4 retry in flight. */
    uint8_t role_stage;
    TickType_t role_deadline;
    TickType_t retry_at;
    TickType_t audio_poll_at;
    uint32_t rtp_samples, rtp_packets;
    uint8_t ds_feat_05[41];
    uint8_t ds_feat_08[48];
    uint8_t ds_feat_09[20];
    uint8_t ds_feat_20[64];
    uint8_t ds_feat_81[64];
    bool ds_feat_05_valid, ds_feat_08_valid, ds_feat_09_valid;
    bool ds_feat_20_valid, ds_feat_81_valid, ds_led_unlocked, ds_80_await_handshake;
    uint8_t ds_probe_stage, ds_probe_retries, ds_pending_get_id, ds_80_repoll_left, ds_feat_logged;
    TickType_t ds_probe_at, ds_80_poll_at;
} peer_t;
typedef struct { uint8_t command, address[6]; } operation_t;
static peer_t peers[PEERS];
static uint8_t ds_out_buf[2][DONGLE_DUALSENSE_USB_OUTPUT_SIZE];
static uint8_t ds_out_len[2];
static bool ds_out_pending[2];
static uint8_t ds_out_seq[2];
static uint8_t ds_out_path[2];
static uint32_t ds_out_gen[2];
static TickType_t ds_out_retry_at[2];
static bool ds_mic_enabled[2];
static bool ds_mic_unmuted[2];
/* Last USB output report forwarded over Bluetooth, for coalescing. Hosts can
 * resend identical rumble/LED output every USB frame; each forwarded report
 * costs controller airtime and ACL buffers shared with the A2DP link. */
static uint8_t ds_out_last[2][DONGLE_DUALSENSE_USB_OUTPUT_SIZE];
static uint8_t ds_out_last_len[2];
static TickType_t ds_out_last_at[2];
#define DS_OUT_MIN_INTERVAL_MS 8U
#define DS_OUT_MIN_INTERVAL_AUDIO_MS 16U
#define DS_OUT_REFRESH_MS 1000U
static uint8_t ds_feat_tx_buf[2][64];
static uint8_t ds_feat_tx_len[2];
static volatile bool ds_feat_tx_pending[2];
static volatile uint8_t ds_feat_get_req[2];
static uint8_t ds_feat_last_80[2][2];
static volatile bool ds_feat_81_busy[2];
static TickType_t ds_feat_81_deadline[2];
typedef struct {
    uint32_t bt_in_31, bt_in_01, usb_out_02, bt_out_31_ok, bt_out_31_err;
    uint16_t bt_feat_set_ok, bt_feat_get_ok;
    uint8_t last_feat_id, last_handshake;
    uint8_t last_valid_flag0, last_valid_flag1;
    uint8_t last_motor_right, last_motor_left;
    uint8_t last_right_trigger_mode, last_left_trigger_mode;
    uint8_t last_out_path_flags;
} ds_slot_stats_t;
static ds_slot_stats_t ds_stats[2];
static dongle_device_blocklist_t *blocklist_ref;

static dongle_hfp_ag_t hfp_ag;
static struct bt_rfcomm_dlc hfp_dlc;
static struct bt_conn *hfp_acl_conn;
static struct bt_conn *sco_active_conn;
static bool hfp_dlc_connected, hfp_dlc_connecting, hfp_sdp_pending, hfp_sdp_done;
static bool hfp_remote_msbc_retry_fallback;
static bool sco_connected, sco_opening, sco_disconnect_sent, sco_msbc_active, a2dp_suspended_for_mic;
static volatile bool hfp_msbc_negotiated_for_hci;
static uint8_t hfp_remote_rfcomm_chan, hfp_rx_log_count, ds_mic_pkt_logs;
static TickType_t hfp_sdp_at, sco_retry_at, hfp_bcs_wait_until, ds_mic_retry_at[2];

static bool ds_lock(void)
{
    const bool is_task = !xPortIsInsideInterrupt();
    if (is_task) {
        taskENTER_CRITICAL();
    }
    return is_task;
}
static void ds_unlock(bool is_task)
{
    if (is_task) {
        taskEXIT_CRITICAL();
    }
}
static TickType_t ds_now_ticks(void)
{
    return xPortIsInsideInterrupt() ? xTaskGetTickCountFromISR()
                                    : xTaskGetTickCount();
}
static struct bt_br_discovery_result discovery[FOUND];
static struct { uint8_t address[6]; int8_t rssi; char name[30]; } found[FOUND];
static volatile uint8_t found_count, scan_active, last_error;
static volatile bool ready;
static SemaphoreHandle_t ready_signal;
static QueueHandle_t operations;
static dongle_device_catalog_t *catalog;
static dongle_settings_store_t *store;
static SemaphoreHandle_t catalog_lock;
static uint8_t headphone[6]; /* host display order, most significant byte first */
/* 0 absent, 1 ACL connecting, 2 authenticating, 3 A2DP connecting,
 * 4 A2DP connected, 5 streaming, 6 failed. Error stages: 1 ACL,
 * 2 authentication, 3 A2DP, 4 storage, 5 disconnection. */
static volatile uint8_t audio_state, audio_error_stage, audio_error;
static uint8_t audio_target[6];
static volatile bool restore_page_scan;
/* Explicitly armed diagnostic capture, read back over management HID. */
static uint8_t media_capture[1024];
static volatile uint16_t media_capture_size;
static volatile bool media_capture_armed;
static uint8_t local_address[6];
static volatile bool avrcp_connected;
static volatile uint8_t avrcp_volume = 0x50U;
/* btprofile 1.8.26 increments the four-bit signaling transaction label modulo
 * 16, but bt_avdtp_open incorrectly rejects label zero as "Error id". The
 * SET_CONFIGURATION confirmation calls OPEN after the preceding label 15 has
 * wrapped. Keep this ABI workaround local: the SDK does not expose bt_avdtp.
 * Verified from its RV32 disassembly: label byte 92, signaling channel 76 bytes.
 * Advancing an unused label before OPEN is safe; the preceding response has
 * already completed. No packet in flight is rewritten. */
struct bt_avdtp;
_Static_assert(sizeof(struct bt_l2cap_br_chan) == 76, "recheck btprofile AVDTP ABI");
extern int __real_bt_avdtp_open(struct bt_avdtp *, struct bt_avdtp_stream *);
int __wrap_bt_avdtp_open(struct bt_avdtp *session, struct bt_avdtp_stream *stream)
{
    if (session && stream) {
        uint8_t *label = (uint8_t *)session + 92;
        if (!*label) {
            *label = 1;
            printf("[RADIO] AVDTP OPEN transaction wrap corrected\r\n");
        }
    }
    return __real_bt_avdtp_open(session, stream);
}
extern int __real_bt_hci_cmd_send_sync(u16_t opcode, struct net_buf *buf, struct net_buf **rsp);
int __wrap_bt_hci_cmd_send_sync(u16_t opcode, struct net_buf *buf, struct net_buf **rsp)
{
    /* Acquire central role at incoming connection acceptance so we avoid a
     * dual-piconet scatternet before L2CAP HID/A2DP traffic starts. */
    if (opcode == BT_HCI_OP_ACCEPT_CONN_REQ && buf &&
        buf->len >= sizeof(struct bt_hci_cp_accept_conn_req)) {
        ((struct bt_hci_cp_accept_conn_req *)buf->data)->role = BT_HCI_ROLE_MASTER;
    } else if (opcode == BT_HCI_OP_ACCEPT_SYNC_CONN_REQ && buf &&
               buf->len >= sizeof(struct bt_hci_cp_accept_sync_conn_req) &&
               hfp_msbc_negotiated_for_hci) {
        struct bt_hci_cp_accept_sync_conn_req *cp =
            (struct bt_hci_cp_accept_sync_conn_req *)buf->data;
        cp->max_latency = sys_cpu_to_le16(0x000dU);
        cp->content_format = sys_cpu_to_le16(0x0063U);
        cp->retrans_effort = 0x02U;
    }
    return __real_bt_hci_cmd_send_sync(opcode, buf, rsp);
}
extern void __real_SBC_Encoder_Init(SBC_ENC_PARAMS *params);
void __wrap_SBC_Encoder_Init(SBC_ENC_PARAMS *params)
{
    /* Cap SBC bitpool to 26 so 5-frame RTP PDUs (338B) fit within a single 3-slot
     * 2-DH3 packet and fit within residual airtime alongside DualSense HID reports. */
    if (params && params->s16BitPool > 26) {
        params->s16BitPool = 26;
    }
    __real_SBC_Encoder_Init(params);
}
static void audio_failure(uint8_t stage, int error)
{
    audio_state = 6; audio_error_stage = stage;
    audio_error = error < 0 ? -error : error;
    last_error = audio_error;
    printf("[RADIO] headphone failure stage=%u error=%u\r\n", stage, audio_error);
}

static void address_out(uint8_t out[6], const bt_addr_t *in)
{ for (unsigned i = 0; i < 6; ++i) out[i] = in->val[5-i]; }
static peer_t *find_peer(struct bt_conn *conn)
{ if (!conn) return NULL; for (unsigned i = 0; i < PEERS; ++i) if (peers[i].conn == conn) return &peers[i]; return NULL; }
static peer_t *peer_address(const bt_addr_t *address)
{ for (unsigned i = 0; i < PEERS; ++i) if (!memcmp(peers[i].address.val, address->val, 6)) return &peers[i]; return NULL; }
static peer_t *channel_peer(struct bt_l2cap_chan *chan)
{ return find_peer(chan->conn); }
static bool is_host_addr_blocked(const uint8_t addr[6])
{
    if (!blocklist_ref || !addr) {
        return false;
    }
    bool blocked = false;
    if (catalog_lock != NULL &&
        xSemaphoreTake(catalog_lock, pdMS_TO_TICKS(200U)) == pdTRUE) {
        blocked = dongle_blocklist_contains(blocklist_ref, addr);
        xSemaphoreGive(catalog_lock);
    } else {
        blocked = dongle_blocklist_contains(blocklist_ref, addr);
    }
    return blocked;
}

static struct bt_uuid_16 hfp_hf_sdp_uuid = BT_UUID_INIT_16(BT_SDP_HANDSFREE_SVCLASS);

static struct bt_sdp_attribute hfp_ag_attrs[] = {
    BT_SDP_NEW_SERVICE,
    BT_SDP_LIST(
        BT_SDP_ATTR_SVCLASS_ID_LIST,
        BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 6),
        BT_SDP_DATA_ELEM_LIST(
            { BT_SDP_TYPE_SIZE(BT_SDP_UUID16), BT_SDP_ARRAY_16(BT_SDP_HANDSFREE_AGW_SVCLASS) },
            { BT_SDP_TYPE_SIZE(BT_SDP_UUID16), BT_SDP_ARRAY_16(BT_SDP_GENERIC_AUDIO_SVCLASS) }
        )
    ),
    BT_SDP_LIST(
        BT_SDP_ATTR_PROTO_DESC_LIST,
        BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 12),
        BT_SDP_DATA_ELEM_LIST(
            {
                BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 3),
                BT_SDP_DATA_ELEM_LIST(
                    { BT_SDP_TYPE_SIZE(BT_SDP_UUID16), BT_SDP_ARRAY_16(BT_SDP_PROTO_L2CAP) }
                )
            },
            {
                BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 5),
                BT_SDP_DATA_ELEM_LIST(
                    { BT_SDP_TYPE_SIZE(BT_SDP_UUID16), BT_SDP_ARRAY_16(BT_SDP_PROTO_RFCOMM) },
                    { BT_SDP_TYPE_SIZE(BT_SDP_UINT8), BT_SDP_ARRAY_8(BT_RFCOMM_CHAN_HFP_AG) }
                )
            }
        )
    ),
    BT_SDP_LIST(
        BT_SDP_ATTR_PROFILE_DESC_LIST,
        BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 8),
        BT_SDP_DATA_ELEM_LIST(
            {
                BT_SDP_TYPE_SIZE_VAR(BT_SDP_SEQ8, 6),
                BT_SDP_DATA_ELEM_LIST(
                    { BT_SDP_TYPE_SIZE(BT_SDP_UUID16), BT_SDP_ARRAY_16(BT_SDP_HANDSFREE_SVCLASS) },
                    { BT_SDP_TYPE_SIZE(BT_SDP_UINT16), BT_SDP_ARRAY_16(0x0107) }
                )
            }
        )
    ),
    BT_SDP_SERVICE_NAME("Handsfree Gateway"),
    {
        BT_SDP_ATTR_NETWORK,
        { BT_SDP_TYPE_SIZE(BT_SDP_UINT8), BT_SDP_ARRAY_8(0x01) }
    },
    BT_SDP_SUPPORTED_FEATURES(0x0020),
};

static struct bt_sdp_record hfp_ag_record = BT_SDP_RECORD(hfp_ag_attrs);

static void release_sco_active_conn(void)
{
    if (sco_active_conn != NULL) {
        if (sco_active_conn->sco.acl != NULL &&
            sco_active_conn->state == BT_CONN_DISCONNECTED) {
            bt_conn_unref(sco_active_conn->sco.acl);
            sco_active_conn->sco.acl = NULL;
        }
        bt_conn_unref(sco_active_conn);
        sco_active_conn = NULL;
    }
}

static void hfp_ag_sync_mic_state(void)
{
    const bool use_msbc = sco_connected ? sco_msbc_active
                                        : (hfp_ag.selected_codec == 2U);
    hfp_msbc_negotiated_for_hci = hfp_ag.selected_codec == 2U;
    dongle_mic_pipeline_set_hfp_state(hfp_ag.slc_ready, sco_connected, use_msbc);
}

static void hfp_ag_reset_state(void)
{
    hfp_dlc_connected = false;
    hfp_dlc_connecting = false;
    hfp_sdp_pending = false;
    hfp_sdp_done = false;
    hfp_remote_rfcomm_chan = 0U;
    hfp_remote_msbc_retry_fallback = false;
    hfp_acl_conn = NULL;
    hfp_bcs_wait_until = 0;
    hfp_rx_log_count = 0U;
    dongle_hfp_ag_init(&hfp_ag);
    hfp_ag_sync_mic_state();
}

static void hfp_rfcomm_tx(void *context, const uint8_t *data, size_t size)
{
    (void)context;
    if (!hfp_dlc_connected || data == NULL || size == 0U) {
        return;
    }
    const size_t mtu = hfp_dlc.mtu > 0U ? (size_t)hfp_dlc.mtu : 64U;
    size_t off = 0U;
    while (off < size) {
        struct net_buf *buf = bt_rfcomm_create_pdu(&hid_tx_pool);
        if (!buf) {
            break;
        }
        const size_t tail = net_buf_tailroom(buf);
        const size_t limit = mtu < tail ? mtu : tail;
        if (limit == 0U) {
            net_buf_unref(buf);
            break;
        }
        size_t chunk = size - off;
        if (chunk > limit) {
            chunk = limit;
            for (size_t k = off + chunk; k > off; --k) {
                if (data[k - 1U] == '\n') {
                    chunk = k - off;
                    break;
                }
            }
        }
        net_buf_add_mem(buf, &data[off], chunk);
        if (bt_rfcomm_dlc_send(&hfp_dlc, buf) < 0) {
            net_buf_unref(buf);
            break;
        }
        off += chunk;
    }
}

static void hfp_dlc_connected_cb(struct bt_rfcomm_dlc *dlc)
{
    (void)dlc;
    hfp_dlc_connected = true;
    hfp_dlc_connecting = false;
    hfp_rx_log_count = 0U;
    dongle_hfp_ag_init(&hfp_ag);
    hfp_ag_sync_mic_state();
    printf("[RADIO] HFP RFCOMM connected\r\n");
}

static void hfp_dlc_disconnected_cb(struct bt_rfcomm_dlc *dlc, struct bt_conn *conn)
{
    (void)dlc;
    (void)conn;
    hfp_dlc_connected = false;
    hfp_dlc_connecting = false;
    hfp_bcs_wait_until = 0;
    hfp_rx_log_count = 0U;
    dongle_hfp_ag_init(&hfp_ag);
    hfp_ag_sync_mic_state();
    printf("[RADIO] HFP RFCOMM disconnected\r\n");
}

static void hfp_dlc_recv_cb(struct bt_rfcomm_dlc *dlc, struct net_buf *buf)
{
    (void)dlc;
    if (!buf || buf->len == 0U) {
        return;
    }
    if (hfp_rx_log_count < 24U) {
        ++hfp_rx_log_count;
        char preview[80];
        const size_t n = buf->len < (sizeof(preview) - 1U) ? buf->len : (sizeof(preview) - 1U);
        for (size_t i = 0U; i < n; ++i) {
            const uint8_t ch = buf->data[i];
            preview[i] = (ch >= 0x20U && ch <= 0x7eU) ? (char)ch : '.';
        }
        preview[n] = '\0';
        printf("[RADIO] HFP RX: %s\r\n", preview);
    }
    const dongle_hfp_ag_action_t act = dongle_hfp_ag_feed(
        &hfp_ag, buf->data, buf->len, hfp_rfcomm_tx, NULL);
    if (act == DONGLE_HFP_AG_ACTION_SLC_READY) {
        hfp_rfcomm_tx(NULL, (const uint8_t *)"\r\n+VGM: 15\r\n", 12U);
        printf("[RADIO] HFP SLC ready hf_codecs=0x%02x selected=%u\r\n",
               hfp_ag.hf_codec_mask, hfp_ag.selected_codec);
    } else if (act == DONGLE_HFP_AG_ACTION_OPEN_SCO_MSBC ||
               act == DONGLE_HFP_AG_ACTION_OPEN_SCO_CVSD) {
        hfp_bcs_wait_until = 0;
        if (act == DONGLE_HFP_AG_ACTION_OPEN_SCO_CVSD &&
            (hfp_ag.hf_codec_mask & 0x02U) != 0U) {
            hfp_remote_msbc_retry_fallback = true;
        }
        printf("[RADIO] HFP codec selected=%u\r\n", hfp_ag.selected_codec);
    }
    hfp_ag_sync_mic_state();
}

static struct bt_rfcomm_dlc_ops hfp_dlc_ops = {
    .connected = hfp_dlc_connected_cb,
    .disconnected = hfp_dlc_disconnected_cb,
    .recv = hfp_dlc_recv_cb,
};

static int hfp_server_accept(struct bt_conn *conn, struct bt_rfcomm_dlc **dlc)
{
    peer_t *p = find_peer(conn);
    if (!p || !p->role || hfp_dlc_connected || hfp_dlc_connecting) {
        return -EACCES;
    }
    memset(&hfp_dlc, 0, sizeof(hfp_dlc));
    hfp_dlc.ops = &hfp_dlc_ops;
    hfp_dlc.mtu = 127U;
    hfp_dlc.required_sec_level = BT_SECURITY_L2;
    hfp_dlc_connecting = true;
    hfp_acl_conn = conn;
    *dlc = &hfp_dlc;
    return 0;
}

static struct bt_rfcomm_server hfp_server = {
    .channel = BT_RFCOMM_CHAN_HFP_AG,
    .accept = hfp_server_accept,
};

static uint8_t hfp_sdp_discover_cb(struct bt_conn *conn,
                                   struct bt_sdp_client_result *result)
{
    if (conn == NULL || conn != hfp_acl_conn) {
        hfp_sdp_pending = false;
        hfp_sdp_done = true;
        return BT_SDP_DISCOVER_UUID_STOP;
    }
    if (result != NULL && result->resp_buf != NULL) {
        uint16_t chan = 0U;
        if (bt_sdp_get_proto_param(result->resp_buf, BT_SDP_PROTO_RFCOMM, &chan) == 0 &&
            chan > 0U && chan <= 30U) {
            hfp_remote_rfcomm_chan = (uint8_t)chan;
            hfp_sdp_pending = false;
            hfp_sdp_done = true;
            printf("[RADIO] HFP HF SDP rfcomm chan=%u\r\n", chan);
            return BT_SDP_DISCOVER_UUID_STOP;
        }
        if (result->next_record_hint) {
            return BT_SDP_DISCOVER_UUID_CONTINUE;
        }
    }
    hfp_sdp_pending = false;
    hfp_sdp_done = true;
    return BT_SDP_DISCOVER_UUID_STOP;
}

static struct bt_sdp_discover_params hfp_sdp_params = {
    .uuid = &hfp_hf_sdp_uuid.uuid,
    .func = hfp_sdp_discover_cb,
    .pool = &hid_rx_pool,
};
#ifdef DONGLE_BT_SIGNAL_TRACE
/* Bounded negotiation headers only: never PCM, HCI authentication or keys. */
static void trace_signal(const char *direction, struct bt_conn *conn, uint16_t cid, struct net_buf *buf)
{
    static unsigned count;
    peer_t *p = find_peer(conn);
    static unsigned media_count;
    if (!p || count >= 512) return;
    if (p->role && audio_state == 5 && (!strcmp(direction, "RX") || buf->len < 17 || buf->data[0] != 0x80 || media_count++ >= 5)) return;
    ++count;
    printf("[BTTRACE] role=%u %s cid=%04x len=%u data=", p->role, direction, cid, buf->len);
    for (unsigned i = 0; i < buf->len && i < 32; ++i) printf("%02x", buf->data[i]);
    printf("\r\n");
}
#endif
int __real_bt_l2cap_chan_send(struct bt_l2cap_chan *chan, struct net_buf *buf);
int __wrap_bt_l2cap_chan_send(struct bt_l2cap_chan *chan, struct net_buf *buf)
{
    peer_t *p = find_peer(chan->conn);
    if (p && p->role) {
        /* The SDK writes wall-clock milliseconds into SBC RTP timestamps.
         * Preserve the initial offset, then advance by encoded sample count. */
        uint32_t next = p->rtp_packets ? p->rtp_samples : buf->len >= 8 ?
            (uint32_t)buf->data[4] << 24 | (uint32_t)buf->data[5] << 16 |
            (uint32_t)buf->data[6] << 8 | buf->data[7] : 0;
        if (dongle_audio_sbc_rtp_timestamp(buf->data, buf->len, &next)) {
            p->rtp_samples = next; ++p->rtp_packets;
            if (media_capture_armed && buf->len <= sizeof(media_capture)) {
                taskENTER_CRITICAL();
                memcpy(media_capture, buf->data, buf->len);
                media_capture_size = buf->len;
                media_capture_armed = false;
                taskEXIT_CRITICAL();
            }
        }
    }
#ifdef DONGLE_BT_SIGNAL_TRACE
    if (chan->conn && chan->conn->type == BT_CONN_TYPE_BR)
        trace_signal("TX", chan->conn, ((struct bt_l2cap_br_chan *)chan)->tx.cid, buf);
#endif
    return __real_bt_l2cap_chan_send(chan, buf);
}
#ifdef DONGLE_BT_SIGNAL_TRACE
void __real_bt_l2cap_recv(struct bt_conn *conn, struct net_buf *buf);
void __wrap_bt_l2cap_recv(struct bt_conn *conn, struct net_buf *buf)
{
    uint16_t cid = buf->len >= 4 ? buf->data[2] | (buf->data[3] << 8) : 0;
    trace_signal("RX", conn, cid, buf);
    __real_bt_l2cap_recv(conn, buf);
}
#endif
static struct net_buf *allocate(struct bt_l2cap_chan *chan)
{ (void)chan; return net_buf_alloc(&hid_rx_pool, K_NO_WAIT); }
static int ds_send_l2cap_report(struct bt_l2cap_chan *chan, uint8_t hidp_hdr,
                                const uint8_t *payload, size_t len)
{
    struct net_buf *buf = net_buf_alloc(&hid_tx_pool, K_NO_WAIT);
    if (!buf) {
        return -ENOMEM;
    }
    net_buf_reserve(buf, BT_L2CAP_CHAN_SEND_RESERVE);
    net_buf_add_u8(buf, hidp_hdr);
    if (payload && len > 0U) {
        net_buf_add_mem(buf, payload, len);
    }
    const int err = bt_l2cap_chan_send(chan, buf);
    if (err < 0) {
        net_buf_unref(buf);
    }
    return err;
}
static int receive_control(struct bt_l2cap_chan *chan, struct net_buf *buf)
{
    peer_t *p = channel_peer(chan);
    if (!p || p->role || !buf || buf->len < 1U) {
        return 0;
    }
    if ((buf->data[0] & 0xf0U) == 0x00U && p->slot < 2U) {
        const bool locked = ds_lock();
        ds_stats[p->slot].last_handshake = buf->data[0] & 0x0fU;
        if (p->ds_80_await_handshake) {
            p->ds_80_await_handshake = false;
            p->ds_pending_get_id = 0x81U;
        }
        ds_unlock(locked);
        return 0;
    }
    if (buf->len >= 7U && buf->data[0] == 0xa3U) {
        uint8_t usb_feat[64];
        size_t usb_len = 0U;
        const bool crc_ok = dongle_dualsense_bt_feature_to_usb(
            buf->data + 1U, buf->len - 1U, usb_feat, sizeof(usb_feat), &usb_len);
        const uint8_t id = buf->data[1];
        uint8_t bit = 0U;
        if (crc_ok) {
            if (id == 0x05U) bit = 0x01U;
            else if (id == 0x09U) bit = 0x02U;
            else if (id == 0x20U) bit = 0x04U;
            else if (id == 0x08U) bit = 0x08U;
            else if (id == 0x81U) bit = 0x10U;
        }
        if (p->slot < 2U && (!crc_ok || (bit != 0U && !(p->ds_feat_logged & bit)))) {
            if (bit != 0U) {
                p->ds_feat_logged |= bit;
            }
            printf("[RADIO] DS feat 0x%02x len=%u ok=%u\r\n",
                   id, (unsigned)(buf->len - 1U), crc_ok ? 1U : 0U);
        }
        if (crc_ok && p->slot < 2U) {
            const bool locked = ds_lock();
            if (id == 0x05U && usb_len == sizeof(p->ds_feat_05)) {
                memcpy(p->ds_feat_05, usb_feat, sizeof(p->ds_feat_05));
                p->ds_feat_05_valid = true;
            } else if (id == 0x08U && usb_len == sizeof(p->ds_feat_08)) {
                memcpy(p->ds_feat_08, usb_feat, sizeof(p->ds_feat_08));
                p->ds_feat_08_valid = true;
            } else if (id == 0x09U && usb_len == sizeof(p->ds_feat_09)) {
                memcpy(p->ds_feat_09, usb_feat, sizeof(p->ds_feat_09));
                p->ds_feat_09_valid = true;
            } else if (id == 0x20U && usb_len == sizeof(p->ds_feat_20)) {
                memcpy(p->ds_feat_20, usb_feat, sizeof(p->ds_feat_20));
                p->ds_feat_20_valid = true;
            } else if (id == 0x81U && usb_len == sizeof(p->ds_feat_81)) {
                memcpy(p->ds_feat_81, usb_feat, sizeof(p->ds_feat_81));
                if (usb_feat[3] == 0x01U && p->ds_80_repoll_left > 0U) {
                    --p->ds_80_repoll_left;
                    ds_feat_81_busy[p->slot] = true;
                    p->ds_80_await_handshake = true;
                    p->ds_80_poll_at = ds_now_ticks() + pdMS_TO_TICKS(15);
                } else {
                    p->ds_feat_81_valid = true;
                    ds_feat_81_busy[p->slot] = false;
                }
            }
            ++ds_stats[p->slot].bt_feat_get_ok;
            ds_stats[p->slot].last_feat_id = id;
            ds_unlock(locked);
        }
    }
    return 0;
}
static int receive_input(struct bt_l2cap_chan *chan, struct net_buf *buf)
{
    peer_t *p = channel_peer(chan);
    uint8_t report[9];
    if (p && !p->input_seen && buf->len > 1) {
        p->input_seen = true;
        printf("[RADIO] controller first input len=%u report=%02x\r\n", buf->len, buf->data[1]);
    }
    if (p && p->authenticated && p->saved && buf->len > 1 && buf->data[0] == 0xa1) {
        const uint8_t *opus_ptr = NULL;
        size_t opus_len = 0U;
        if (p->slot < 2U && dongle_dualsense_bt_extract_mic_opus(
                buf->data + 1U, buf->len - 1U, &opus_ptr, &opus_len)) {
            if (ds_mic_pkt_logs < 4U) {
                ++ds_mic_pkt_logs;
                printf("[RADIO] DS mic opus slot=%u len=%u hdr=%02x%02x%02x\r\n",
                       (unsigned)p->slot, (unsigned)opus_len,
                       opus_len > 0U ? opus_ptr[0] : 0U,
                       opus_len > 1U ? opus_ptr[1] : 0U,
                       opus_len > 2U ? opus_ptr[2] : 0U);
            }
            dongle_mic_pipeline_feed_dualsense_opus(p->slot, opus_ptr, opus_len);
        } else if (dongle_dualsense_input(buf->data + 1, buf->len - 1, report)) {
            if (p->slot < 2U) {
                const bool locked = ds_lock();
                if (buf->data[1] == 0x31U && buf->len - 1U == 78U) {
                    ++ds_stats[p->slot].bt_in_31;
                } else if (buf->data[1] == 0x01U && buf->len - 1U == 10U) {
                    ++ds_stats[p->slot].bt_in_01;
                }
                ds_unlock(locked);
            }
            dongle_usb_gamepad_sink(NULL, p->slot, report, sizeof(report));
            uint8_t ds_usb[DONGLE_DUALSENSE_USB_INPUT_SIZE];
            if (dongle_dualsense_bt_to_usb_input(buf->data + 1, buf->len - 1, ds_usb)) {
                dongle_usb_gamepad_dualsense_raw(p->slot, ds_usb);
            }
        }
#ifdef DONGLE_BT_SIGNAL_TRACE
        else if (!p->input_rejected) {
            p->input_rejected = true;
            printf("[RADIO] rejected controller input len=%u data=", buf->len);
            for (unsigned i = 0; i < buf->len && i < 79; ++i) printf("%02x", buf->data[i]);
            printf("\r\n");
        }
#endif
    }
    return 0;
}
static void control_connected(struct bt_l2cap_chan *chan)
{ peer_t *p = channel_peer(chan); if (p) { p->control_open = true; p->profile_pending = true; printf("[RADIO] controller control connected\r\n"); } }
static void input_connected(struct bt_l2cap_chan *chan)
{
    peer_t *p = channel_peer(chan);
    if (p) {
        p->interrupt_open = true;
        p->profile_pending = true;
        dongle_mic_pipeline_set_ds_connected(dongle_bluetooth_connected());
        printf("[RADIO] controller input connected\r\n");
    }
}
static void channel_disconnected(struct bt_l2cap_chan *chan)
{
    peer_t *p = channel_peer(chan);
    if (!p) return;
    if (chan == &p->control.chan) p->control_open = false;
    else {
        p->interrupt_open = false;
        dongle_mic_pipeline_set_ds_connected(dongle_bluetooth_connected());
    }
    const uint8_t neutral[9] = {128,128,128,128,0,0,0,0,8};
    if (p->slot < 2) {
        dongle_usb_gamepad_sink(NULL, p->slot, neutral, 9);
        dongle_usb_gamepad_dualsense_clear(p->slot);
    }
}
static struct bt_l2cap_chan_ops control_ops = {
    .connected = control_connected, .disconnected = channel_disconnected,
    .recv = receive_control, .alloc_buf = allocate,
};
static struct bt_l2cap_chan_ops input_ops = {
    .connected = input_connected, .disconnected = channel_disconnected,
    .recv = receive_input, .alloc_buf = allocate,
};
static void setup_channels(peer_t *p)
{
    memset(&p->control, 0, sizeof(p->control));
    memset(&p->interrupt, 0, sizeof(p->interrupt));
    p->control.chan.ops = &control_ops; p->control.rx.mtu = HID_MTU;
    p->interrupt.chan.ops = &input_ops; p->interrupt.rx.mtu = HID_MTU;
}
static int accept_control(struct bt_conn *conn, struct bt_l2cap_chan **channel)
{
    peer_t *p = find_peer(conn);
    if (!p || p->role || p->control.chan.conn) return -EACCES;
    *channel = &p->control.chan; return 0;
}
static int accept_input(struct bt_conn *conn, struct bt_l2cap_chan **channel)
{
    peer_t *p = find_peer(conn);
    if (!p || p->role || p->interrupt.chan.conn) return -EACCES;
    *channel = &p->interrupt.chan; return 0;
}
static struct bt_l2cap_server control_server = {.psm = 0x11, .accept = accept_control};
static struct bt_l2cap_server input_server = {.psm = 0x13, .accept = accept_input};

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (conn->type == BT_CONN_TYPE_SCO) {
        return;
    }
    if (conn->type != BT_CONN_TYPE_BR) return;
    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info)) return;
    uint8_t host_addr[6];
    address_out(host_addr, info.br.dst);
    peer_t *p = peer_address(info.br.dst);
    restore_page_scan = true;
    bool outgoing = p && p->connecting;
    if (p) { p->connecting = false; p->retry_at = xTaskGetTickCount() + pdMS_TO_TICKS(10000); }
    if (err) { last_error = err; if (p) { if (p->role) audio_failure(1, err); if (p->conn) bt_conn_unref(p->conn); p->conn = NULL; } return; }
    if (is_host_addr_blocked(host_addr)) {
        if (p) { p->saved = false; p->reconnect = false; }
        bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        return;
    }
    if (!p || (!p->saved && !p->reconnect)) { bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN); return; }
    if (!p->conn) p->conn = bt_conn_ref(conn);
    p->rtp_packets = p->rtp_samples = 0;
    p->outgoing = outgoing;
    last_error = 0;
    p->security_pending = true;
    /* Request the central role before authentication so it cannot collide
     * with the security LMP transaction; see manage_role(). */
    p->role_stage = 1; p->role_deadline = xTaskGetTickCount();
    if (p->role) { audio_state = 2; audio_error = audio_error_stage = 0; }
    printf("[RADIO] ACL connected role=%u link=%s\r\n", p->role,
           conn->role == BT_HCI_ROLE_MASTER ? "central" : "peripheral");
}
static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    if (conn->type == BT_CONN_TYPE_SCO) {
        return;
    }
    peer_t *p = find_peer(conn);
    if (!p) return;
    const uint8_t neutral[9] = {128,128,128,128,0,0,0,0,8};
    if (!p->role && p->slot < 2) {
        dongle_usb_gamepad_sink(NULL, p->slot, neutral, 9);
        dongle_usb_gamepad_dualsense_clear(p->slot);
        const bool locked = ds_lock();
        ds_out_pending[p->slot] = false;
        ds_out_retry_at[p->slot] = 0;
        ds_mic_enabled[p->slot] = false;
        ds_mic_unmuted[p->slot] = false;
        ds_mic_retry_at[p->slot] = 0;
        ds_feat_tx_pending[p->slot] = false;
        ds_feat_get_req[p->slot] = 0U;
        ds_feat_81_busy[p->slot] = false;
        ds_unlock(locked);
    }
    p->ds_feat_05_valid = p->ds_feat_08_valid = p->ds_feat_09_valid = false;
    p->ds_feat_20_valid = p->ds_feat_81_valid = false;
    p->ds_led_unlocked = p->ds_80_await_handshake = false;
    p->ds_probe_stage = p->ds_probe_retries = p->ds_pending_get_id = 0U;
    p->ds_80_repoll_left = p->ds_feat_logged = 0U;
    if (p->role) {
        release_sco_active_conn();
        sco_connected = false;
        sco_opening = false;
        sco_disconnect_sent = false;
        sco_msbc_active = false;
        a2dp_suspended_for_mic = false;
        hfp_ag_reset_state();
        dongle_usb_audio_streaming(false);
        audio_failure(5, reason);
        avrcp_connected = false;
#ifdef CONFIG_BT_AVRCP
        avrcp_set_playback_status(PLAY_STATUS_PAUSED);
#endif
    }
    bt_conn_unref(p->conn);
    p->conn = NULL; p->authenticated = false; p->security_pending = false;
    p->control_open = p->interrupt_open = p->profile_pending = p->opening = false;
    p->audio_connected = p->audio_start_requested = false;
    p->role_stage = 0;
    if (!p->role && p->slot < 2) {
        ds_out_last_len[p->slot] = 0;
        dongle_mic_pipeline_set_ds_connected(dongle_bluetooth_connected());
    }
    p->retry_at = xTaskGetTickCount() + pdMS_TO_TICKS(3000);
    /* The SDK still owns the L2CAP channel list during this callback. Do not
     * memset its channel objects before the deferred stack cleanup finishes. */
    restore_page_scan = true;
    printf("[RADIO] disconnected reason=%u\r\n", reason);
}
static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
    peer_t *p = find_peer(conn);
    if (!p) return;
    if (err || level < BT_SECURITY_L2) { last_error = err ? err : 1; if (p->role) audio_failure(2, last_error); return; }
    p->authenticated = true; p->profile_pending = true;
    if (p->role) {
        audio_state = 3;
        hfp_acl_conn = conn;
        hfp_sdp_at = xTaskGetTickCount() + pdMS_TO_TICKS(1200U);
        hfp_sdp_done = false;
        hfp_sdp_pending = false;
        hfp_remote_rfcomm_chan = 0U;
    }
    printf("[RADIO] authenticated role=%u\r\n", p->role);
}
static struct bt_conn_cb connection_callbacks = {
    .connected = connected, .disconnected = disconnected, .security_changed = security_changed,
};
static void cancel(struct bt_conn *conn) { (void)conn; }
/* SSP Just Works: only explicitly selected or previously saved addresses are accepted. */
static void pairing_confirm(struct bt_conn *conn)
{
    struct bt_conn_info info;
    if (conn->type == BT_CONN_TYPE_BR && bt_conn_get_info(conn, &info) == 0) {
        uint8_t host_addr[6];
        address_out(host_addr, info.br.dst);
        if (is_host_addr_blocked(host_addr)) {
            bt_conn_auth_cancel(conn);
            return;
        }
    }
    if (find_peer(conn)) bt_conn_auth_pairing_confirm(conn); else bt_conn_auth_cancel(conn);
}
static void pairing_failed(struct bt_conn *conn, enum bt_security_err err)
{ peer_t *p = find_peer(conn); last_error = err ? err : 1; if (p && p->role) audio_failure(2, last_error); }
static struct bt_conn_auth_cb authentication = {
    .cancel = cancel, .pairing_confirm = pairing_confirm, .pairing_failed = pairing_failed,
};
static void inquiry_complete(struct bt_br_discovery_result *results, size_t count)
{
    taskENTER_CRITICAL();
    found_count = count > FOUND ? FOUND : count;
    for (unsigned i = 0; i < found_count; ++i) {
        address_out(found[i].address, &results[i].addr);
        found[i].rssi = results[i].rssi;
        memset(found[i].name, 0, sizeof(found[i].name));
        for (size_t offset = 0; offset < sizeof(results[i].eir);) {
            uint8_t length = results[i].eir[offset];
            if (!length || offset + 1U + length > sizeof(results[i].eir)) break;
            if (length > 1 && (results[i].eir[offset+1] == 9 || results[i].eir[offset+1] == 8)) {
                size_t n = length - 1;
                if (n >= sizeof(found[i].name)) n = sizeof(found[i].name) - 1;
                memcpy(found[i].name, &results[i].eir[offset+2], n);
            }
            offset += length + 1U;
        }
    }
    scan_active = 0;
    taskEXIT_CRITICAL();
    printf("[RADIO] discovery complete count=%u\r\n", found_count);
}
static void audio_chain(struct bt_conn *conn, uint8_t state)
{
    peer_t *p = find_peer(conn);
    if (!p || !p->role) return;
    if (state == BT_A2DP_CHAIN_CONNECTED) {
        p->audio_connected = true;
        audio_state = 3; audio_error = audio_error_stage = 0;
        /* SDK avdtp.c starts bt_source_start_discovery before this callback.
         * bt_start_discovery uses the sink callback and would overwrite the
         * pending source transaction, leaving responses unmatched. */
    } else if (state == BT_A2DP_CHAIN_DISCONNECTED) {
        p->audio_connected = false;
        p->audio_start_requested = false;
        dongle_usb_audio_streaming(false); audio_failure(3, ENOTCONN);
    }
}
static void audio_stream(uint8_t state)
{
    bool active = state == BT_A2DP_STREAM_START;
    audio_state = active ? 5 : 4;
    dongle_usb_audio_streaming(active);
#ifdef CONFIG_BT_AVRCP
    if (avrcp_connected) avrcp_set_playback_status(active ? PLAY_STATUS_PLAYING : PLAY_STATUS_PAUSED);
#endif
}
static void audio_started(void)
{
    audio_state = 5;
    dongle_usb_audio_streaming(true);
#ifdef CONFIG_BT_AVRCP
    if (avrcp_connected) avrcp_set_playback_status(PLAY_STATUS_PLAYING);
#endif
}
static void audio_discovered(struct bt_conn *conn, uint16_t count)
{ peer_t *p = find_peer(conn); if (p && p->role) printf("[RADIO] A2DP endpoints=%u\r\n", count); }
static void audio_suspended(void)
{
    audio_state = 4;
    dongle_usb_audio_streaming(false);
#ifdef CONFIG_BT_AVRCP
    if (avrcp_connected) avrcp_set_playback_status(PLAY_STATUS_PAUSED);
#endif
}
static struct a2dp_callback audio_callbacks = {
    .chain = audio_chain, .stream = audio_stream, .start_cfm = audio_started, .suspend_cfm = audio_suspended,
};
#ifdef CONFIG_BT_AVRCP
static void avrcp_chain(struct bt_conn *conn, uint8_t state)
{
    (void)conn;
    if (state == BT_AVRCP_CHAIN_CONNECTED) {
        avrcp_connected = true;
        avrcp_set_playback_status(dongle_usb_audio_is_streaming() ? PLAY_STATUS_PLAYING : PLAY_STATUS_PAUSED);
        avrcp_send_volume_notification(NULL);
        avrcp_set_absvol_cmd(NULL, avrcp_volume);
        printf("[RADIO] AVRCP connected vol=%u\r\n", avrcp_volume);
    } else if (state == BT_AVRCP_CHAIN_DISCONNECTED) {
        avrcp_connected = false;
        printf("[RADIO] AVRCP disconnected\r\n");
    }
}
static void avrcp_absvol(uint8_t vol)
{
    avrcp_volume = (uint8_t)(vol & ABS_VOL_MASK);
    printf("[RADIO] AVRCP abs_vol=%u\r\n", avrcp_volume);
}
static void avrcp_passthrough_handler(uint8_t released, uint8_t option_id)
{
    printf("[RADIO] AVRCP key=0x%02x released=%u\r\n", option_id, released);
}
static struct avrcp_callback avrcp_callbacks = {
    .chain = avrcp_chain,
    .abs_vol = avrcp_absvol,
    .passthrough_handler = avrcp_passthrough_handler,
};
#endif
static void enabled(int err) { last_error = err ? (uint8_t)-err : 0; ready = !err; xSemaphoreGive(ready_signal); }
static void start_connection(operation_t *op)
{
    last_error = 0;
    bool is_audio = op->command == 0x19;
    if (is_audio) { memcpy(audio_target, op->address, 6); audio_state = 1; audio_error = audio_error_stage = 0; }
    bt_addr_t address;
    for (unsigned i = 0; i < 6; ++i) address.val[i] = op->address[5-i];
    peer_t *p = peer_address(&address);
    if (!p) for (unsigned i = 0; i < PEERS; ++i)
        if (!peers[i].conn && !peers[i].connecting &&
            !peers[i].control.chan.conn && !peers[i].interrupt.chan.conn) { p = &peers[i]; break; }
    if (!p) { last_error = EBUSY; if (is_audio) audio_failure(1, EBUSY); return; }
    if (p->conn || p->connecting) {
        if (is_audio && p->role) {
            audio_state = p->connecting ? 1 : dongle_usb_audio_is_streaming() ? 5 : p->audio_connected ? 4 : p->authenticated ? 3 : 2;
            if (!dongle_usb_audio_is_streaming()) p->audio_start_requested = false;
        } else last_error = EBUSY;
        return;
    }
    if (p->control.chan.conn || p->interrupt.chan.conn) {
        last_error = EBUSY; if (is_audio) audio_failure(1, EBUSY); return;
    }
    /* The controller has one paging procedure. Do not overlap outgoing pages
     * or restore page scan while another peer is still connecting. */
    for (unsigned i = 0; i < PEERS; ++i) if (peers[i].connecting) {
        last_error = EBUSY; if (is_audio) audio_failure(1, EBUSY); return;
    }
    bool saved = p->saved && p->role == is_audio && !memcmp(p->address.val, address.val, 6);
    uint8_t slot = saved ? p->slot : 0xff;
    memset(p, 0, sizeof(*p)); p->address = address;
    p->role = op->command == 0x19; p->slot = 0xff; p->outgoing = true;
    p->saved = saved; p->slot = slot; p->reconnect = true;
    p->retry_at = xTaskGetTickCount() + pdMS_TO_TICKS(10000);
    setup_channels(p);
    p->connecting = true;
    struct bt_br_conn_param param = {.allow_role_switch = false};
    /* This BL616 controller cannot page a remote while page scan is enabled.
     * Restore connectability from the task after the completion callback. */
    bt_br_set_connectable(false);
    bt_br_set_discoverable(false);
    struct bt_conn *connection = bt_conn_create_br(&address, &param);
    /* Bouffalo returns a borrowed sticky reference, unlike Zephyr. The SDK
     * releases it on failure/disconnect. The success callback takes our own
     * reference; never store or unref the borrowed return value here. */
    if (!connection) {
        p->connecting = false; restore_page_scan = true;
        last_error = ECONNREFUSED; if (is_audio) audio_failure(1, ECONNREFUSED);
    }
}
static void save_peer(peer_t *p)
{
    uint8_t address[6]; address_out(address, &p->address);
    if (p->role) {
        if (!ef_set_env_blob("dongle-headphone", address, 6)) {
            memcpy(headphone, address, 6); p->saved = true;
        } else { last_error = EIO; audio_failure(4, EIO); }
    } else {
        xSemaphoreTake(catalog_lock, portMAX_DELAY);
        dongle_device_catalog_t before = *catalog;
        for (unsigned i = 0; i < 2; ++i) if (catalog->controllers[i].paired &&
            !memcmp(catalog->controllers[i].address, address, 6)) { p->slot = i; p->saved = true; }
        if (!p->saved && dongle_catalog_pair_controller(catalog, address, &p->slot) == DONGLE_CATALOG_OK) {
            if (dongle_settings_store_save(store, catalog) == DONGLE_STORE_OK) p->saved = true;
            else { *catalog = before; last_error = EIO; }
        }
        xSemaphoreGive(catalog_lock);
    }
}
/* Scatternet avoidance. The SDK accepts every incoming ACL as peripheral
 * (hci_core.c accept_conn), so a PS-button DualSense reconnect makes the
 * dongle a peripheral in the controller's piconet while it is central for
 * Theater U. The radio must then follow two unrelated clocks, and the SBC
 * stream loses slots whenever the DualSense piconet is serviced: audible as
 * intermittent dropouts. Take the central role on every link so one schedule
 * owns both, then stop the peer from switching it back. Reference counting is
 * safe across the switch: the SDK tracks BT_CONN_BR_INITIAL_MASTER. */
#define DONGLE_HCI_OP_SWITCH_ROLE BT_OP(BT_OGF_LINK_POLICY, 0x000b)
struct dongle_hci_cp_switch_role { bt_addr_t bdaddr; uint8_t role; } __packed;
static int request_central(const bt_addr_t *address)
{
    struct net_buf *buf = bt_hci_cmd_create(DONGLE_HCI_OP_SWITCH_ROLE,
                                            sizeof(struct dongle_hci_cp_switch_role));
    if (!buf) return -ENOBUFS;
    struct dongle_hci_cp_switch_role *cp = net_buf_add(buf, sizeof(*cp));
    bt_addr_copy(&cp->bdaddr, address);
    cp->role = BT_HCI_ROLE_MASTER;
    /* Completes on Command Status; the SDK's Role Change handler then
     * updates connection->role, which manage_role() polls. */
    return bt_hci_cmd_send_sync(DONGLE_HCI_OP_SWITCH_ROLE, buf, NULL);
}
static int keep_central(struct bt_conn *connection)
{
    struct net_buf *buf = bt_hci_cmd_create(BT_HCI_OP_WRITE_LINK_POLICY,
                                            sizeof(struct bt_hci_cp_write_link_policy));
    if (!buf) return -ENOBUFS;
    struct bt_hci_cp_write_link_policy *cp = net_buf_add(buf, sizeof(*cp));
    cp->handle = sys_cpu_to_le16(connection->handle);
    /* Keep ACL links central and active to prevent scatternet and sniff stalls. */
    cp->link_policy = sys_cpu_to_le16(0);
    return bt_hci_cmd_send_sync(BT_HCI_OP_WRITE_LINK_POLICY, buf, NULL);
}
#define DONGLE_HCI_OP_QOS_SETUP BT_OP(BT_OGF_LINK_POLICY, 0x0007)
struct dongle_hci_cp_qos_setup {
    uint16_t handle;
    uint8_t flags;
    uint8_t service_type;
    uint32_t token_rate;
    uint32_t peak_bandwidth;
    uint32_t latency;
    uint32_t delay_variation;
} __packed;
static int configure_qos(struct bt_conn *connection, bool is_audio)
{
    struct net_buf *buf = bt_hci_cmd_create(DONGLE_HCI_OP_QOS_SETUP,
                                            sizeof(struct dongle_hci_cp_qos_setup));
    if (!buf) return -ENOBUFS;
    struct dongle_hci_cp_qos_setup *cp = net_buf_add(buf, sizeof(*cp));
    cp->handle = sys_cpu_to_le16(connection->handle);
    cp->flags = 0U;
    if (is_audio) {
        cp->service_type = 0x02U; /* Guaranteed */
        cp->token_rate = sys_cpu_to_le32(45000U);
        cp->peak_bandwidth = 0U;
        cp->latency = sys_cpu_to_le32(15000U);
    } else {
        cp->service_type = 0x01U; /* Best Effort */
        cp->token_rate = 0U;
        cp->peak_bandwidth = 0U;
        cp->latency = sys_cpu_to_le32(10000U);
    }
    cp->delay_variation = sys_cpu_to_le32(0xffffffffU);
    return bt_hci_cmd_send_sync(DONGLE_HCI_OP_QOS_SETUP, buf, NULL);
}
/* Radio task only: issues synchronous HCI commands. */
static void manage_role(peer_t *p, struct bt_conn *connection)
{
    if (!p->role_stage) return;
    if (connection->role == BT_HCI_ROLE_MASTER) {
        p->role_stage = 0;
        int err = keep_central(connection);
        int qos_err = configure_qos(connection, p->role != 0U);
        printf("[RADIO] central link role=%u policy=%d qos=%d\r\n", p->role, err, qos_err);
        return;
    }
    const TickType_t now = xTaskGetTickCount();
    if ((int32_t)(now - p->role_deadline) < 0) return;
    switch (p->role_stage) {
    case 1:
    case 3: {
        /* A first attempt can collide with the peer's own authentication;
         * retry once after it completes (encryption pause is handled by LM). */
        if (p->role_stage == 3 && !p->authenticated) return;
        int err = request_central(&p->address);
        p->role_deadline = now + pdMS_TO_TICKS(1000);
        if (err) {
            printf("[RADIO] role switch request role=%u err=%d\r\n", p->role, err);
            p->role_stage = p->role_stage == 1 ? 3 : 0;
        } else {
            ++p->role_stage;
        }
        break;
    }
    default:
        printf("[RADIO] role switch timeout role=%u stage=%u\r\n", p->role, p->role_stage);
        p->role_stage = p->role_stage == 2 ? 3 : 0;
        break;
    }
    if (!p->role_stage)
        printf("[RADIO] remains peripheral role=%u; audio may drop out\r\n", p->role);
}
static int sco_hci_disconnect(uint16_t handle, uint8_t reason)
{
    if (handle == 0U) {
        return -EINVAL;
    }
    struct net_buf *buf = bt_hci_cmd_create(BT_HCI_OP_DISCONNECT,
                                            sizeof(struct bt_hci_cp_disconnect));
    if (!buf) {
        return -ENOBUFS;
    }
    struct bt_hci_cp_disconnect *cp = net_buf_add(buf, sizeof(*cp));
    cp->handle = sys_cpu_to_le16(handle);
    cp->reason = reason;
    return bt_hci_cmd_send_sync(BT_HCI_OP_DISCONNECT, buf, NULL);
}

static void radio_task(void *unused)
{
    (void)unused;
    operation_t op;
    bool usb_was_open = false;
    for (;;) {
        bool usb_open = dongle_usb_audio_playback_open();
        if (usb_open && !usb_was_open) for (unsigned i = 0; i < PEERS; ++i)
            if (peers[i].role) peers[i].audio_start_requested = false;
        usb_was_open = usb_open;
        if (restore_page_scan) {
            bool paging = false;
            for (unsigned i = 0; i < PEERS; ++i) paging |= peers[i].connecting;
            if (!paging) { restore_page_scan = false; bt_br_set_connectable(true); }
        }
        bool ctrl_active = false;
        for (unsigned i = 0; i < PEERS; ++i) {
            if (!peers[i].role && peers[i].interrupt_open) {
                ctrl_active = true;
                break;
            }
        }
        if (xQueueReceive(operations, &op, pdMS_TO_TICKS(ctrl_active ? 2 : 20)) == pdTRUE) {
            last_error = 0;
            if (op.command == 0x13) {
                struct bt_br_discovery_param param = {.length = 5, .limited = false};
                int err = bt_br_discovery_start(&param, discovery, FOUND, inquiry_complete);
                if (err) { last_error = (uint8_t)-err; scan_active = 0; }
            } else if (op.command == 0x1a || op.command == 0x1b) {
                bt_addr_t addr;
                for (unsigned i = 0; i < 6; ++i) addr.val[i] = op.address[5-i];
                peer_t *p = peer_address(&addr);
                if (op.command == 0x1b) {
                    bt_keys_link_key_clear_addr(&addr);
                    if (p) { p->saved = false; p->reconnect = false; }
                }
                if (p) {
                    p->reconnect = false;
                    if (p->role) {
                        if (sco_active_conn != NULL) {
                            if (sco_active_conn->state == BT_CONN_CONNECTED &&
                                sco_active_conn->handle != 0U) {
                                (void)sco_hci_disconnect(sco_active_conn->handle,
                                                         BT_HCI_ERR_REMOTE_USER_TERM_CONN);
                            }
                            release_sco_active_conn();
                        }
                        sco_connected = false;
                        sco_opening = false;
                        sco_disconnect_sent = false;
                        sco_msbc_active = false;
                        if (hfp_dlc_connected) {
                            (void)bt_rfcomm_dlc_disconnect(&hfp_dlc);
                        }
                        a2dp_suspended_for_mic = false;
                        hfp_ag_reset_state();
                    }
                }
                if (p && p->conn) bt_conn_disconnect(p->conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
            } else start_connection(&op);
        }
        for (unsigned i = 0; i < PEERS; ++i) {
            peer_t *p = &peers[i];
            taskENTER_CRITICAL();
            struct bt_conn *connection = p->conn ? bt_conn_ref(p->conn) : NULL;
            taskEXIT_CRITICAL();
            if (!connection) continue;
            if (p->role && p->authenticated && p->audio_connected && !p->audio_start_requested &&
                !dongle_usb_audio_is_streaming() &&
                !dongle_mic_pipeline_desired_hfp_sco() && !sco_connected && !sco_opening &&
                !a2dp_suspended_for_mic &&
                (int32_t)(xTaskGetTickCount() - p->audio_poll_at) >= 0) {
                p->audio_poll_at = xTaskGetTickCount() + pdMS_TO_TICKS(200);
                uint16_t mtu;
                unsigned connected_channels = 0;
                bool configuring = false;
                struct bt_l2cap_chan *channel;
                taskENTER_CRITICAL();
                SYS_SLIST_FOR_EACH_CONTAINER(&connection->channels, channel, node) {
                    if (channel->psm == 0x19) {
                        if (channel->state == BT_L2CAP_CONNECTED) ++connected_channels;
                        else configuring = true;
                    }
                }
                taskEXIT_CRITICAL();
                /* OPEN establishes the media transport, but this SDK waits
                 * for the application to issue START explicitly. MTU alone
                 * is assigned before media L2CAP configuration completes. */
                /* Incoming signaling channels retain psm=0 in this SDK;
                 * require configured outgoing AVDTP transport and no pending
                 * AVDTP channel, rather than counting two PSM fields. */
                if (connected_channels && !configuring && !bt_a2dp_get_media_tx_mtu(connection, &mtu) && mtu) {
                    p->audio_start_requested = true;
                    int result = bt_stream_resume(connection);
                    printf("[RADIO] A2DP media mtu=%u start=%d\r\n", mtu, result);
                    if (result < 0) audio_failure(3, result);
                }
            }
            if (p->role && p->authenticated) {
                const TickType_t now = xTaskGetTickCount();
                if (p->audio_connected && !hfp_dlc_connected && !hfp_dlc_connecting &&
                    !hfp_sdp_pending && !hfp_sdp_done &&
                    (int32_t)(now - hfp_sdp_at) >= 0) {
                    hfp_acl_conn = connection;
                    hfp_sdp_pending = true;
                    const int sdp_err = bt_sdp_discover(connection, &hfp_sdp_params);
                    if (sdp_err != 0) {
                        hfp_sdp_pending = false;
                        hfp_sdp_done = true;
                        printf("[RADIO] HFP SDP discover err=%d\r\n", sdp_err);
                    }
                }
                if (hfp_remote_rfcomm_chan != 0U && !hfp_dlc_connected && !hfp_dlc_connecting) {
                    const uint8_t ch = hfp_remote_rfcomm_chan;
                    hfp_remote_rfcomm_chan = 0U;
                    memset(&hfp_dlc, 0, sizeof(hfp_dlc));
                    hfp_dlc.ops = &hfp_dlc_ops;
                    hfp_dlc.mtu = 127U;
                    hfp_dlc.required_sec_level = BT_SECURITY_L2;
                    hfp_dlc_connecting = true;
                    const int rf_err = bt_rfcomm_dlc_connect(connection, &hfp_dlc, ch);
                    if (rf_err != 0) {
                        hfp_dlc_connecting = false;
                        printf("[RADIO] HFP RFCOMM connect ch=%u err=%d\r\n", ch, rf_err);
                    }
                }
                uint8_t s_state = BT_CONN_DISCONNECTED;
                uint16_t s_handle = 0U;
                bool s_present = false;
                if (sco_active_conn != NULL) {
                    s_state = sco_active_conn->state;
                    s_handle = sco_active_conn->handle;
                    if (s_state == BT_CONN_CONNECTED || s_state == BT_CONN_CONNECT) {
                        s_present = true;
                    } else {
                        release_sco_active_conn();
                    }
                }
                if (s_present && s_state == BT_CONN_CONNECTED) {
                    if (!sco_connected) {
                        sco_connected = true;
                        sco_opening = false;
                        sco_disconnect_sent = false;
                        sco_msbc_active = hfp_msbc_negotiated_for_hci;
                        hfp_ag_sync_mic_state();
                        printf("[RADIO] SCO connected handle=%u codec=%s\r\n",
                               (unsigned)s_handle, sco_msbc_active ? "mSBC" : "CVSD");
                    }
                } else if (s_present && s_state == BT_CONN_CONNECT) {
                    sco_opening = true;
                } else {
                    if (sco_connected) {
                        sco_connected = false;
                        sco_opening = false;
                        sco_disconnect_sent = false;
                        sco_msbc_active = false;
                        hfp_ag_sync_mic_state();
                        printf("[RADIO] SCO disconnected\r\n");
                    } else if (sco_opening) {
                        sco_opening = false;
                        sco_disconnect_sent = false;
                        sco_msbc_active = false;
                        if (hfp_msbc_negotiated_for_hci && !hfp_remote_msbc_retry_fallback) {
                            hfp_remote_msbc_retry_fallback = true;
                            hfp_ag.hf_codec_mask &= (uint8_t)~0x02U;
                            hfp_ag.selected_codec = 0U;
                            hfp_bcs_wait_until = 0;
                            sco_retry_at = now + pdMS_TO_TICKS(250U);
                        } else {
                            sco_retry_at = now + pdMS_TO_TICKS(2000U);
                        }
                        hfp_ag_sync_mic_state();
                        printf("[RADIO] SCO connect failed msbc=%u fallback=%u\r\n",
                               hfp_msbc_negotiated_for_hci ? 1U : 0U,
                               hfp_remote_msbc_retry_fallback ? 1U : 0U);
                    }
                }

                const bool want_sco = dongle_mic_pipeline_desired_hfp_sco();
                if (want_sco && hfp_ag.slc_ready) {
                    if (!s_present && !sco_connected && !sco_opening &&
                        (int32_t)(now - sco_retry_at) >= 0) {
                        if (dongle_usb_audio_is_streaming() || audio_state == 5U) {
                            a2dp_suspended_for_mic = true;
                            (void)bt_stream_suspend(connection);
                            sco_retry_at = now + pdMS_TO_TICKS(150U);
                        } else if (hfp_ag.selected_codec == 0U) {
                            if (!hfp_ag.bcs_pending && hfp_bcs_wait_until == 0) {
                                const dongle_hfp_ag_action_t act = dongle_hfp_ag_request_codec(
                                    &hfp_ag, hfp_rfcomm_tx, NULL);
                                if (act == DONGLE_HFP_AG_ACTION_NONE) {
                                    hfp_bcs_wait_until = now + pdMS_TO_TICKS(500U);
                                } else {
                                    hfp_ag_sync_mic_state();
                                }
                            } else if (hfp_bcs_wait_until != 0 &&
                                       (int32_t)(now - hfp_bcs_wait_until) >= 0) {
                                hfp_ag.hf_codec_mask &= (uint8_t)~0x02U;
                                hfp_remote_msbc_retry_fallback = true;
                                hfp_ag.selected_codec = 1U;
                                hfp_bcs_wait_until = 0;
                                hfp_ag_sync_mic_state();
                            }
                        } else {
                            const bool use_msbc = !hfp_remote_msbc_retry_fallback &&
                                                  hfp_ag.selected_codec == 2U;
                            hfp_msbc_negotiated_for_hci = use_msbc;
                            hfp_ag_sync_mic_state();
                            const struct esco_para *para = use_msbc
                                ? ESCO_PARAM(8000U, 8000U, 13U, 0x0063U, 2U,
                                             HCI_PKT_TYPE_ESCO_EV3 | HCI_PKT_TYPE_ESCO_3EV3 |
                                             HCI_PKT_TYPE_ESCO_2EV5 | HCI_PKT_TYPE_ESCO_3EV5)
                                : ESCO_PARAM(8000U, 8000U, 12U, 0x0060U, 2U,
                                             HCI_PKT_TYPE_ESCO_EV3 | HCI_PKT_TYPE_ESCO_3EV3 |
                                             HCI_PKT_TYPE_ESCO_2EV5 | HCI_PKT_TYPE_ESCO_3EV5);
                            release_sco_active_conn();
                            struct bt_conn *created = bt_conn_create_sco(&p->address, para);
                            if (created != NULL) {
                                sco_active_conn = created;
                                sco_opening = true;
                                sco_disconnect_sent = false;
                                sco_retry_at = now + pdMS_TO_TICKS(3000U);
                                printf("[RADIO] SCO opening codec=%s\r\n", use_msbc ? "mSBC" : "CVSD");
                            } else if (use_msbc) {
                                hfp_remote_msbc_retry_fallback = true;
                                hfp_ag.hf_codec_mask &= (uint8_t)~0x02U;
                                hfp_ag.selected_codec = 0U;
                                hfp_bcs_wait_until = 0;
                                sco_retry_at = now + pdMS_TO_TICKS(300U);
                            } else {
                                sco_retry_at = now + pdMS_TO_TICKS(2000U);
                            }
                        }
                    }
                } else {
                    if (s_present && s_state == BT_CONN_CONNECTED && !sco_disconnect_sent) {
                        sco_disconnect_sent = true;
                        (void)sco_hci_disconnect(s_handle, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
                        printf("[RADIO] SCO disconnecting\r\n");
                    } else if (!s_present && !sco_connected && !sco_opening &&
                               a2dp_suspended_for_mic && !want_sco) {
                        a2dp_suspended_for_mic = false;
                        if (p->audio_connected && dongle_usb_audio_playback_open()) {
                            (void)bt_stream_resume(connection);
                        }
                    }
                }
            }
            manage_role(p, connection);
            if (p->security_pending && p->role_stage != 1 && p->role_stage != 2) {
                p->security_pending = false;
                /* This SDK can retain required_sec_level across reconnects. */
                connection->required_sec_level = BT_SECURITY_L1;
                int err = bt_conn_set_security(connection, BT_SECURITY_L2);
                if (err && err != -EINPROGRESS) { last_error = (uint8_t)-err; if (p->role) audio_failure(2, err); }
                /* A bonded reconnect may already be encrypted. The SDK then
                 * returns success without emitting another security callback. */
                if (!err && !p->authenticated && p->conn == connection &&
                    bt_conn_get_security(connection) >= BT_SECURITY_L2)
                    security_changed(connection, bt_conn_get_security(connection), 0);
            }
            if (!p->role && p->authenticated && p->saved &&
                p->control_open && p->interrupt_open && p->opening) {
                const TickType_t now = xTaskGetTickCount();
                if ((int32_t)(now - p->ds_probe_at) >= 0) {
                    if (p->ds_probe_stage == 1U || p->ds_probe_stage == 2U) {
                        const uint8_t probe_id = p->ds_probe_stage == 1U ? 0x20U : 0x09U;
                        (void)ds_send_l2cap_report(&p->control.chan, 0x43U, &probe_id, 1U);
                        ++p->ds_probe_stage;
                        p->ds_probe_at = now + pdMS_TO_TICKS(100);
                    } else if (p->ds_probe_stage == 3U) {
                        uint8_t seq = 0U;
                        if (p->slot < 2U) {
                            const bool locked = ds_lock();
                            seq = ds_out_seq[p->slot]++;
                            ds_unlock(locked);
                        }
                        uint8_t bt_rep[DONGLE_DUALSENSE_BT_OUTPUT_SIZE];
                        dongle_dualsense_bt_unlock_leds(seq, bt_rep);
                        if (ds_send_l2cap_report(&p->interrupt.chan, 0xa2U,
                                                 bt_rep, sizeof(bt_rep)) >= 0) {
                            p->ds_led_unlocked = true;
                        }
                        p->ds_probe_stage = 4U;
                        p->ds_probe_at = now + pdMS_TO_TICKS(200);
                    } else if (p->ds_probe_stage == 4U) {
                        if ((!p->ds_feat_05_valid || !p->ds_feat_20_valid ||
                             !p->ds_feat_09_valid) && p->ds_probe_retries < 3U) {
                            const uint8_t retry_id = !p->ds_feat_05_valid ? 0x05U
                                : (!p->ds_feat_20_valid ? 0x20U : 0x09U);
                            ++p->ds_probe_retries;
                            (void)ds_send_l2cap_report(&p->control.chan, 0x43U, &retry_id, 1U);
                            p->ds_probe_at = now + pdMS_TO_TICKS(400);
                        } else {
                            p->ds_probe_stage = 5U;
                        }
                    }
                }
                if (p->slot < 2U) {
                    uint8_t feat_copy[64];
                    uint8_t feat_len = 0U;
                    bool has_feat_tx = false;
                    bool locked = ds_lock();
                    if (ds_feat_tx_pending[p->slot]) {
                        feat_len = ds_feat_tx_len[p->slot];
                        if (feat_len <= sizeof(feat_copy)) {
                            memcpy(feat_copy, ds_feat_tx_buf[p->slot], feat_len);
                            has_feat_tx = true;
                        }
                        ds_feat_tx_pending[p->slot] = false;
                    }
                    ds_unlock(locked);
                    if (has_feat_tx) {
                        uint8_t bt_feat[64];
                        const size_t bt_len = dongle_dualsense_usb_to_bt_feature(
                            feat_copy, feat_len, bt_feat, sizeof(bt_feat));
                        if (bt_len >= 6U &&
                            ds_send_l2cap_report(&p->control.chan, 0x53U,
                                                 bt_feat, bt_len) >= 0) {
                            locked = ds_lock();
                            ++ds_stats[p->slot].bt_feat_set_ok;
                            ds_unlock(locked);
                            if (bt_feat[0] == 0x80U) {
                                p->ds_80_await_handshake = true;
                                p->ds_80_poll_at = now + pdMS_TO_TICKS(25);
                                p->ds_80_repoll_left = 5U;
                            } else if (bt_feat[0] == 0x08U) {
                                p->ds_pending_get_id = 0x08U;
                            }
                        }
                    }
                    if (p->ds_80_await_handshake &&
                        (int32_t)(now - p->ds_80_poll_at) >= 0) {
                        p->ds_80_await_handshake = false;
                        p->ds_pending_get_id = 0x81U;
                    }
                    uint8_t req_id = 0U;
                    locked = ds_lock();
                    if (p->ds_pending_get_id != 0U) {
                        req_id = p->ds_pending_get_id;
                        p->ds_pending_get_id = 0U;
                    } else if (ds_feat_get_req[p->slot] != 0U) {
                        req_id = ds_feat_get_req[p->slot];
                        ds_feat_get_req[p->slot] = 0U;
                    }
                    ds_unlock(locked);
                    if (req_id != 0U) {
                        (void)ds_send_l2cap_report(&p->control.chan, 0x43U, &req_id, 1U);
                    }

                    const int8_t desired_mic_slot = dongle_mic_pipeline_desired_ds_slot();
                    const bool want_ds_mic = p->slot < 2U && desired_mic_slot == (int8_t)p->slot;
                    if (p->slot < 2U && p->ds_led_unlocked &&
                        want_ds_mic && !ds_mic_unmuted[p->slot] &&
                        (int32_t)(now - ds_mic_retry_at[p->slot]) >= 0) {
                        uint8_t bt_preamp[DONGLE_DUALSENSE_BT_OUTPUT_SIZE];
                        locked = ds_lock();
                        const uint8_t mic_seq = ds_out_seq[p->slot];
                        ds_unlock(locked);
                        dongle_dualsense_bt_mic_preamp_report(
                            mic_seq, true, false, bt_preamp);
                        const int mic_err = ds_send_l2cap_report(
                            &p->interrupt.chan, 0xa2U, bt_preamp, sizeof(bt_preamp));
                        printf("[RADIO] DS mic preamp slot=%u err=%d\r\n",
                               (unsigned)p->slot, mic_err);
                        if (mic_err >= 0) {
                            locked = ds_lock();
                            ++ds_out_seq[p->slot];
                            ds_mic_unmuted[p->slot] = true;
                            ds_mic_retry_at[p->slot] = now + pdMS_TO_TICKS(15U);
                            ds_unlock(locked);
                        } else {
                            ds_mic_retry_at[p->slot] = now + pdMS_TO_TICKS(50U);
                        }
                    } else if (p->slot < 2U && p->ds_led_unlocked &&
                               want_ds_mic != ds_mic_enabled[p->slot] &&
                               (int32_t)(now - ds_mic_retry_at[p->slot]) >= 0) {
                        uint8_t bt_mic[DONGLE_DUALSENSE_BT_MIC_REPORT_SIZE];
                        locked = ds_lock();
                        const uint8_t mic_seq = ds_out_seq[p->slot];
                        ds_unlock(locked);
                        dongle_dualsense_bt_mic_control_report(
                            mic_seq, want_ds_mic, false, bt_mic);
                        const int mic_err = ds_send_l2cap_report(
                            &p->interrupt.chan, 0xa2U, bt_mic, sizeof(bt_mic));
                        printf("[RADIO] DS mic toggle slot=%u enable=%u err=%d\r\n",
                               (unsigned)p->slot, want_ds_mic ? 1U : 0U, mic_err);
                        if (mic_err >= 0) {
                            locked = ds_lock();
                            ++ds_out_seq[p->slot];
                            ds_mic_enabled[p->slot] = want_ds_mic;
                            if (want_ds_mic) {
                                ds_mic_pkt_logs = 0U;
                            } else {
                                ds_mic_unmuted[p->slot] = false;
                            }
                            ds_unlock(locked);
                        } else {
                            ds_mic_retry_at[p->slot] = now + pdMS_TO_TICKS(50U);
                        }
                    }

                    uint8_t usb_copy[DONGLE_DUALSENSE_USB_OUTPUT_SIZE];
                    uint8_t usb_len = 0U;
                    uint8_t out_path = 0U;
                    uint32_t out_gen = 0U;
                    bool has_out = false;
                    const uint16_t fw_ver = p->ds_feat_20_valid
                        ? (uint16_t)((uint16_t)p->ds_feat_20[44] |
                                     ((uint16_t)p->ds_feat_20[45] << 8U))
                        : 0x0402U;
                    const TickType_t out_gap = pdMS_TO_TICKS(dongle_usb_audio_is_streaming()
                        ? DS_OUT_MIN_INTERVAL_AUDIO_MS : DS_OUT_MIN_INTERVAL_MS);
                    locked = ds_lock();
                    /* Leave a newer report pending until both interval and any
                     * controller retry backoff elapse; do not drop on -ENOMEM. */
                    if (ds_out_pending[p->slot] &&
                        (int32_t)(now - ds_out_retry_at[p->slot]) >= 0 &&
                        (!ds_out_last_len[p->slot] ||
                         (TickType_t)(now - ds_out_last_at[p->slot]) >= out_gap)) {
                        usb_len = ds_out_len[p->slot];
                        out_path = ds_out_path[p->slot];
                        out_gen = ds_out_gen[p->slot];
                        memcpy(usb_copy, ds_out_buf[p->slot], sizeof(usb_copy));
                        dongle_dualsense_normalize_usb_output(usb_copy, fw_ver);
                        const bool unchanged = usb_len == ds_out_last_len[p->slot] &&
                            !memcmp(usb_copy, ds_out_last[p->slot], usb_len) &&
                            (TickType_t)(now - ds_out_last_at[p->slot]) < pdMS_TO_TICKS(DS_OUT_REFRESH_MS);
                        if (unchanged) {
                            ds_out_pending[p->slot] = false;
                        } else {
                            has_out = true;
                        }
                    }
                    const uint8_t seq = ds_out_seq[p->slot];
                    ds_unlock(locked);
                    if (has_out) {
                        uint8_t bt_rep[DONGLE_DUALSENSE_BT_OUTPUT_SIZE];
                        if (dongle_dualsense_usb_to_bt_output(usb_copy, usb_len, seq, bt_rep)) {
                            const int tx_err = ds_send_l2cap_report(
                                &p->interrupt.chan, 0xa2U, bt_rep, sizeof(bt_rep));
                            locked = ds_lock();
                            if (tx_err >= 0) {
                                memcpy(ds_out_last[p->slot], usb_copy, usb_len);
                                ds_out_last_len[p->slot] = usb_len;
                                ds_out_last_at[p->slot] = now;
                                ++ds_out_seq[p->slot];
                                if (ds_out_gen[p->slot] == out_gen) {
                                    ds_out_pending[p->slot] = false;
                                }
                                ++ds_stats[p->slot].bt_out_31_ok;
                                ds_stats[p->slot].last_valid_flag0 = usb_copy[1];
                                ds_stats[p->slot].last_valid_flag1 = usb_copy[2];
                                ds_stats[p->slot].last_motor_right = usb_copy[3];
                                ds_stats[p->slot].last_motor_left = usb_copy[4];
                                ds_stats[p->slot].last_right_trigger_mode = usb_copy[11];
                                ds_stats[p->slot].last_left_trigger_mode = usb_copy[22];
                                ds_stats[p->slot].last_out_path_flags = out_path;
                            } else {
                                ++ds_stats[p->slot].bt_out_31_err;
                                ds_out_retry_at[p->slot] = now + pdMS_TO_TICKS(8U);
                            }
                            ds_unlock(locked);
                        } else {
                            locked = ds_lock();
                            if (ds_out_gen[p->slot] == out_gen) {
                                ds_out_pending[p->slot] = false;
                            }
                            ds_unlock(locked);
                        }
                    }
                }
            }
            if (!p->authenticated || !p->profile_pending || p->conn != connection) { bt_conn_unref(connection); continue; }
            p->profile_pending = false;
            if (!p->saved) save_peer(p);
            if (!p->saved) { bt_conn_unref(connection); continue; }
            if (p->role) {
                if (!p->opening) { p->opening = true; if (!bt_a2dp_connect(connection)) audio_failure(3, EIO); }
            } else if (p->outgoing && !p->control_open && !p->control.chan.conn) {
                int err = bt_l2cap_chan_connect(connection, &p->control.chan, 0x11);
                if (err) last_error = (uint8_t)-err;
            } else if (p->outgoing && p->control_open && !p->interrupt_open && !p->interrupt.chan.conn) {
                int err = bt_l2cap_chan_connect(connection, &p->interrupt.chan, 0x13);
                if (err) last_error = (uint8_t)-err;
            } else if (p->control_open && p->interrupt_open && !p->opening) {
                const uint8_t probe_05 = 0x05U;
                if (ds_send_l2cap_report(&p->control.chan, 0x43U, &probe_05, 1U) >= 0) {
                    p->opening = true;
                    p->ds_probe_stage = 1U;
                    p->ds_probe_retries = 0U;
                    p->ds_probe_at = xTaskGetTickCount() + pdMS_TO_TICKS(100);
                }
            }
            bt_conn_unref(connection);
        }
        /* Retry saved peers when they become available after boot. One page
         * at a time keeps the controller responsive to incoming connections. */
        bool paging = false;
        bool headphone_link = false;
        for (unsigned i = 0; i < PEERS; ++i) {
            paging |= peers[i].connecting;
            headphone_link |= peers[i].role && peers[i].conn;
        }
        if (!scan_active && !paging && !dongle_usb_ota_active()) for (unsigned i = 0; i < PEERS; ++i) {
            peer_t *p = &peers[i];
            if (!p->reconnect || p->conn || p->control.chan.conn || p->interrupt.chan.conn ||
                (int32_t)(xTaskGetTickCount() - p->retry_at) < 0) continue;
            /* Unanswered controller pages measurably starve SBC transport.
             * With a headphone link, retain page scan for the controller's
             * PS-button reconnect instead of repeatedly paging it ourselves. */
            if (!p->role && headphone_link) continue;
            operation_t retry = {.command = p->role ? 0x19 : 0x14};
            address_out(retry.address, &p->address);
            printf("[RADIO] reconnect role=%u\r\n", p->role);
            start_connection(&retry);
            break;
        }
    }
}
bool dongle_bluetooth_ready(void) { return ready; }
uint8_t dongle_bluetooth_connected(void)
{
    uint8_t mask = 0;
    for (unsigned i = 0; i < PEERS; ++i)
        if (!peers[i].role && peers[i].interrupt_open && peers[i].slot < 2) mask |= 1U << peers[i].slot;
    return mask;
}
uint8_t dongle_bluetooth_connected_devices(void)
{
    taskENTER_CRITICAL();
    uint8_t mask = dongle_bluetooth_connected();
    for (unsigned i = 0; i < PEERS; ++i)
        if (peers[i].role && peers[i].conn && peers[i].authenticated) mask |= 4U;
    taskEXIT_CRITICAL();
    return mask;
}
void dongle_bluetooth_bind_blocklist(dongle_device_blocklist_t *blocklist)
{
    blocklist_ref = blocklist;
}
uint8_t dongle_bluetooth_remove_audio(void)
{
    uint8_t old_hp[6] = { 0 };
    taskENTER_CRITICAL();
    memcpy(old_hp, headphone, sizeof(old_hp));
    for (unsigned i = 0U; i < PEERS; ++i) {
        if (peers[i].role) {
            if (!dongle_catalog_address_is_valid(old_hp)) {
                address_out(old_hp, &peers[i].address);
            }
            peers[i].saved = false;
            peers[i].reconnect = false;
        }
    }
    memset(headphone, 0, sizeof(headphone));
    memset(audio_target, 0, sizeof(audio_target));
    audio_state = 0U;
    audio_error_stage = 0U;
    audio_error = 0U;
    taskEXIT_CRITICAL();

    if (ef_del_env("dongle-headphone") != EF_NO_ERR) {
        return DONGLE_USB_OTA_STATUS_STORAGE_ERROR;
    }
    if (dongle_catalog_address_is_valid(old_hp) && operations != NULL) {
        operation_t op = { .command = 0x1bU };
        memcpy(op.address, old_hp, sizeof(op.address));
        (void)xQueueSend(operations, &op, 0);
    }
    return DONGLE_USB_OTA_STATUS_OK;
}
void dongle_bluetooth_disconnect_address(const uint8_t address[6])
{
    if (address == NULL || !dongle_catalog_address_is_valid(address)) {
        return;
    }
    dongle_bluetooth_removed(address);
}
void dongle_bluetooth_removed(const uint8_t address[6])
{
    operation_t op = {.command = 0x1b}; memcpy(op.address, address, 6);
    if (operations) xQueueSend(operations, &op, 0);
}
void dongle_bluetooth_audio_metrics(uint8_t out[12])
{
    uint16_t peak_l = 0U;
    uint16_t peak_r = 0U;
    uint32_t nonzero = 0U;
    dongle_usb_audio_metrics(&peak_l, &peak_r, &nonzero);
    out[0] = 0U;
    out[1] = (uint8_t)((avrcp_connected ? 0x01U : 0U) |
                       (hfp_ag.slc_ready ? 0x02U : 0U) |
                       (sco_connected ? 0x04U : 0U) |
                       ((sco_connected && sco_msbc_active) ? 0x08U : 0U));
    out[2] = avrcp_volume;
    out[3] = (uint8_t)(peak_l & 0xffU);
    out[4] = (uint8_t)(peak_l >> 8U);
    out[5] = (uint8_t)(peak_r & 0xffU);
    out[6] = (uint8_t)(peak_r >> 8U);
    out[7] = (uint8_t)(nonzero & 0xffU);
    out[8] = (uint8_t)((nonzero >> 8U) & 0xffU);
    out[9] = (uint8_t)((nonzero >> 16U) & 0xffU);
    out[10] = (uint8_t)((nonzero >> 24U) & 0xffU);
    uint8_t link_roles = 0U;
    taskENTER_CRITICAL();
    for (unsigned i = 0U; i < PEERS; ++i) {
        if (!peers[i].conn) {
            continue;
        }
        const uint8_t enc = peers[i].conn->role == BT_HCI_ROLE_MASTER ? 1U : 2U;
        if (peers[i].role) {
            link_roles |= enc;
        } else if (peers[i].slot < 2U) {
            link_roles |= (uint8_t)(enc << (2U + peers[i].slot * 2U));
        }
    }
    taskEXIT_CRITICAL();
    out[11] = link_roles;
}
void dongle_bluetooth_local_address(uint8_t out[6])
{
    if (out) memcpy(out, local_address, 6U);
}
static void ds_write_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
    p[2] = (uint8_t)(value >> 16U);
    p[3] = (uint8_t)(value >> 24U);
}
static void ds_write_le16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
}
size_t dongle_bluetooth_dualsense_feature(
    uint8_t slot, uint8_t report_id, uint8_t *out, size_t capacity)
{
    if (slot >= 2U || !out || !capacity) return 0U;
    size_t copied = 0U;
    const TickType_t now = ds_now_ticks();
    const bool locked = ds_lock();
    if (ds_feat_81_busy[slot] && (int32_t)(now - ds_feat_81_deadline[slot]) >= 0) {
        ds_feat_81_busy[slot] = false;
    }
    for (unsigned i = 0U; i < PEERS; ++i) {
        const peer_t *p = &peers[i];
        if (p->role || p->slot != slot || !p->conn) continue;
        if (report_id == 0x05U && p->ds_feat_05_valid && capacity >= sizeof(p->ds_feat_05)) {
            memcpy(out, p->ds_feat_05, sizeof(p->ds_feat_05));
            copied = sizeof(p->ds_feat_05);
        } else if (report_id == 0x08U && p->ds_feat_08_valid && capacity >= sizeof(p->ds_feat_08)) {
            memcpy(out, p->ds_feat_08, sizeof(p->ds_feat_08));
            copied = sizeof(p->ds_feat_08);
        } else if (report_id == 0x09U && p->ds_feat_09_valid && capacity >= sizeof(p->ds_feat_09)) {
            memcpy(out, p->ds_feat_09, sizeof(p->ds_feat_09));
            copied = sizeof(p->ds_feat_09);
        } else if (report_id == 0x20U && p->ds_feat_20_valid && capacity >= sizeof(p->ds_feat_20)) {
            memcpy(out, p->ds_feat_20, sizeof(p->ds_feat_20));
            copied = sizeof(p->ds_feat_20);
        } else if (report_id == 0x81U && p->ds_feat_81_valid && capacity >= sizeof(p->ds_feat_81)) {
            memcpy(out, p->ds_feat_81, sizeof(p->ds_feat_81));
            copied = sizeof(p->ds_feat_81);
        } else if (report_id == 0x81U && ds_feat_81_busy[slot] && capacity >= 64U) {
            memset(out, 0, 64U);
            out[0] = 0x81U;
            out[1] = ds_feat_last_80[slot][0];
            out[2] = ds_feat_last_80[slot][1];
            out[3] = 0x01U;
            copied = 64U;
        } else if (p->control_open && p->authenticated &&
                   dongle_dualsense_feature_size(report_id) != 0U &&
                   report_id != 0x80U) {
            ds_feat_get_req[slot] = report_id;
        }
        break;
    }
    ds_unlock(locked);
    return copied;
}
void dongle_bluetooth_send_dualsense_output_ex(
    uint8_t slot, const uint8_t *usb_report, size_t size, uint8_t path_flags)
{
    if (slot >= 2U || !usb_report || size < DONGLE_DUALSENSE_USB_OUTPUT_SIZE ||
        usb_report[0] != 0x02U) {
        return;
    }
    const uint8_t len = (uint8_t)(size > DONGLE_DUALSENSE_USB_OUTPUT_SIZE
        ? DONGLE_DUALSENSE_USB_OUTPUT_SIZE : size);
    const bool locked = ds_lock();
    if (ds_out_pending[slot] &&
        ds_out_len[slot] == DONGLE_DUALSENSE_USB_OUTPUT_SIZE &&
        len == DONGLE_DUALSENSE_USB_OUTPUT_SIZE) {
        dongle_dualsense_merge_usb_output(ds_out_buf[slot], usb_report);
    } else {
        memcpy(ds_out_buf[slot], usb_report, len);
        ds_out_len[slot] = len;
        ds_out_pending[slot] = true;
    }
    ++ds_out_gen[slot];
    ds_out_path[slot] |= path_flags;
    ++ds_stats[slot].usb_out_02;
    ds_unlock(locked);
}
void dongle_bluetooth_send_dualsense_output(
    uint8_t slot, const uint8_t *usb_report, size_t size)
{
    dongle_bluetooth_send_dualsense_output_ex(slot, usb_report, size, 0x01U);
}
bool dongle_bluetooth_send_dualsense_feature(
    uint8_t slot, const uint8_t *usb_report, size_t size)
{
    if (slot >= 2U || !usb_report || size < 2U) {
        return false;
    }
    const uint8_t id = usb_report[0];
    const size_t expected = dongle_dualsense_feature_size(id);
    if ((id != 0x80U && id != 0x08U) || expected < 6U ||
        size < expected - 4U || size > sizeof(ds_feat_tx_buf[slot])) {
        return false;
    }
    const uint8_t len = (uint8_t)(size > expected ? expected : size);
    const TickType_t now = ds_now_ticks();
    const bool locked = ds_lock();
    memcpy(ds_feat_tx_buf[slot], usb_report, len);
    ds_feat_tx_len[slot] = len;
    ds_feat_tx_pending[slot] = true;
    if (id == 0x80U) {
        ds_feat_last_80[slot][0] = usb_report[1];
        ds_feat_last_80[slot][1] = size >= 3U ? usb_report[2] : 0U;
        ds_feat_81_busy[slot] = true;
        ds_feat_81_deadline[slot] = now + pdMS_TO_TICKS(500);
        for (unsigned i = 0U; i < PEERS; ++i) {
            if (!peers[i].role && peers[i].slot == slot) {
                peers[i].ds_feat_81_valid = false;
            }
        }
    }
    ds_unlock(locked);
    return true;
}
bool dongle_bluetooth_dualsense_metrics(
    uint8_t slot, uint8_t out[DONGLE_DUALSENSE_METRICS_PAYLOAD_SIZE])
{
    if (slot >= 2U || !out) {
        return false;
    }
    const bool locked = ds_lock();
    const peer_t *p = NULL;
    for (unsigned i = 0U; i < PEERS; ++i) {
        if (!peers[i].role && peers[i].slot == slot) {
            p = &peers[i];
            break;
        }
    }
    const uint8_t conn_flags = (p ? (
        (p->conn ? 0x01U : 0U) |
        (p->authenticated ? 0x02U : 0U) |
        (p->control_open ? 0x04U : 0U) |
        (p->interrupt_open ? 0x08U : 0U) |
        (p->opening ? 0x10U : 0U) |
        (p->saved ? 0x20U : 0U)) : 0U) |
        (ds_feat_81_busy[slot] ? 0x40U : 0U);
    const uint8_t feat_mask = p ? (
        (p->ds_feat_05_valid ? 0x01U : 0U) |
        (p->ds_feat_09_valid ? 0x02U : 0U) |
        (p->ds_feat_20_valid ? 0x04U : 0U) |
        (p->ds_feat_08_valid ? 0x08U : 0U) |
        (p->ds_feat_81_valid ? 0x10U : 0U) |
        (p->ds_led_unlocked ? 0x20U : 0U)) : 0U;
    out[0] = 0U;
    out[1] = slot;
    out[2] = conn_flags;
    out[3] = feat_mask;
    ds_write_le32(&out[4], ds_stats[slot].bt_in_31);
    ds_write_le32(&out[8], ds_stats[slot].bt_in_01);
    ds_write_le32(&out[12], ds_stats[slot].usb_out_02);
    ds_write_le32(&out[16], ds_stats[slot].bt_out_31_ok);
    ds_write_le32(&out[20], ds_stats[slot].bt_out_31_err);
    ds_write_le16(&out[24], ds_stats[slot].bt_feat_set_ok);
    ds_write_le16(&out[26], ds_stats[slot].bt_feat_get_ok);
    out[28] = ds_stats[slot].last_feat_id;
    out[29] = ds_stats[slot].last_handshake;
    out[30] = ds_stats[slot].last_valid_flag0;
    out[31] = ds_stats[slot].last_valid_flag1;
    out[32] = ds_stats[slot].last_motor_right;
    out[33] = ds_stats[slot].last_motor_left;
    out[34] = ds_stats[slot].last_right_trigger_mode;
    out[35] = ds_stats[slot].last_left_trigger_mode;
    out[36] = ds_stats[slot].last_out_path_flags;
    out[37] = (p && p->ds_feat_20_valid) ? p->ds_feat_20[44] : 0U;
    ds_unlock(locked);
    return true;
}
size_t dongle_bluetooth_command(uint8_t command, const uint8_t *data, size_t size, uint8_t response[44])
{
    response[0] = 0;
    if (!ready) { response[0] = 8; return 1; }
    if (command == 0x1c && size == 0) {
        taskENTER_CRITICAL();
        media_capture_size = 0; media_capture_armed = true;
        taskEXIT_CRITICAL();
        return 1;
    }
    if (command == 0x1d && size == 2) {
        uint16_t offset = data[0] | (uint16_t)data[1] << 8;
        taskENTER_CRITICAL();
        uint16_t length = media_capture_size;
        if (!length || offset >= length) {
            response[0] = length ? 1 : 3;
            taskEXIT_CRITICAL(); return 1;
        }
        uint16_t count = length - offset;
        if (count > 39) count = 39;
        response[1] = length; response[2] = length >> 8;
        response[3] = offset; response[4] = offset >> 8;
        memcpy(response + 5, media_capture + offset, count);
        taskEXIT_CRITICAL(); return count + 5;
    }
    if (command == 0x17 && size == 1) {
        taskENTER_CRITICAL();
        if (data[0] >= found_count) { response[0] = 10; taskEXIT_CRITICAL(); return 1; }
        unsigned i = data[0]; response[1] = i;
        memcpy(response+2, found[i].address, 6); response[8] = (uint8_t)found[i].rssi;
        memcpy(response+9, found[i].name, 30);
        taskEXIT_CRITICAL(); return 39;
    }
    if (command == 0x18 && !size) {
        response[1] = ready; response[2] = scan_active; response[3] = found_count;
        response[4] = last_error; response[5] = dongle_bluetooth_connected();
        response[6] = dongle_usb_audio_is_streaming(); memcpy(response+7, headphone, 6);
        dongle_usb_audio_diagnostics(response+13);
        response[33] = audio_state; response[34] = audio_error_stage; response[35] = audio_error;
        memcpy(response+36, audio_target, 6);
        response[42] = response[43] = 0;
        for (unsigned i = 0; i < PEERS; ++i) if (peers[i].role && peers[i].conn) {
            response[42] = 1; response[43] = peers[i].authenticated;
        }
        return 44;
    }
    if ((command == 0x13 && !size) || ((command == 0x14 || command == 0x19 || command == 0x1a) && size == 6 && dongle_catalog_address_is_valid(data))) {
        if ((command == 0x14 || command == 0x19) && is_host_addr_blocked(data)) {
            response[0] = DONGLE_USB_OTA_STATUS_UNSUPPORTED;
            return 1;
        }
        if (scan_active) { response[0] = 3; return 1; }
        operation_t op = {.command = command};
        if (size) memcpy(op.address, data, 6);
        if (command == 0x13) { scan_active = 1; found_count = 0; }
        if (xQueueSend(operations, &op, 0) != pdTRUE) { if (command == 0x13) scan_active = 0; response[0] = 3; }
        return 1;
    }
    response[0] = 1; return 1;
}
int dongle_bluetooth_start(dongle_device_catalog_t *devices, dongle_settings_store_t *settings, SemaphoreHandle_t mutex)
{
    catalog = devices; store = settings; catalog_lock = mutex;
    ds_stats[0].last_handshake = 0xffU;
    ds_stats[1].last_handshake = 0xffU;
    dongle_hfp_ag_init(&hfp_ag);
    dongle_bluetooth_storage_bind(store);
    ready_signal = xSemaphoreCreateBinary(); operations = xQueueCreate(4, sizeof(operation_t));
    if (!ready_signal || !operations) return -ENOMEM;
    if (rfparam_init(0, NULL, 0)) return -EIO;
    net_buf_init(&hid_tx_pool, 12, BT_L2CAP_BUF_SIZE(HID_MTU), NULL);
    net_buf_init(&hid_rx_pool, 10, BT_L2CAP_BUF_SIZE(HID_MTU), NULL);
    btble_controller_init(configMAX_PRIORITIES - 1);
    hci_driver_init();
    bt_conn_cb_register(&connection_callbacks); bt_conn_auth_cb_register(&authentication);
    int err = bt_enable(enabled);
    if (err) return err;
    if (xSemaphoreTake(ready_signal, pdMS_TO_TICKS(15000)) != pdTRUE || !ready) return -ETIMEDOUT;
    bt_addr_le_t local; bt_get_local_public_address(&local);
    uint8_t zero[6] = {0};
    if (!memcmp(local.a.val, zero, 6)) {
        uint8_t chipid[8]; bflb_efuse_get_chipid(chipid);
        memcpy(local.a.val, chipid, 6); local.a.val[5] |= 0xc0;
        bt_set_bd_addr(&local.a); bt_set_local_public_address(local.a.val);
    }
    address_out(local_address, &local.a);
    printf("[RADIO] ready %02x:%02x:%02x:%02x:%02x:%02x\r\n", local.a.val[5],local.a.val[4],local.a.val[3],local.a.val[2],local.a.val[1],local.a.val[0]);
    a2dp_cb_register(&audio_callbacks);
#ifdef CONFIG_BT_AVRCP
    avrcp_cb_register(&avrcp_callbacks);
#endif
    bt_a2dp_discovery_cb_register(audio_discovered);
    bt_l2cap_br_server_register(&control_server); bt_l2cap_br_server_register(&input_server);
    (void)bt_sdp_register_service(&hfp_ag_record);
    (void)bt_rfcomm_server_register(&hfp_server);
    bt_br_set_connectable(true);
    GLB_Set_USB_CLK_From_WIFIPLL(1);
    ef_get_env_blob("dongle-headphone", headphone, 6, NULL);
    if (dongle_catalog_address_is_valid(headphone) && is_host_addr_blocked(headphone)) {
        memset(headphone, 0, sizeof(headphone));
    }
    for (unsigned i = 0; i < 2; ++i) {
        if (catalog->controllers[i].paired &&
            !is_host_addr_blocked(catalog->controllers[i].address)) {
            peers[i].slot = i; peers[i].saved = peers[i].reconnect = true;
            peers[i].retry_at = xTaskGetTickCount() + pdMS_TO_TICKS(1500 + i * 1000);
            for (unsigned j = 0; j < 6; ++j) peers[i].address.val[j] = catalog->controllers[i].address[5-j];
            setup_channels(&peers[i]);
        }
    }
    if (dongle_catalog_address_is_valid(headphone)) {
        for (unsigned i = 0; i < PEERS; ++i) {
            if (!peers[i].saved) {
                peers[i].role = 1; peers[i].slot = 0xff; peers[i].saved = peers[i].reconnect = true;
                peers[i].retry_at = xTaskGetTickCount() + pdMS_TO_TICKS(1000);
                for (unsigned j = 0; j < 6; ++j) peers[i].address.val[j] = headphone[5-j];
                break;
            }
        }
    }
    if (xTaskCreate(radio_task, "radio", 2048, NULL, 4, NULL) != pdPASS) { ready = false; return -ENOMEM; }
    return 0;
}
