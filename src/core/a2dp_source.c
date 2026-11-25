#include "dongle/a2dp_source.h"

#include <string.h>

#define AVDTP_MSG_COMMAND   0x00U
#define AVDTP_MSG_GENERAL_REJECT 0x01U
#define AVDTP_MSG_RESP_ACCEPT 0x02U
#define AVDTP_MSG_RESP_REJECT 0x03U

#define AVDTP_CAT_MEDIA_TRANSPORT 0x01U
#define AVDTP_CAT_MEDIA_CODEC     0x07U
#define AVDTP_CAT_DELAY_REPORTING 0x08U

#define AVDTP_ERR_BAD_HEADER_FORMAT     0x01U
#define AVDTP_ERR_BAD_LENGTH            0x11U
#define AVDTP_ERR_BAD_ACP_SEID          0x12U
#define AVDTP_ERR_SEP_IN_USE            0x13U
#define AVDTP_ERR_BAD_SERV_CATEGORY     0x17U
#define AVDTP_ERR_BAD_STATE             0x31U
#define AVDTP_ERR_NOT_SUPPORTED_COMMAND 0x19U

#define A2DP_CODEC_SBC    0x00U
#define A2DP_CODEC_AAC    0x02U
#define A2DP_CODEC_VENDOR 0xFFU

#define VENDOR_ID_LDAC    0x0000012DU
#define CODEC_ID_LDAC     0x00AAU

#define VENDOR_ID_APTX    0x0000004FU
#define CODEC_ID_APTX     0x0001U

#define VENDOR_ID_APTX_HD 0x000000D7U
#define CODEC_ID_APTX_HD  0x0024U

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8U);
}

static void set_state(dongle_a2dp_source_t *ctx, dongle_a2dp_stream_state_t st)
{
    ctx->state = st;
    if (ctx->cb.on_state != NULL) {
        ctx->cb.on_state(ctx->user, st, (uint8_t)ctx->active_codec,
                         ctx->active_sample_rate_hz);
    }
}

static void notify_error(dongle_a2dp_source_t *ctx, uint8_t sig, uint8_t err)
{
    if (ctx->cb.on_error != NULL) {
        ctx->cb.on_error(ctx->user, sig, err);
    }
}

static uint8_t local_seid_for_codec(dongle_audio_codec_t codec)
{
    switch (codec) {
        case DONGLE_AUDIO_CODEC_LDAC:    return 1U;
        case DONGLE_AUDIO_CODEC_APTX_HD: return 2U;
        case DONGLE_AUDIO_CODEC_APTX:    return 3U;
        case DONGLE_AUDIO_CODEC_AAC:     return 4U;
        case DONGLE_AUDIO_CODEC_SBC:     return 5U;
        default:                         return 0U;
    }
}

static dongle_audio_codec_t codec_for_local_seid(uint8_t seid)
{
    switch (seid) {
        case 1U: return DONGLE_AUDIO_CODEC_LDAC;
        case 2U: return DONGLE_AUDIO_CODEC_APTX_HD;
        case 3U: return DONGLE_AUDIO_CODEC_APTX;
        case 4U: return DONGLE_AUDIO_CODEC_AAC;
        case 5U: return DONGLE_AUDIO_CODEC_SBC;
        default: return DONGLE_AUDIO_CODEC_AUTO;
    }
}

size_t dongle_a2dp_source_local_codec_caps(dongle_audio_codec_t codec,
                                           uint8_t *out,
                                           size_t capacity)
{
    if (out == NULL) {
        return 0U;
    }
    switch (codec) {
        case DONGLE_AUDIO_CODEC_LDAC: {
            /* MediaType(0x00), CodecType(0xFF), Vendor(0x0000012D LE), Codec(0x00AA LE),
             * Freq: 44.1(0x20)|48(0x10)|96(0x04) = 0x34, Ch: Dual(0x02)|Stereo(0x01) = 0x03 */
            static const uint8_t ldac_caps[10] = {
                0x00U, 0xFFU, 0x2DU, 0x01U, 0x00U, 0x00U, 0xAAU, 0x00U, 0x34U, 0x03U
            };
            if (capacity < sizeof(ldac_caps)) return 0U;
            memcpy(out, ldac_caps, sizeof(ldac_caps));
            return sizeof(ldac_caps);
        }
        case DONGLE_AUDIO_CODEC_APTX_HD: {
            /* MediaType(0x00), CodecType(0xFF), Vendor(0x000000D7 LE), Codec(0x0024 LE),
             * Freq+Ch: 44.1(0x20)|48(0x10)|Stereo(0x02) = 0x32, 4 reserved bytes */
            static const uint8_t aptx_hd_caps[13] = {
                0x00U, 0xFFU, 0xD7U, 0x00U, 0x00U, 0x00U, 0x24U, 0x00U, 0x32U,
                0x00U, 0x00U, 0x00U, 0x00U
            };
            if (capacity < sizeof(aptx_hd_caps)) return 0U;
            memcpy(out, aptx_hd_caps, sizeof(aptx_hd_caps));
            return sizeof(aptx_hd_caps);
        }
        case DONGLE_AUDIO_CODEC_APTX: {
            /* MediaType(0x00), CodecType(0xFF), Vendor(0x0000004F LE), Codec(0x0001 LE),
             * Freq+Ch: 44.1(0x20)|48(0x10)|Stereo(0x02) = 0x32 */
            static const uint8_t aptx_caps[9] = {
                0x00U, 0xFFU, 0x4FU, 0x00U, 0x00U, 0x00U, 0x01U, 0x00U, 0x32U
            };
            if (capacity < sizeof(aptx_caps)) return 0U;
            memcpy(out, aptx_caps, sizeof(aptx_caps));
            return sizeof(aptx_caps);
        }
        case DONGLE_AUDIO_CODEC_AAC: {
            /* MediaType(0x00), CodecType(0x02), ObjType(0x80=MPEG2 LC),
             * Freq1(0x01=44.1k), Freq2|Ch(0x80=48k | 0x04=2ch), VBR|256kbps(0x83,0xE8,0x00) */
            static const uint8_t aac_caps[8] = {
                0x00U, 0x02U, 0x80U, 0x01U, 0x84U, 0x83U, 0xE8U, 0x00U
            };
            if (capacity < sizeof(aac_caps)) return 0U;
            memcpy(out, aac_caps, sizeof(aac_caps));
            return sizeof(aac_caps);
        }
        case DONGLE_AUDIO_CODEC_SBC: {
            /* MediaType(0x00), CodecType(0x00), Freq|Ch(44.1|48 + Stereo|Joint = 0x33),
             * Blk|Sub|Alloc(16|8|Loudness = 0x15), MinBP(2), MaxBP(53) */
            static const uint8_t sbc_caps[6] = {
                0x00U, 0x00U, 0x33U, 0x15U, 0x02U, 0x35U
            };
            if (capacity < sizeof(sbc_caps)) return 0U;
            memcpy(out, sbc_caps, sizeof(sbc_caps));
            return sizeof(sbc_caps);
        }
        default:
            return 0U;
    }
}

static int send_cmd(dongle_a2dp_source_t *ctx, uint8_t signal_id,
                    const uint8_t *payload, size_t payload_len)
{
    uint8_t buf[64];
    if (ctx->cb.send_signal == NULL || 2U + payload_len > sizeof(buf)) {
        return -1;
    }
    const uint8_t label = ctx->tx_label & 0x0FU;
    ctx->tx_label = (uint8_t)((ctx->tx_label + 1U) & 0x0FU);
    ctx->pending_label = label;
    ctx->pending_signal = signal_id;

    buf[0] = (uint8_t)((label << 4U) | AVDTP_MSG_COMMAND);
    buf[1] = signal_id;
    if (payload_len > 0U && payload != NULL) {
        memcpy(&buf[2], payload, payload_len);
    }
    return ctx->cb.send_signal(ctx->user, buf, 2U + payload_len);
}

static void send_rsp(dongle_a2dp_source_t *ctx, uint8_t label, uint8_t msg_type,
                     uint8_t signal_id, const uint8_t *payload, size_t payload_len)
{
    uint8_t buf[64];
    if (ctx->cb.send_signal == NULL || 2U + payload_len > sizeof(buf)) {
        return;
    }
    buf[0] = (uint8_t)(((label & 0x0FU) << 4U) | (msg_type & 0x03U));
    buf[1] = signal_id;
    if (payload_len > 0U && payload != NULL) {
        memcpy(&buf[2], payload, payload_len);
    }
    (void)ctx->cb.send_signal(ctx->user, buf, 2U + payload_len);
}

static dongle_audio_codec_t classify_codec(const uint8_t *info, uint8_t len)
{
    if (len < 2U || info[0] != 0x00U) {
        return DONGLE_AUDIO_CODEC_AUTO;
    }
    if (info[1] == A2DP_CODEC_SBC && len >= 6U) {
        return DONGLE_AUDIO_CODEC_SBC;
    }
    if (info[1] == A2DP_CODEC_AAC && len >= 8U) {
        return DONGLE_AUDIO_CODEC_AAC;
    }
    if (info[1] == A2DP_CODEC_VENDOR && len >= 9U) {
        const uint32_t vid = read_le32(&info[2]);
        const uint16_t cid = read_le16(&info[6]);
        if (vid == VENDOR_ID_LDAC && cid == CODEC_ID_LDAC && len >= 10U) {
            return DONGLE_AUDIO_CODEC_LDAC;
        }
        if (vid == VENDOR_ID_APTX_HD && cid == CODEC_ID_APTX_HD && len >= 9U) {
            return DONGLE_AUDIO_CODEC_APTX_HD;
        }
        if (vid == VENDOR_ID_APTX && cid == CODEC_ID_APTX && len >= 9U) {
            return DONGLE_AUDIO_CODEC_APTX;
        }
    }
    return DONGLE_AUDIO_CODEC_AUTO;
}

static uint32_t pick_sample_rate(dongle_audio_codec_t codec, const uint8_t *caps,
                                 uint32_t usb_rate)
{
    bool has_44 = false, has_48 = false, has_96 = false;
    switch (codec) {
        case DONGLE_AUDIO_CODEC_LDAC:
            has_44 = (caps[8] & 0x20U) != 0U;
            has_48 = (caps[8] & 0x10U) != 0U;
            has_96 = (caps[8] & 0x04U) != 0U;
            break;
        case DONGLE_AUDIO_CODEC_APTX_HD:
        case DONGLE_AUDIO_CODEC_APTX:
            has_44 = (caps[8] & 0x20U) != 0U;
            has_48 = (caps[8] & 0x10U) != 0U;
            break;
        case DONGLE_AUDIO_CODEC_AAC:
            has_44 = (caps[3] & 0x01U) != 0U;
            has_48 = (caps[4] & 0x80U) != 0U;
            break;
        case DONGLE_AUDIO_CODEC_SBC:
            has_44 = (caps[2] & 0x20U) != 0U;
            has_48 = (caps[2] & 0x10U) != 0U;
            break;
        default:
            return 0U;
    }

    if (usb_rate == 96000U && has_96) return 96000U;
    if (usb_rate == 96000U && has_48) return 48000U;
    if (usb_rate == 48000U && has_48) return 48000U;
    if (usb_rate == 44100U && has_44) return 44100U;
    if (has_48) return 48000U;
    if (has_44) return 44100U;
    if (has_96) return 96000U;
    return 0U;
}

static bool build_codec_config(const dongle_a2dp_remote_sep_t *sep,
                               uint32_t usb_rate,
                               uint8_t *out,
                               uint8_t *out_len,
                               uint32_t *out_rate)
{
    const uint8_t *caps = sep->codec_info;
    const dongle_audio_codec_t codec = sep->resolved_codec;
    const uint32_t rate = pick_sample_rate(codec, caps, usb_rate);
    if (rate == 0U) {
        return false;
    }

    switch (codec) {
        case DONGLE_AUDIO_CODEC_LDAC: {
            const uint8_t freq_bit = (rate == 96000U) ? 0x04U :
                                     (rate == 48000U) ? 0x10U : 0x20U;
            const uint8_t ch = (caps[9] & 0x01U) ? 0x01U :
                               (caps[9] & 0x02U) ? 0x02U : 0U;
            if (ch == 0U) return false;
            memcpy(out, caps, 8U);
            out[8] = freq_bit;
            out[9] = ch;
            *out_len = 10U;
            *out_rate = rate;
            return true;
        }
        case DONGLE_AUDIO_CODEC_APTX_HD: {
            if ((caps[8] & 0x02U) == 0U) return false;
            const uint8_t freq_bit = (rate == 48000U) ? 0x10U : 0x20U;
            memcpy(out, caps, 8U);
            out[8] = (uint8_t)(freq_bit | 0x02U);
            out[9] = out[10] = out[11] = out[12] = 0U;
            *out_len = 13U;
            *out_rate = rate;
            return true;
        }
        case DONGLE_AUDIO_CODEC_APTX: {
            if ((caps[8] & 0x02U) == 0U) return false;
            const uint8_t freq_bit = (rate == 48000U) ? 0x10U : 0x20U;
            memcpy(out, caps, 8U);
            out[8] = (uint8_t)(freq_bit | 0x02U);
            *out_len = 9U;
            *out_rate = rate;
            return true;
        }
        case DONGLE_AUDIO_CODEC_AAC: {
            /* Require MPEG-2 AAC LC (0x80) or MPEG-4 AAC LC (0x40) and 2 channels (0x04). */
            uint8_t obj = 0U;
            if (caps[2] & 0x80U) obj = 0x80U;
            else if (caps[2] & 0x40U) obj = 0x40U;
            if (obj == 0U || (caps[4] & 0x04U) == 0U) return false;
            out[0] = 0x00U;
            out[1] = A2DP_CODEC_AAC;
            out[2] = obj;
            out[3] = (rate == 44100U) ? 0x01U : 0x00U;
            out[4] = (uint8_t)(((rate == 48000U) ? 0x80U : 0x00U) | 0x04U);
            /* Cap bitrate at min(remote_max, 256000 bps); preserve VBR bit if remote supports it. */
            uint32_t remote_br = ((uint32_t)(caps[5] & 0x7FU) << 16U) |
                                 ((uint32_t)caps[6] << 8U) | (uint32_t)caps[7];
            uint32_t br = (remote_br == 0U || remote_br > 256000U) ? 256000U : remote_br;
            const uint8_t vbr = (caps[5] & 0x80U) ? 0x80U : 0x00U;
            out[5] = (uint8_t)(vbr | ((br >> 16U) & 0x7FU));
            out[6] = (uint8_t)((br >> 8U) & 0xFFU);
            out[7] = (uint8_t)(br & 0xFFU);
            *out_len = 8U;
            *out_rate = rate;
            return true;
        }
        case DONGLE_AUDIO_CODEC_SBC: {
            const uint8_t freq_bit = (rate == 48000U) ? 0x10U : 0x20U;
            const uint8_t ch_bit = (caps[2] & 0x01U) ? 0x01U :
                                   (caps[2] & 0x02U) ? 0x02U : 0U;
            const uint8_t blk_bit = (caps[3] & 0x10U) ? 0x10U :
                                    (caps[3] & 0x20U) ? 0x20U :
                                    (caps[3] & 0x40U) ? 0x40U :
                                    (caps[3] & 0x80U) ? 0x80U : 0U;
            const uint8_t sub_bit = (caps[3] & 0x04U) ? 0x04U :
                                    (caps[3] & 0x08U) ? 0x08U : 0U;
            const uint8_t alloc_bit = (caps[3] & 0x01U) ? 0x01U :
                                      (caps[3] & 0x02U) ? 0x02U : 0U;
            if (!ch_bit || !blk_bit || !sub_bit || !alloc_bit) return false;
            uint8_t min_bp = caps[4] < 2U ? 2U : caps[4];
            uint8_t max_bp = caps[5] > 53U ? 53U : caps[5];
            if (min_bp > max_bp) min_bp = max_bp;
            out[0] = 0x00U;
            out[1] = A2DP_CODEC_SBC;
            out[2] = (uint8_t)(freq_bit | ch_bit);
            out[3] = (uint8_t)(blk_bit | sub_bit | alloc_bit);
            out[4] = min_bp;
            out[5] = max_bp;
            *out_len = 6U;
            *out_rate = rate;
            return true;
        }
        default:
            return false;
    }
}

static const dongle_a2dp_remote_sep_t *select_best_sep(const dongle_a2dp_source_t *ctx)
{
    static const dongle_audio_codec_t auto_order[] = {
        DONGLE_AUDIO_CODEC_LDAC,
        DONGLE_AUDIO_CODEC_APTX_HD,
        DONGLE_AUDIO_CODEC_APTX,
        DONGLE_AUDIO_CODEC_AAC,
        DONGLE_AUDIO_CODEC_SBC
    };

    /* First honor explicit user codec preference if supported by remote. */
    if (ctx->preferred_codec != DONGLE_AUDIO_CODEC_AUTO) {
        for (uint8_t i = 0U; i < ctx->remote_sep_count; ++i) {
            const dongle_a2dp_remote_sep_t *s = &ctx->remote_seps[i];
            if (s->caps_valid && !s->in_use && s->resolved_codec == ctx->preferred_codec) {
                uint8_t cfg[DONGLE_A2DP_MAX_CODEC_CAP_LEN];
                uint8_t cfg_len = 0U;
                uint32_t rate = 0U;
                if (build_codec_config(s, ctx->usb_sample_rate_hz, cfg, &cfg_len, &rate)) {
                    return s;
                }
            }
        }
    }

    /* Fall back to AUTO priority order. */
    for (size_t p = 0U; p < sizeof(auto_order) / sizeof(auto_order[0]); ++p) {
        for (uint8_t i = 0U; i < ctx->remote_sep_count; ++i) {
            const dongle_a2dp_remote_sep_t *s = &ctx->remote_seps[i];
            if (s->caps_valid && !s->in_use && s->resolved_codec == auto_order[p]) {
                uint8_t cfg[DONGLE_A2DP_MAX_CODEC_CAP_LEN];
                uint8_t cfg_len = 0U;
                uint32_t rate = 0U;
                if (build_codec_config(s, ctx->usb_sample_rate_hz, cfg, &cfg_len, &rate)) {
                    return s;
                }
            }
        }
    }
    return NULL;
}

static int send_set_configuration(dongle_a2dp_source_t *ctx)
{
    const dongle_a2dp_remote_sep_t *sep = select_best_sep(ctx);
    if (sep == NULL) {
        notify_error(ctx, AVDTP_SIG_SET_CONFIGURATION, AVDTP_ERR_BAD_SERV_CATEGORY);
        set_state(ctx, DONGLE_A2DP_STATE_IDLE);
        return -1;
    }

    uint8_t cfg[DONGLE_A2DP_MAX_CODEC_CAP_LEN];
    uint8_t cfg_len = 0U;
    uint32_t rate = 0U;
    if (!build_codec_config(sep, ctx->usb_sample_rate_hz, cfg, &cfg_len, &rate)) {
        notify_error(ctx, AVDTP_SIG_SET_CONFIGURATION, AVDTP_ERR_BAD_SERV_CATEGORY);
        set_state(ctx, DONGLE_A2DP_STATE_IDLE);
        return -1;
    }

    ctx->local_seid = local_seid_for_codec(sep->resolved_codec);
    ctx->remote_seid = sep->seid;
    ctx->active_codec = sep->resolved_codec;
    ctx->active_sample_rate_hz = rate;
    memcpy(ctx->active_config, cfg, cfg_len);
    ctx->active_config_len = cfg_len;

    uint8_t payload[32];
    size_t pos = 0U;
    payload[pos++] = (uint8_t)(ctx->remote_seid << 2U);
    payload[pos++] = (uint8_t)(ctx->local_seid << 2U);
    payload[pos++] = AVDTP_CAT_MEDIA_TRANSPORT;
    payload[pos++] = 0U;
    payload[pos++] = AVDTP_CAT_MEDIA_CODEC;
    payload[pos++] = cfg_len;
    memcpy(&payload[pos], cfg, cfg_len);
    pos += cfg_len;

    set_state(ctx, DONGLE_A2DP_STATE_CONFIGURING);
    return send_cmd(ctx, AVDTP_SIG_SET_CONFIGURATION, payload, pos);
}

static int request_next_caps(dongle_a2dp_source_t *ctx)
{
    while (ctx->caps_index < ctx->remote_sep_count) {
        const dongle_a2dp_remote_sep_t *s = &ctx->remote_seps[ctx->caps_index];
        if (s->media_type == 0x00U && s->tsep == 1U && !s->in_use) {
            const uint8_t acp = (uint8_t)(s->seid << 2U);
            set_state(ctx, DONGLE_A2DP_STATE_GET_CAPS);
            return send_cmd(ctx, AVDTP_SIG_GET_ALL_CAPABILITIES, &acp, 1U);
        }
        ++ctx->caps_index;
    }
    return send_set_configuration(ctx);
}

static void parse_service_caps(dongle_a2dp_remote_sep_t *sep, const uint8_t *caps, size_t len)
{
    size_t pos = 0U;
    while (pos + 2U <= len) {
        const uint8_t cat = caps[pos];
        const uint8_t clen = caps[pos + 1U];
        pos += 2U;
        if (pos + clen > len) {
            break;
        }
        if (cat == AVDTP_CAT_MEDIA_CODEC && clen >= 2U && clen <= DONGLE_A2DP_MAX_CODEC_CAP_LEN) {
            memcpy(sep->codec_info, &caps[pos], clen);
            sep->codec_info_len = clen;
            sep->codec_type = sep->codec_info[1];
            sep->resolved_codec = classify_codec(sep->codec_info, clen);
            sep->caps_valid = (sep->resolved_codec != DONGLE_AUDIO_CODEC_AUTO);
        }
        pos += clen;
    }
}

static void handle_response(dongle_a2dp_source_t *ctx, uint8_t label,
                            uint8_t msg_type, uint8_t sig_id,
                            const uint8_t *payload, size_t len)
{
    if (label != ctx->pending_label || sig_id != ctx->pending_signal) {
        return;
    }

    if (msg_type == AVDTP_MSG_RESP_REJECT || msg_type == AVDTP_MSG_GENERAL_REJECT) {
        const uint8_t err = len >= 1U ? payload[0] : AVDTP_ERR_BAD_STATE;
        if (sig_id == AVDTP_SIG_GET_ALL_CAPABILITIES &&
            (err == AVDTP_ERR_NOT_SUPPORTED_COMMAND || err == AVDTP_ERR_BAD_HEADER_FORMAT)) {
            const uint8_t acp = (uint8_t)(ctx->remote_seps[ctx->caps_index].seid << 2U);
            (void)send_cmd(ctx, AVDTP_SIG_GET_CAPABILITIES, &acp, 1U);
            return;
        }
        notify_error(ctx, sig_id, err);
        if (sig_id == AVDTP_SIG_GET_CAPABILITIES) {
            ++ctx->caps_index;
            (void)request_next_caps(ctx);
            return;
        }
        set_state(ctx, DONGLE_A2DP_STATE_IDLE);
        return;
    }

    if (msg_type != AVDTP_MSG_RESP_ACCEPT) {
        return;
    }

    switch (sig_id) {
        case AVDTP_SIG_DISCOVER: {
            ctx->remote_sep_count = 0U;
            for (size_t i = 0U; i + 2U <= len && ctx->remote_sep_count < DONGLE_A2DP_MAX_REMOTE_SEPS; i += 2U) {
                dongle_a2dp_remote_sep_t *s = &ctx->remote_seps[ctx->remote_sep_count++];
                memset(s, 0, sizeof(*s));
                s->seid = (uint8_t)(payload[i] >> 2U);
                s->in_use = (payload[i] & 0x02U) != 0U;
                s->media_type = (uint8_t)((payload[i + 1U] >> 4U) & 0x0FU);
                s->tsep = (uint8_t)((payload[i + 1U] >> 3U) & 0x01U);
            }
            ctx->caps_index = 0U;
            (void)request_next_caps(ctx);
            break;
        }
        case AVDTP_SIG_GET_ALL_CAPABILITIES:
        case AVDTP_SIG_GET_CAPABILITIES: {
            if (ctx->caps_index < ctx->remote_sep_count) {
                parse_service_caps(&ctx->remote_seps[ctx->caps_index], payload, len);
                ++ctx->caps_index;
            }
            (void)request_next_caps(ctx);
            break;
        }
        case AVDTP_SIG_SET_CONFIGURATION: {
            const uint8_t acp = (uint8_t)(ctx->remote_seid << 2U);
            set_state(ctx, DONGLE_A2DP_STATE_OPENING);
            (void)send_cmd(ctx, AVDTP_SIG_OPEN, &acp, 1U);
            break;
        }
        case AVDTP_SIG_OPEN: {
            set_state(ctx, DONGLE_A2DP_STATE_OPEN_MEDIA);
            if (ctx->cb.open_media != NULL) {
                if (ctx->cb.open_media(ctx->user) != 0) {
                    notify_error(ctx, AVDTP_SIG_OPEN, AVDTP_ERR_BAD_STATE);
                    set_state(ctx, DONGLE_A2DP_STATE_IDLE);
                }
            }
            break;
        }
        case AVDTP_SIG_START: {
            set_state(ctx, DONGLE_A2DP_STATE_STREAMING);
            break;
        }
        case AVDTP_SIG_SUSPEND: {
            set_state(ctx, DONGLE_A2DP_STATE_OPEN);
            break;
        }
        case AVDTP_SIG_CLOSE: {
            ctx->media_connected = false;
            if (ctx->cb.close_media != NULL) {
                ctx->cb.close_media(ctx->user);
            }
            if (ctx->reconfiguring) {
                ctx->reconfiguring = false;
                (void)send_set_configuration(ctx);
            } else {
                ctx->active_codec = DONGLE_AUDIO_CODEC_AUTO;
                ctx->active_sample_rate_hz = 0U;
                set_state(ctx, DONGLE_A2DP_STATE_IDLE);
            }
            break;
        }
        case AVDTP_SIG_ABORT: {
            ctx->media_connected = false;
            if (ctx->cb.close_media != NULL) {
                ctx->cb.close_media(ctx->user);
            }
            ctx->active_codec = DONGLE_AUDIO_CODEC_AUTO;
            ctx->active_sample_rate_hz = 0U;
            set_state(ctx, DONGLE_A2DP_STATE_IDLE);
            break;
        }
        default:
            break;
    }
}

static void handle_command(dongle_a2dp_source_t *ctx, uint8_t label,
                           uint8_t sig_id, const uint8_t *payload, size_t len)
{
    switch (sig_id) {
        case AVDTP_SIG_DISCOVER: {
            uint8_t rsp[10];
            for (uint8_t i = 0U; i < 5U; ++i) {
                const uint8_t seid = (uint8_t)(i + 1U);
                const bool in_use = (ctx->state >= DONGLE_A2DP_STATE_OPEN &&
                                     ctx->local_seid == seid);
                rsp[i * 2U] = (uint8_t)((seid << 2U) | (in_use ? 0x02U : 0x00U));
                rsp[i * 2U + 1U] = 0x00U; /* Audio (0), Source (0) */
            }
            send_rsp(ctx, label, AVDTP_MSG_RESP_ACCEPT, sig_id, rsp, sizeof(rsp));
            break;
        }
        case AVDTP_SIG_GET_CAPABILITIES:
        case AVDTP_SIG_GET_ALL_CAPABILITIES: {
            if (len < 1U) {
                const uint8_t err = AVDTP_ERR_BAD_LENGTH;
                send_rsp(ctx, label, AVDTP_MSG_RESP_REJECT, sig_id, &err, 1U);
                return;
            }
            const uint8_t seid = (uint8_t)(payload[0] >> 2U);
            const dongle_audio_codec_t codec = codec_for_local_seid(seid);
            if (codec == DONGLE_AUDIO_CODEC_AUTO) {
                const uint8_t err = AVDTP_ERR_BAD_ACP_SEID;
                send_rsp(ctx, label, AVDTP_MSG_RESP_REJECT, sig_id, &err, 1U);
                return;
            }
            uint8_t rsp[24];
            size_t pos = 0U;
            rsp[pos++] = AVDTP_CAT_MEDIA_TRANSPORT;
            rsp[pos++] = 0U;
            rsp[pos++] = AVDTP_CAT_MEDIA_CODEC;
            const size_t cap_len = dongle_a2dp_source_local_codec_caps(
                codec, &rsp[pos + 1U], sizeof(rsp) - pos - 1U);
            rsp[pos++] = (uint8_t)cap_len;
            pos += cap_len;
            if (sig_id == AVDTP_SIG_GET_ALL_CAPABILITIES) {
                rsp[pos++] = AVDTP_CAT_DELAY_REPORTING;
                rsp[pos++] = 0U;
            }
            send_rsp(ctx, label, AVDTP_MSG_RESP_ACCEPT, sig_id, rsp, pos);
            break;
        }
        case AVDTP_SIG_SET_CONFIGURATION: {
            if (len < 4U) {
                const uint8_t err[2] = { 0x00U, AVDTP_ERR_BAD_LENGTH };
                send_rsp(ctx, label, AVDTP_MSG_RESP_REJECT, sig_id, err, 2U);
                return;
            }
            const uint8_t acp_seid = (uint8_t)(payload[0] >> 2U);
            const uint8_t int_seid = (uint8_t)(payload[1] >> 2U);
            const dongle_audio_codec_t codec = codec_for_local_seid(acp_seid);
            if (codec == DONGLE_AUDIO_CODEC_AUTO) {
                const uint8_t err[2] = { 0x00U, AVDTP_ERR_BAD_ACP_SEID };
                send_rsp(ctx, label, AVDTP_MSG_RESP_REJECT, sig_id, err, 2U);
                return;
            }
            if (ctx->state >= DONGLE_A2DP_STATE_OPEN && ctx->local_seid != acp_seid) {
                const uint8_t err[2] = { 0x00U, AVDTP_ERR_SEP_IN_USE };
                send_rsp(ctx, label, AVDTP_MSG_RESP_REJECT, sig_id, err, 2U);
                return;
            }
            ctx->local_seid = acp_seid;
            ctx->remote_seid = int_seid;
            ctx->active_codec = codec;
            ctx->active_sample_rate_hz = 44100U;
            dongle_a2dp_remote_sep_t tmp;
            memset(&tmp, 0, sizeof(tmp));
            parse_service_caps(&tmp, &payload[2], len - 2U);
            if (tmp.caps_valid) {
                memcpy(ctx->active_config, tmp.codec_info, tmp.codec_info_len);
                ctx->active_config_len = tmp.codec_info_len;
                const uint32_t rate = pick_sample_rate(codec, tmp.codec_info,
                                                       ctx->usb_sample_rate_hz);
                if (rate != 0U) ctx->active_sample_rate_hz = rate;
            }
            send_rsp(ctx, label, AVDTP_MSG_RESP_ACCEPT, sig_id, NULL, 0U);
            set_state(ctx, DONGLE_A2DP_STATE_OPEN_MEDIA);
            break;
        }
        case AVDTP_SIG_OPEN: {
            if (len < 1U || (payload[0] >> 2U) != ctx->local_seid) {
                const uint8_t err = AVDTP_ERR_BAD_ACP_SEID;
                send_rsp(ctx, label, AVDTP_MSG_RESP_REJECT, sig_id, &err, 1U);
                return;
            }
            send_rsp(ctx, label, AVDTP_MSG_RESP_ACCEPT, sig_id, NULL, 0U);
            set_state(ctx, DONGLE_A2DP_STATE_OPEN_MEDIA);
            break;
        }
        case AVDTP_SIG_START: {
            send_rsp(ctx, label, AVDTP_MSG_RESP_ACCEPT, sig_id, NULL, 0U);
            set_state(ctx, DONGLE_A2DP_STATE_STREAMING);
            break;
        }
        case AVDTP_SIG_SUSPEND: {
            send_rsp(ctx, label, AVDTP_MSG_RESP_ACCEPT, sig_id, NULL, 0U);
            set_state(ctx, DONGLE_A2DP_STATE_OPEN);
            break;
        }
        case AVDTP_SIG_CLOSE: {
            send_rsp(ctx, label, AVDTP_MSG_RESP_ACCEPT, sig_id, NULL, 0U);
            ctx->media_connected = false;
            if (ctx->cb.close_media != NULL) {
                ctx->cb.close_media(ctx->user);
            }
            ctx->active_codec = DONGLE_AUDIO_CODEC_AUTO;
            ctx->active_sample_rate_hz = 0U;
            set_state(ctx, DONGLE_A2DP_STATE_IDLE);
            break;
        }
        case AVDTP_SIG_ABORT: {
            send_rsp(ctx, label, AVDTP_MSG_RESP_ACCEPT, sig_id, NULL, 0U);
            ctx->media_connected = false;
            if (ctx->cb.close_media != NULL) {
                ctx->cb.close_media(ctx->user);
            }
            ctx->active_codec = DONGLE_AUDIO_CODEC_AUTO;
            ctx->active_sample_rate_hz = 0U;
            set_state(ctx, DONGLE_A2DP_STATE_IDLE);
            break;
        }
        case AVDTP_SIG_DELAYREPORT: {
            if (len >= 3U) {
                ctx->delay_ms_tenths = (uint16_t)(((uint16_t)payload[1] << 8U) | payload[2]);
            }
            send_rsp(ctx, label, AVDTP_MSG_RESP_ACCEPT, sig_id, NULL, 0U);
            break;
        }
        default: {
            const uint8_t err = AVDTP_ERR_NOT_SUPPORTED_COMMAND;
            send_rsp(ctx, label, AVDTP_MSG_RESP_REJECT, sig_id, &err, 1U);
            break;
        }
    }
}

void dongle_a2dp_source_init(dongle_a2dp_source_t *ctx,
                              const dongle_a2dp_source_callbacks_t *cb,
                              void *user)
{
    if (ctx == NULL) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    if (cb != NULL) {
        ctx->cb = *cb;
    }
    ctx->user = user;
    ctx->usb_sample_rate_hz = 44100U;
}

void dongle_a2dp_source_reset(dongle_a2dp_source_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    const dongle_a2dp_source_callbacks_t cb = ctx->cb;
    void *user = ctx->user;
    const dongle_audio_codec_t pref = ctx->preferred_codec;
    const uint32_t rate = ctx->usb_sample_rate_hz;
    memset(ctx, 0, sizeof(*ctx));
    ctx->cb = cb;
    ctx->user = user;
    ctx->preferred_codec = pref;
    ctx->usb_sample_rate_hz = rate ? rate : 44100U;
}

void dongle_a2dp_source_set_preferences(dongle_a2dp_source_t *ctx,
                                         dongle_audio_codec_t preferred_codec,
                                         uint32_t usb_sample_rate_hz)
{
    if (ctx == NULL) {
        return;
    }
    if (preferred_codec < DONGLE_AUDIO_CODEC_COUNT) {
        ctx->preferred_codec = preferred_codec;
    }
    if (usb_sample_rate_hz == 44100U || usb_sample_rate_hz == 48000U ||
        usb_sample_rate_hz == 96000U) {
        ctx->usb_sample_rate_hz = usb_sample_rate_hz;
    }
}

int dongle_a2dp_source_connect(dongle_a2dp_source_t *ctx, bool start_after_open)
{
    if (ctx == NULL) {
        return -1;
    }
    ctx->start_after_open = start_after_open;
    ctx->reconfiguring = false;
    ctx->remote_sep_count = 0U;
    set_state(ctx, DONGLE_A2DP_STATE_DISCOVERING);
    return send_cmd(ctx, AVDTP_SIG_DISCOVER, NULL, 0U);
}

int dongle_a2dp_source_media_connected(dongle_a2dp_source_t *ctx)
{
    if (ctx == NULL) {
        return -1;
    }
    ctx->media_connected = true;
    set_state(ctx, DONGLE_A2DP_STATE_OPEN);
    if (ctx->start_after_open) {
        return dongle_a2dp_source_start(ctx);
    }
    return 0;
}

int dongle_a2dp_source_start(dongle_a2dp_source_t *ctx)
{
    if (ctx == NULL || (ctx->state != DONGLE_A2DP_STATE_OPEN &&
                        ctx->state != DONGLE_A2DP_STATE_SUSPENDING)) {
        return -1;
    }
    const uint8_t acp = (uint8_t)(ctx->remote_seid << 2U);
    set_state(ctx, DONGLE_A2DP_STATE_STARTING);
    return send_cmd(ctx, AVDTP_SIG_START, &acp, 1U);
}

int dongle_a2dp_source_suspend(dongle_a2dp_source_t *ctx)
{
    if (ctx == NULL || ctx->state != DONGLE_A2DP_STATE_STREAMING) {
        return -1;
    }
    const uint8_t acp = (uint8_t)(ctx->remote_seid << 2U);
    set_state(ctx, DONGLE_A2DP_STATE_SUSPENDING);
    return send_cmd(ctx, AVDTP_SIG_SUSPEND, &acp, 1U);
}

int dongle_a2dp_source_close(dongle_a2dp_source_t *ctx)
{
    if (ctx == NULL || ctx->state < DONGLE_A2DP_STATE_OPEN) {
        return -1;
    }
    const uint8_t acp = (uint8_t)(ctx->remote_seid << 2U);
    set_state(ctx, DONGLE_A2DP_STATE_CLOSING);
    return send_cmd(ctx, AVDTP_SIG_CLOSE, &acp, 1U);
}

int dongle_a2dp_source_reconfigure(dongle_a2dp_source_t *ctx,
                                   dongle_audio_codec_t preferred_codec,
                                   uint32_t usb_sample_rate_hz)
{
    if (ctx == NULL) {
        return -1;
    }
    dongle_a2dp_source_set_preferences(ctx, preferred_codec, usb_sample_rate_hz);
    if (ctx->remote_sep_count == 0U || ctx->state < DONGLE_A2DP_STATE_OPEN) {
        return 0;
    }
    ctx->start_after_open = (ctx->state == DONGLE_A2DP_STATE_STREAMING || ctx->start_after_open);
    ctx->reconfiguring = true;
    const uint8_t acp = (uint8_t)(ctx->remote_seid << 2U);
    set_state(ctx, DONGLE_A2DP_STATE_CLOSING);
    return send_cmd(ctx, AVDTP_SIG_CLOSE, &acp, 1U);
}

void dongle_a2dp_source_recv_signal(dongle_a2dp_source_t *ctx,
                                    const uint8_t *data,
                                    size_t len)
{
    if (ctx == NULL || data == NULL || len < 2U) {
        return;
    }
    const uint8_t label = (uint8_t)((data[0] >> 4U) & 0x0FU);
    const uint8_t pkt_type = (uint8_t)((data[0] >> 2U) & 0x03U);
    const uint8_t msg_type = (uint8_t)(data[0] & 0x03U);
    const uint8_t sig_id = (uint8_t)(data[1] & 0x3FU);

    if (pkt_type != 0U) {
        return; /* Only single-packet signaling is used. */
    }
    if (msg_type == AVDTP_MSG_COMMAND) {
        handle_command(ctx, label, sig_id, &data[2], len - 2U);
    } else {
        handle_response(ctx, label, msg_type, sig_id, &data[2], len - 2U);
    }
}
