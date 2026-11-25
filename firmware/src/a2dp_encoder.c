#include "dongle/a2dp_encoder.h"
#include "dongle/a2dp_packetizer.h"

#include "FreeRTOS.h"
#include "sbc_encoder.h"
#include "ldacBT.h"
#include "voAAC.h"
#include "cmnMemory.h"
#include "openaptx.h"

#include <string.h>

#define A2DP_ENCODER_SSRC 0x444F4E47U /* "DONG" */

typedef struct {
    dongle_audio_codec_t codec;
    uint32_t sample_rate_hz;
    uint16_t media_mtu;
    uint16_t rtp_seq;
    uint32_t rtp_timestamp;

    union {
        struct {
            SBC_ENC_PARAMS params;
            uint8_t frames_buf[640];
            size_t frame_bytes;
            uint8_t target_frames;
        } sbc;
        struct {
            VO_AUDIO_CODECAPI api;
            VO_HANDLE handle;
            VO_MEM_OPERATOR mem_op;
            uint8_t raw_buf[1536];
        } aac;
        struct {
            HANDLE_LDAC_BT handle;
            uint8_t stream_buf[1024];
        } ldac;
        struct {
            struct aptx_context *ctx;
            bool is_hd;
            uint8_t pcm24[128 * 6];
            uint8_t encoded[128 * 2];
        } aptx;
    } u;
} a2dp_encoder_state_t;

static a2dp_encoder_state_t *enc_state;

int dongle_a2dp_encoder_open(dongle_audio_codec_t codec,
                             uint32_t sample_rate_hz,
                             uint16_t media_mtu,
                             bool controller_active)
{
    dongle_a2dp_encoder_close();

    if (media_mtu < 256U) {
        media_mtu = 672U;
    }

    enc_state = (a2dp_encoder_state_t *)pvPortMalloc(sizeof(a2dp_encoder_state_t));
    if (enc_state == NULL) {
        return -1;
    }
    memset(enc_state, 0, sizeof(*enc_state));
    enc_state->codec = codec;
    enc_state->sample_rate_hz = sample_rate_hz;
    enc_state->media_mtu = media_mtu;

    switch (codec) {
        case DONGLE_AUDIO_CODEC_SBC: {
            SBC_ENC_PARAMS *p = &enc_state->u.sbc.params;
            p->s16SamplingFreq = (sample_rate_hz == 48000U) ? SBC_sf48000 : SBC_sf44100;
            p->s16ChannelMode = SBC_JOINT_STEREO;
            p->s16NumOfSubBands = SUB_BANDS_8;
            p->s16NumOfChannels = 2;
            p->s16NumOfBlocks = SBC_BLOCK_3; /* 16 blocks */
            p->s16AllocationMethod = SBC_LOUDNESS;
            p->s16BitPool = controller_active ? 26 : 35;
            p->sbc_mode = SBC_MODE_STD;
            p->u8NumPacketToEncode = 1U;
            SBC_Encoder_Init(p);
            enc_state->u.sbc.target_frames = 4U;
            return 0;
        }
        case DONGLE_AUDIO_CODEC_AAC: {
            voGetAACEncAPI(&enc_state->u.aac.api);
            enc_state->u.aac.mem_op.Alloc = cmnMemAlloc;
            enc_state->u.aac.mem_op.Copy = cmnMemCopy;
            enc_state->u.aac.mem_op.Free = cmnMemFree;
            enc_state->u.aac.mem_op.Set = cmnMemSet;
            enc_state->u.aac.mem_op.Check = cmnMemCheck;
            VO_CODEC_INIT_USERDATA user_data = {
                .memflag = VO_IMF_USERMEMOPERATOR,
                .memData = &enc_state->u.aac.mem_op,
            };
            if (enc_state->u.aac.api.Init(&enc_state->u.aac.handle,
                                          VO_AUDIO_CodingAAC, &user_data) != VO_ERR_NONE) {
                dongle_a2dp_encoder_close();
                return -1;
            }
            AACENC_PARAM params = {
                .sampleRate = (int)sample_rate_hz,
                .bitRate = controller_active ? 160000 : 256000,
                .nChannels = 2,
                .adtsUsed = VOAAC_RAWDATA,
            };
            if (enc_state->u.aac.api.SetParam(enc_state->u.aac.handle,
                                              VO_PID_AAC_ENCPARAM, &params) != VO_ERR_NONE) {
                dongle_a2dp_encoder_close();
                return -1;
            }
            return 0;
        }
        case DONGLE_AUDIO_CODEC_LDAC: {
            enc_state->u.ldac.handle = ldacBT_get_handle();
            if (enc_state->u.ldac.handle == NULL) {
                dongle_a2dp_encoder_close();
                return -1;
            }
            int ldac_mtu = (int)media_mtu;
            if (ldac_mtu < 679) {
                ldac_mtu = 679;
            }
            /* channel_mode 0x01 = LDACBT_CHANNEL_MODE_STEREO */
            if (ldacBT_init_handle_encode(enc_state->u.ldac.handle,
                                          ldac_mtu, LDACBT_EQMID_MQ, 0x01,
                                          LDACBT_SMPL_FMT_S16,
                                          (int)sample_rate_hz) != 0) {
                dongle_a2dp_encoder_close();
                return -1;
            }
            if (controller_active) {
                (void)ldacBT_alter_eqmid_priority(enc_state->u.ldac.handle,
                                                  LDACBT_EQMID_INC_CONNECTION);
            }
            return 0;
        }
        case DONGLE_AUDIO_CODEC_APTX:
        case DONGLE_AUDIO_CODEC_APTX_HD: {
            const bool is_hd = (codec == DONGLE_AUDIO_CODEC_APTX_HD);
            enc_state->u.aptx.is_hd = is_hd;
            enc_state->u.aptx.ctx = aptx_init(is_hd ? 1 : 0);
            if (enc_state->u.aptx.ctx == NULL) {
                dongle_a2dp_encoder_close();
                return -1;
            }
            return 0;
        }
        default:
            dongle_a2dp_encoder_close();
            return -1;
    }
}

void dongle_a2dp_encoder_close(void)
{
    if (enc_state == NULL) {
        return;
    }
    switch (enc_state->codec) {
        case DONGLE_AUDIO_CODEC_AAC:
            if (enc_state->u.aac.handle != NULL) {
                enc_state->u.aac.api.Uninit(enc_state->u.aac.handle);
            }
            break;
        case DONGLE_AUDIO_CODEC_LDAC:
            if (enc_state->u.ldac.handle != NULL) {
                ldacBT_close_handle(enc_state->u.ldac.handle);
                ldacBT_free_handle(enc_state->u.ldac.handle);
            }
            break;
        case DONGLE_AUDIO_CODEC_APTX:
        case DONGLE_AUDIO_CODEC_APTX_HD:
            if (enc_state->u.aptx.ctx != NULL) {
                aptx_finish(enc_state->u.aptx.ctx);
            }
            break;
        default:
            break;
    }
    vPortFree(enc_state);
    enc_state = NULL;
}

void dongle_a2dp_encoder_set_controller_active(bool controller_active)
{
    if (enc_state == NULL) {
        return;
    }
    switch (enc_state->codec) {
        case DONGLE_AUDIO_CODEC_LDAC:
            if (enc_state->u.ldac.handle != NULL) {
                (void)ldacBT_set_eqmid(enc_state->u.ldac.handle, LDACBT_EQMID_MQ);
                if (controller_active) {
                    (void)ldacBT_alter_eqmid_priority(enc_state->u.ldac.handle,
                                                      LDACBT_EQMID_INC_CONNECTION);
                }
            }
            break;
        case DONGLE_AUDIO_CODEC_SBC:
            enc_state->u.sbc.params.s16BitPool = controller_active ? 26 : 35;
            SBC_Encoder_Init(&enc_state->u.sbc.params);
            break;
        case DONGLE_AUDIO_CODEC_AAC:
            if (enc_state->u.aac.handle != NULL) {
                AACENC_PARAM params = {
                    .sampleRate = (int)enc_state->sample_rate_hz,
                    .bitRate = controller_active ? 160000 : 256000,
                    .nChannels = 2,
                    .adtsUsed = VOAAC_RAWDATA,
                };
                (void)enc_state->u.aac.api.SetParam(
                    enc_state->u.aac.handle, VO_PID_AAC_ENCPARAM, &params);
            }
            break;
        default:
            break;
    }
}

void dongle_a2dp_encoder_on_tx_congestion(void)
{
    if (enc_state == NULL) {
        return;
    }
    if (enc_state->codec == DONGLE_AUDIO_CODEC_LDAC &&
        enc_state->u.ldac.handle != NULL) {
        (void)ldacBT_alter_eqmid_priority(enc_state->u.ldac.handle,
                                          LDACBT_EQMID_INC_CONNECTION);
    }
}

size_t dongle_a2dp_encoder_frame_samples(void)
{
    if (enc_state == NULL) {
        return 128U;
    }
    switch (enc_state->codec) {
        case DONGLE_AUDIO_CODEC_SBC:
            return (size_t)enc_state->u.sbc.target_frames * 128U;
        case DONGLE_AUDIO_CODEC_AAC:
            return 1024U;
        case DONGLE_AUDIO_CODEC_LDAC:
        case DONGLE_AUDIO_CODEC_APTX:
        case DONGLE_AUDIO_CODEC_APTX_HD:
            return 128U;
        default:
            return 128U;
    }
}

int dongle_a2dp_encoder_encode(const int16_t *pcm_s16,
                               size_t num_frames,
                               uint8_t *out_pdu,
                               size_t out_capacity,
                               size_t *out_len)
{
    if (enc_state == NULL || pcm_s16 == NULL || out_pdu == NULL || out_len == NULL) {
        return -1;
    }
    *out_len = 0U;

    switch (enc_state->codec) {
        case DONGLE_AUDIO_CODEC_SBC: {
            const uint8_t nframes = (uint8_t)(num_frames / 128U);
            if (nframes == 0U || num_frames % 128U != 0U) {
                return -1;
            }
            size_t total_sbc = 0U;
            for (uint8_t i = 0U; i < nframes; ++i) {
                SBC_ENC_PARAMS *p = &enc_state->u.sbc.params;
                p->ps16PcmBuffer = (SINT16 *)&pcm_s16[(size_t)i * 256U];
                p->pu8Packet = &enc_state->u.sbc.frames_buf[total_sbc];
                SBC_Encoder(p);
                total_sbc += p->u16PacketLength;
                if (total_sbc + p->u16PacketLength > sizeof(enc_state->u.sbc.frames_buf)) {
                    break;
                }
            }
            const size_t n = dongle_a2dp_packetize_sbc(
                out_pdu, out_capacity, enc_state->rtp_seq++, enc_state->rtp_timestamp,
                A2DP_ENCODER_SSRC, nframes, enc_state->u.sbc.frames_buf, total_sbc);
            enc_state->rtp_timestamp += (uint32_t)num_frames;
            if (n == 0U) return -1;
            *out_len = n;
            return 0;
        }
        case DONGLE_AUDIO_CODEC_AAC: {
            if (num_frames != 1024U) {
                return -1;
            }
            VO_CODECBUFFER in_buf = {
                .Buffer = (VO_PBYTE)pcm_s16,
                .Length = (VO_U32)(num_frames * 2U * sizeof(int16_t)),
            };
            VO_CODECBUFFER out_buf = {
                .Buffer = enc_state->u.aac.raw_buf,
                .Length = sizeof(enc_state->u.aac.raw_buf),
            };
            VO_AUDIO_OUTPUTINFO out_info;
            memset(&out_info, 0, sizeof(out_info));
            if (enc_state->u.aac.api.SetInputData(enc_state->u.aac.handle, &in_buf) != VO_ERR_NONE ||
                enc_state->u.aac.api.GetOutputData(enc_state->u.aac.handle, &out_buf, &out_info) != VO_ERR_NONE) {
                return -1;
            }
            const size_t n = dongle_a2dp_packetize_aac_latm(
                out_pdu, out_capacity, enc_state->rtp_seq++, enc_state->rtp_timestamp,
                A2DP_ENCODER_SSRC, enc_state->sample_rate_hz,
                enc_state->u.aac.raw_buf, out_buf.Length);
            enc_state->rtp_timestamp += (uint32_t)num_frames;
            if (n == 0U) return -1;
            *out_len = n;
            return 0;
        }
        case DONGLE_AUDIO_CODEC_LDAC: {
            if (num_frames != 128U) {
                return -1;
            }
            int pcm_used = 0;
            int stream_sz = 0;
            int frame_num = 0;
            if (ldacBT_encode(enc_state->u.ldac.handle, (void *)pcm_s16, &pcm_used,
                              enc_state->u.ldac.stream_buf, &stream_sz, &frame_num) != 0) {
                return -1;
            }
            if (stream_sz > 0 && frame_num > 0) {
                const size_t n = dongle_a2dp_packetize_ldac(
                    out_pdu, out_capacity, enc_state->rtp_seq++, enc_state->rtp_timestamp,
                    A2DP_ENCODER_SSRC, (uint8_t)frame_num,
                    enc_state->u.ldac.stream_buf, (size_t)stream_sz);
                enc_state->rtp_timestamp += (uint32_t)frame_num *
                    ((enc_state->sample_rate_hz >= 88200U) ? 256U : 128U);
                if (n == 0U) return -1;
                *out_len = n;
            }
            return 0;
        }
        case DONGLE_AUDIO_CODEC_APTX:
        case DONGLE_AUDIO_CODEC_APTX_HD: {
            if (num_frames != 128U) {
                return -1;
            }
            uint8_t *dst = enc_state->u.aptx.pcm24;
            for (size_t i = 0U; i < 256U; ++i) {
                const uint16_t u = (uint16_t)pcm_s16[i];
                dst[i * 3U] = 0U;
                dst[i * 3U + 1U] = (uint8_t)(u & 0xFFU);
                dst[i * 3U + 2U] = (uint8_t)(u >> 8U);
            }
            size_t written = 0U;
            (void)aptx_encode(enc_state->u.aptx.ctx,
                              enc_state->u.aptx.pcm24, sizeof(enc_state->u.aptx.pcm24),
                              enc_state->u.aptx.encoded, sizeof(enc_state->u.aptx.encoded),
                              &written);
            if (written > 0U) {
                size_t n = 0U;
                if (enc_state->u.aptx.is_hd) {
                    n = dongle_a2dp_packetize_aptx_hd(
                        out_pdu, out_capacity, enc_state->rtp_seq++, enc_state->rtp_timestamp,
                        A2DP_ENCODER_SSRC, enc_state->u.aptx.encoded, written);
                    enc_state->rtp_timestamp += (uint32_t)num_frames;
                } else {
                    n = dongle_a2dp_packetize_aptx(
                        out_pdu, out_capacity, enc_state->u.aptx.encoded, written);
                }
                if (n == 0U) return -1;
                *out_len = n;
            }
            return 0;
        }
        default:
            return -1;
    }
}
