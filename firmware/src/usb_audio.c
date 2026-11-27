#include "dongle/usb_audio.h"
#include "dongle/a2dp_encoder.h"
#include "dongle/a2dp_packetizer.h"
#include "dongle/audio_stream.h"
#include "dongle/bluetooth.h"
#include "dongle/dualsense_audio.h"
#include "dongle/dualsense_haptics.h"
#include "dongle/usb_profiles.h"
#include "opus/opus.h"
#include "FreeRTOS.h"
#include "task.h"
#include "usbd_core.h"
#include "bflb_mtimer.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifdef BL616
#define DONGLE_TCM_CODE __attribute__((section(".tcm_code")))
#else
#define DONGLE_TCM_CODE
#endif

static uint8_t bus;
static volatile bool playing, configured, opened;
static volatile uint32_t usb_sample_rate_hz = 44100U;
static volatile uint8_t bt_codec = DONGLE_AUDIO_CODEC_SBC;
static volatile uint32_t bt_sample_rate_hz = 44100U;
static volatile uint16_t bt_media_mtu = 672U;
static volatile bool encoder_reset_pending;
static dongle_audio_stream_t pcm;
static uint8_t storage[32768];
_Static_assert((sizeof(storage) & (sizeof(storage) - 1U)) == 0U, "pcm storage must be a power of two");
static volatile uint32_t received, sent, underflows, overflows, errors;
static volatile uint16_t peak_left, peak_right;
static volatile uint32_t nonzero_packets;
static dongle_audio_stream_t mic_pcm;
static uint8_t mic_storage[2048];
static volatile bool mic_opened, mic_busy, mic_muted;
static volatile int mic_volume_db;
static volatile uint16_t mic_peak;
static volatile uint32_t mic_sent_bytes, mic_errors;
static TaskHandle_t audio_task_handle;
static volatile bool audio_ota_suspend_req;
static volatile bool audio_ota_suspended;
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t packet[512];
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t ds_haptics_packet[512];
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t mic_packet[64];
static dongle_ds_haptics_t ds_haptics;
static uint8_t ds_stereo_scratch[196];
static volatile bool ds_opened, ds_playback_muted;
static volatile int ds_playback_volume_db;
static int16_t ds_pcm_ring[2][DONGLE_DS_AUDIO_ACCUM_SAMPLES * 4U];
static volatile uint32_t ds_pcm_wr_pos;
static volatile uint8_t ds_pcm_wr_idx;
static volatile bool ds_pcm_ready;
static __attribute__((aligned(8))) uint8_t ds_opus_enc_mem[36864];
static OpusEncoder *ds_opus_enc;
static int ds_opus_force_channels;
static TaskHandle_t ds_audio_task_handle;
static StaticTask_t ds_audio_tcb;
static StackType_t ds_audio_stack[6144];
static volatile bool ds_audio_ota_suspend_req;
static volatile bool ds_audio_ota_suspended;
static struct usbd_interface control, stream, mic_stream, hp_control, hp_stream;
static struct audio_entity_info audio_entities[] = {
    { .bEntityId = 2, .bDescriptorSubtype = AUDIO_CONTROL_FEATURE_UNIT, .ep = 0x02 },
    { .bEntityId = 4, .bDescriptorSubtype = AUDIO_CONTROL_FEATURE_UNIT, .ep = 0x84 },
    { .bEntityId = 5, .bDescriptorSubtype = AUDIO_CONTROL_FEATURE_UNIT, .ep = 0x84 },
};

void dongle_usb_audio_streaming(bool active, uint8_t codec, uint32_t rate_hz, uint16_t media_mtu)
{
    if (active) {
        bt_codec = codec;
        bt_sample_rate_hz = rate_hz ? rate_hz : 44100U;
        bt_media_mtu = media_mtu ? media_mtu : 672U;
        encoder_reset_pending = true;
    }
    playing = active;
    printf("[AUDIO] streaming=%u codec=%u bt_rate=%lu usb_rate=%lu\r\n",
           active ? 1U : 0U, codec, (unsigned long)bt_sample_rate_hz,
           (unsigned long)usb_sample_rate_hz);
}
bool dongle_usb_audio_is_streaming(void) { return playing; }
bool dongle_usb_audio_playback_open(void) { return configured && opened; }
bool dongle_usb_audio_mic_open(void) { return configured && mic_opened; }
size_t dongle_usb_audio_mic_write(const uint8_t *data, size_t size)
{
    if (!data || !size) return 0U;
    size_t written = 0U;
    taskENTER_CRITICAL();
    if (configured && mic_opened) {
        (void)dongle_audio_stream_write(&mic_pcm, data, size, &written);
    }
    taskEXIT_CRITICAL();
    return written;
}
static void put32(uint8_t *p, uint32_t v)
{ for (unsigned i=0; i<4; ++i) p[i] = v >> (8*i); }
void dongle_usb_audio_diagnostics(uint8_t out[20])
{
    put32(out, received); put32(out+4, sent); put32(out+8, underflows);
    put32(out+12, overflows); put32(out+16, errors);
}
void dongle_usb_audio_metrics(uint16_t *out_peak_l, uint16_t *out_peak_r,
                              uint32_t *out_nonzero_packets)
{
    taskENTER_CRITICAL();
    const uint16_t pl = peak_left;
    const uint16_t pr = peak_right;
    const uint32_t nz = nonzero_packets;
    peak_left = 0U;
    peak_right = 0U;
    taskEXIT_CRITICAL();
    if (out_peak_l) *out_peak_l = pl;
    if (out_peak_r) *out_peak_r = pr;
    if (out_nonzero_packets) *out_nonzero_packets = nz;
}
void dongle_usb_audio_mic_metrics(bool *open, bool *muted, uint16_t *peak,
                                  uint32_t *sent_bytes, uint32_t *errors)
{
    taskENTER_CRITICAL();
    const bool is_open = configured && mic_opened;
    const bool is_muted = mic_muted;
    const uint16_t p = mic_peak;
    const uint32_t sb = mic_sent_bytes;
    const uint32_t err = mic_errors;
    mic_peak = 0U;
    taskEXIT_CRITICAL();
    if (open) *open = is_open;
    if (muted) *muted = is_muted;
    if (peak) *peak = p;
    if (sent_bytes) *sent_bytes = sb;
    if (errors) *errors = err;
}
static DONGLE_TCM_CODE void ds_audio_received(uint8_t id, uint8_t ep, uint32_t size)
{
    if (id != bus || ep != 0x02U || !ds_opened) return;
    if (size > 0U && size <= 392U && (size % 8U) == 0U) {
        const int16_t *src = (const int16_t *)ds_haptics_packet;
        const uint32_t total = size / sizeof(int16_t);
        const uint32_t cap = DONGLE_DS_AUDIO_ACCUM_SAMPLES * 4U;
        uint32_t consumed = 0U;
        while (consumed < total) {
            int16_t *dst = ds_pcm_ring[ds_pcm_wr_idx];
            uint32_t pos = ds_pcm_wr_pos;
            const uint32_t space = cap - pos;
            uint32_t chunk = total - consumed;
            if (chunk > space) chunk = space;
            memcpy(&dst[pos], &src[consumed], chunk * sizeof(int16_t));
            pos += chunk;
            consumed += chunk;
            if (pos >= cap) {
                ds_pcm_wr_pos = 0U;
                ds_pcm_wr_idx ^= 1U;
                ds_pcm_ready = true;
                if (ds_audio_task_handle != NULL) {
                    BaseType_t woken = pdFALSE;
                    vTaskNotifyGiveFromISR(ds_audio_task_handle, &woken);
                    portYIELD_FROM_ISR(woken);
                }
            } else {
                ds_pcm_wr_pos = pos;
            }
        }
    } else {
        ++errors;
    }
    if (usbd_ep_start_read(bus, 0x02U, ds_haptics_packet, 392U)) ++errors;
}
static DONGLE_TCM_CODE void audio_received(uint8_t id, uint8_t ep, uint32_t size)
{
    if (id != bus || ep != 3 || !opened) return;
    size_t written = 0;
    const bool ds_quad = dongle_usb_profile() == DONGLE_PROFILE_DUALSENSE &&
                         !dongle_usb_profile_dualsense_split_audio();
    if (ds_quad) {
        if (size > 0U && size <= 392U && (size % 8U) == 0U) {
            size_t stereo_bytes = 0U;
            uint16_t pkt_l = 0U;
            uint16_t pkt_r = 0U;
            uint8_t motor_l = 0U;
            uint8_t motor_r = 0U;
            bool haptics_changed = false;
            if (dongle_ds_haptics_process_4ch(
                    &ds_haptics, packet, size, ds_stereo_scratch, &stereo_bytes,
                    &pkt_l, &pkt_r, &motor_l, &motor_r, &haptics_changed)) {
                if (pkt_l > peak_left) peak_left = pkt_l;
                if (pkt_r > peak_right) peak_right = pkt_r;
                if (pkt_l > 0U || pkt_r > 0U) ++nonzero_packets;
                dongle_audio_stream_write(&pcm, ds_stereo_scratch, stereo_bytes, &written);
                received += written;
                if (written < stereo_bytes) ++overflows;
                if (haptics_changed) {
                    dongle_bluetooth_send_dualsense_audio_haptics(0U, motor_l, motor_r);
                }
            } else {
                ++errors;
            }
        } else {
            ++errors;
        }
    } else if (size <= 388U && !(size % 4U)) {
        uint16_t pkt_peak = 0U;
        for (uint32_t i = 0; i + 3U < size; i += 4U) {
            const int16_t l = (int16_t)((uint16_t)packet[i] | ((uint16_t)packet[i + 1U] << 8U));
            const int16_t r = (int16_t)((uint16_t)packet[i + 2U] | ((uint16_t)packet[i + 3U] << 8U));
            const uint16_t al = l == INT16_MIN ? 32767U : (uint16_t)(l < 0 ? -l : l);
            const uint16_t ar = r == INT16_MIN ? 32767U : (uint16_t)(r < 0 ? -r : r);
            if (al > peak_left) peak_left = al;
            if (ar > peak_right) peak_right = ar;
            if (al > pkt_peak) pkt_peak = al;
            if (ar > pkt_peak) pkt_peak = ar;
        }
        if (pkt_peak > 0U) ++nonzero_packets;
        dongle_audio_stream_write(&pcm, packet, size, &written);
        received += written;
        if (written < size) ++overflows;
    } else ++errors;
    if (usbd_ep_start_read(bus, 3, packet, ds_quad ? 392U : 388U)) ++errors;
}
static DONGLE_TCM_CODE uint16_t db_to_q15_gain(int db)
{
    /* Q15 multipliers for 0 dB down to -59 dB (round(32768 * 10^(dB/20))). */
    static const uint16_t table[60] = {
        32768U, 29205U, 26029U, 23198U, 20675U, 18427U, 16423U, 14637U, 13045U, 11627U,
        10362U,  9235U,  8231U,  7336U,  6538U,  5827U,  5193U,  4629U,  4125U,  3677U,
         3277U,  2920U,  2603U,  2320U,  2068U,  1843U,  1642U,  1464U,  1305U,  1163U,
         1036U,   924U,   823U,   734U,   654U,   583U,   519U,   463U,   413U,   368U,
          328U,   292U,   260U,   232U,   207U,   184U,   164U,   146U,   130U,   116U,
          104U,    92U,    82U,    73U,    65U,    58U,    52U,    46U,    41U,    37U
    };
    if (db >= 0) return 32768U;
    if (db <= -60) return 0U;
    return table[-db];
}
static DONGLE_TCM_CODE void fill_mic_frame(uint8_t out[32])
{
    size_t read_bytes = 0U;
    (void)dongle_audio_stream_read(&mic_pcm, out, 32U, &read_bytes);
    if (read_bytes < 32U) {
        memset(&out[read_bytes], 0, 32U - read_bytes);
    }
    if (mic_muted || mic_volume_db <= -60) {
        memset(out, 0, 32U);
        return;
    }
    const uint16_t gain = db_to_q15_gain(mic_volume_db);
    uint16_t pkt_peak = 0U;
    for (size_t i = 0U; i + 1U < 32U; i += 2U) {
        int16_t s = (int16_t)((uint16_t)out[i] | ((uint16_t)out[i + 1U] << 8U));
        if (gain != 32768U) {
            s = (int16_t)(((int32_t)s * (int32_t)gain) >> 15);
            out[i] = (uint8_t)s;
            out[i + 1U] = (uint8_t)((uint16_t)s >> 8U);
        }
        const uint16_t mag = s == INT16_MIN ? 32767U : (uint16_t)(s < 0 ? -s : s);
        if (mag > pkt_peak) pkt_peak = mag;
    }
    if (pkt_peak > mic_peak) mic_peak = pkt_peak;
}
static DONGLE_TCM_CODE void mic_sent(uint8_t id, uint8_t ep, uint32_t size)
{
    if (id != bus || ep != 0x84) return;
    mic_sent_bytes += size;
    if (configured && mic_opened) {
        fill_mic_frame(mic_packet);
        if (usbd_ep_start_write(bus, 0x84, mic_packet, 32)) {
            mic_busy = false;
            ++mic_errors;
        }
    } else {
        mic_busy = false;
    }
}
static struct usbd_endpoint out_endpoint = {.ep_addr=3, .ep_cb=audio_received};
static struct usbd_endpoint ds_out_endpoint = {.ep_addr=2, .ep_cb=ds_audio_received};
static struct usbd_endpoint mic_endpoint = {.ep_addr=0x84, .ep_cb=mic_sent};
void usbd_audio_open(uint8_t id, uint8_t intf)
{
    if (id != bus) return;
    const bool split = dongle_usb_profile_dualsense_split_audio();
    if (split && intf == 1U) {
        ds_pcm_wr_pos = 0U;
        ds_pcm_wr_idx = 0U;
        ds_pcm_ready = false;
        ds_opened = true;
        if (usbd_ep_start_read(bus, 0x02U, ds_haptics_packet, 392U)) ++errors;
    } else if ((split && intf == 6U) || (!split && intf == 1U)) {
        const bool ds_quad = !split && dongle_usb_profile() == DONGLE_PROFILE_DUALSENSE;
        if (ds_quad) {
            (void)dongle_ds_haptics_reset(&ds_haptics);
            if (usb_sample_rate_hz != 48000U) {
                usb_sample_rate_hz = 48000U;
                dongle_audio_stream_reset(&pcm);
                dongle_bluetooth_set_usb_sample_rate(48000U);
            }
        }
        opened = true;
        if (usbd_ep_start_read(bus, 3, packet, ds_quad ? 392U : 388U)) ++errors;
    } else if (intf == 2) {
        dongle_audio_stream_reset(&mic_pcm);
        mic_opened = true;
        fill_mic_frame(mic_packet);
        mic_busy = true;
        if (usbd_ep_start_write(bus, 0x84, mic_packet, 32)) {
            mic_busy = false;
            ++mic_errors;
        }
    }
}
void usbd_audio_close(uint8_t id, uint8_t intf)
{
    if (id != bus) return;
    const bool split = dongle_usb_profile_dualsense_split_audio();
    if (split && intf == 1U) {
        ds_opened = false;
        ds_pcm_wr_pos = 0U;
        ds_pcm_ready = false;
    } else if ((split && intf == 6U) || (!split && intf == 1U)) {
        opened = false;
        if (!split && dongle_ds_haptics_reset(&ds_haptics)) {
            dongle_bluetooth_send_dualsense_audio_haptics(0U, 0U, 0U);
        }
    } else if (intf == 2) {
        mic_opened = false;
        mic_busy = false;
        dongle_audio_stream_reset(&mic_pcm);
    }
}
void usbd_audio_set_sampling_freq(uint8_t id, uint8_t ep, uint32_t sampling_freq)
{
    (void)id;
    if (ep == 0x02U) {
        return;
    }
    const bool ds_quad = dongle_usb_profile() == DONGLE_PROFILE_DUALSENSE &&
                         !dongle_usb_profile_dualsense_split_audio();
    const bool valid_rate = ds_quad
        ? (sampling_freq == 48000U)
        : (sampling_freq == 44100U || sampling_freq == 48000U || sampling_freq == 96000U);
    if (ep == 3U && valid_rate) {
        if (usb_sample_rate_hz != sampling_freq) {
            usb_sample_rate_hz = sampling_freq;
            dongle_audio_stream_reset(&pcm);
            dongle_bluetooth_set_usb_sample_rate(sampling_freq);
        }
    }
}
uint32_t usbd_audio_get_sampling_freq(uint8_t id, uint8_t ep)
{
    (void)id;
    if (ep == 0x84U) return 16000U;
    if (ep == 0x02U) return 48000U;
    if (dongle_usb_profile() == DONGLE_PROFILE_DUALSENSE &&
        !dongle_usb_profile_dualsense_split_audio()) {
        return 48000U;
    }
    return usb_sample_rate_hz;
}
void usbd_audio_set_mute(uint8_t id, uint8_t ep, uint8_t ch, bool mute)
{
    (void)id; (void)ch;
    if (ep == 0x02U) {
        ds_playback_muted = mute;
        dongle_bluetooth_set_dualsense_audio_mute(0U, mute);
    } else if (ep == 0x84U) {
        mic_muted = mute;
    }
}
bool usbd_audio_get_mute(uint8_t id, uint8_t ep, uint8_t ch)
{
    (void)id; (void)ch;
    if (ep == 0x02U) return ds_playback_muted;
    return ep == 0x84U ? mic_muted : false;
}
void usbd_audio_set_volume(uint8_t id, uint8_t ep, uint8_t ch, int volume_db)
{
    (void)id; (void)ch;
    if (volume_db < -100) volume_db = -100;
    if (volume_db > 0) volume_db = 0;
    if (ep == 0x02U) {
        ds_playback_volume_db = volume_db;
        const uint8_t vol = (uint8_t)(((volume_db + 100) * 127 + 50) / 100);
        dongle_bluetooth_set_dualsense_audio_volume(0U, vol, vol);
    } else if (ep == 0x84U) {
        mic_volume_db = volume_db;
    }
}
int usbd_audio_get_volume(uint8_t id, uint8_t ep, uint8_t ch)
{
    (void)id; (void)ch;
    if (ep == 0x02U) return ds_playback_volume_db;
    return ep == 0x84U ? mic_volume_db : 0;
}
void dongle_usb_audio_event(uint8_t event)
{
    if (event == USBD_EVENT_CONFIGURED || event == USBD_EVENT_RESUME) configured = true;
    else if (event == USBD_EVENT_RESET || event == USBD_EVENT_DISCONNECTED ||
             event == USBD_EVENT_SUSPEND || event == USBD_EVENT_DEINIT) {
        configured = opened = ds_opened = mic_opened = mic_busy = false;
        ds_pcm_wr_pos = 0U;
        ds_pcm_ready = false;
        if (dongle_ds_haptics_reset(&ds_haptics)) {
            dongle_bluetooth_send_dualsense_audio_haptics(0U, 0U, 0U);
        }
    }
}
static void audio_task(void *unused)
{
    (void)unused;
    static int16_t usb_pcm[2048 * 2];
    static int16_t bt_pcm[1024 * 2];
    static uint8_t media_pdu[1024];
    bool enc_open = false;
    bool enc_ctrl_active = false;
    uint64_t epoch = 0, frames = 0, last_congestion_us = 0;
    for (;;) {
        bool should_suspend = false;
        taskENTER_CRITICAL();
        if (audio_ota_suspend_req) {
            audio_ota_suspended = true;
            should_suspend = true;
        } else {
            audio_ota_suspended = false;
        }
        taskEXIT_CRITICAL();
        if (should_suspend) {
            if (enc_open) {
                dongle_a2dp_encoder_close();
                enc_open = false;
            }
            epoch = frames = 0;
            vTaskSuspend(NULL);
            continue;
        }
        if (configured && mic_opened) {
            taskENTER_CRITICAL();
            if (!mic_busy) {
                mic_busy = true;
                fill_mic_frame(mic_packet);
                if (usbd_ep_start_write(bus, 0x84, mic_packet, 32)) {
                    mic_busy = false;
                    ++mic_errors;
                }
            }
            taskEXIT_CRITICAL();
        }
        size_t available = atomic_load_explicit(&pcm.write_cursor, memory_order_acquire) -
                           atomic_load_explicit(&pcm.read_cursor, memory_order_relaxed);
        const bool playing_now = playing;
        if (!playing_now || !opened) {
            if (enc_open) {
                dongle_a2dp_encoder_close();
                enc_open = false;
            }
            epoch = frames = 0;
            if (available) {
                size_t discarded;
                dongle_audio_stream_read(&pcm, (uint8_t *)usb_pcm, sizeof(usb_pcm), &discarded);
            }
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        if (encoder_reset_pending || !enc_open) {
            encoder_reset_pending = false;
            const bool ctrl_active = (dongle_bluetooth_connected() & 0x03U) != 0U;
            if (dongle_a2dp_encoder_open((dongle_audio_codec_t)bt_codec,
                                         bt_sample_rate_hz, bt_media_mtu,
                                         ctrl_active) == 0) {
                enc_open = true;
                enc_ctrl_active = ctrl_active;
            } else {
                enc_open = false;
                ++errors;
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            epoch = frames = 0;
        }
        const bool ctrl_active_now = (dongle_bluetooth_connected() & 0x03U) != 0U;
        if (ctrl_active_now != enc_ctrl_active) {
            dongle_a2dp_encoder_set_controller_active(ctrl_active_now);
            enc_ctrl_active = ctrl_active_now;
        }
        const uint32_t bt_rate = bt_sample_rate_hz;
        const uint32_t usb_rate = usb_sample_rate_hz;
        const size_t bt_frames = dongle_a2dp_encoder_frame_samples();
        size_t usb_frames = (usb_rate == bt_rate)
            ? bt_frames
            : (size_t)(((uint64_t)bt_frames * usb_rate + (bt_rate / 2U)) / bt_rate);
        if (usb_frames == 0U) usb_frames = bt_frames;
        if (usb_frames > 2048U) usb_frames = 2048U;
        const size_t usb_bytes = usb_frames * 4U;

        uint64_t now = bflb_mtimer_get_time_us();
        if (!epoch) {
            const size_t prebuf = (size_t)((usb_rate * 40ULL) / 1000ULL) * 4U;
            if (available < (prebuf > usb_bytes ? prebuf : usb_bytes)) {
                vTaskDelay(1);
                continue;
            }
            epoch = now;
            frames = 0;
        }
        const size_t high_water = (size_t)((usb_rate * 80ULL) / 1000ULL) * 4U;
        if (available < high_water && (now - epoch) * bt_rate < frames * 1000000ULL) {
            vTaskDelay(1);
            continue;
        }
        if (available < usb_bytes) {
            ++underflows;
            epoch = frames = 0;
            vTaskDelay(1);
            continue;
        }
        size_t n = 0U;
        dongle_audio_stream_read(&pcm, (uint8_t *)usb_pcm, usb_bytes, &n);
        if (playing && opened && n == usb_bytes) {
            const int16_t *src_pcm = usb_pcm;
            if (usb_rate != bt_rate) {
                dongle_audio_resample_s16_stereo(usb_pcm, usb_frames, bt_pcm, bt_frames);
                src_pcm = bt_pcm;
            }
            size_t pdu_len = 0U;
            const int enc_rc = dongle_a2dp_encoder_encode(src_pcm, bt_frames, media_pdu,
                                                          sizeof(media_pdu), &pdu_len);
            if (enc_rc == 0) {
                int snd_rc = 0;
                if (pdu_len > 0U) {
                    for (unsigned attempt = 0U; attempt < 15U; ++attempt) {
                        snd_rc = dongle_bluetooth_send_a2dp_media(media_pdu, pdu_len);
                        if (snd_rc >= 0 ||
                            (snd_rc != -ENOMEM && snd_rc != -EAGAIN && snd_rc != -ENOBUFS) ||
                            !playing || !opened) {
                            break;
                        }
                        vTaskDelay(pdMS_TO_TICKS(1));
                    }
                }
                if (pdu_len > 0U && snd_rc < 0) {
                    ++errors;
                    if (now - last_congestion_us >= 500000ULL) {
                        last_congestion_us = now;
                        dongle_a2dp_encoder_on_tx_congestion();
                    }
                } else {
                    sent += (uint32_t)(bt_frames * 4U);
                }
            } else {
                ++errors;
            }
            frames += bt_frames;
            if ((bflb_mtimer_get_time_us() - epoch) * bt_rate >
                frames * 1000000ULL + (uint64_t)bt_rate * 60000ULL) {
                epoch = frames = 0;
            }
        }
    }
}
void dongle_usb_audio_set_rumble_gain(uint16_t gain_pct)
{
    taskENTER_CRITICAL();
    dongle_ds_haptics_set_gain(&ds_haptics, gain_pct);
    taskEXIT_CRITICAL();
}
uint16_t dongle_usb_audio_get_rumble_gain(void)
{
    taskENTER_CRITICAL();
    const uint16_t gain = dongle_ds_haptics_get_gain(&ds_haptics);
    taskEXIT_CRITICAL();
    return gain;
}
void dongle_usb_audio_attach(uint8_t id)
{
    bus = id;
    configured = false;
    opened = false;
    ds_opened = false;
    ds_playback_muted = false;
    ds_playback_volume_db = 0;
    mic_opened = false;
    mic_busy = false;
    const uint8_t n_ent = (uint8_t)(sizeof(audio_entities) / sizeof(audio_entities[0]));
    usbd_add_interface(bus, usbd_audio_init_intf(bus, &control, 0x100, audio_entities, n_ent));
    usbd_add_interface(bus, usbd_audio_init_intf(bus, &stream, 0x100, audio_entities, n_ent));
    usbd_add_interface(bus, usbd_audio_init_intf(bus, &mic_stream, 0x100, audio_entities, n_ent));
    usbd_add_endpoint(bus, &out_endpoint); usbd_add_endpoint(bus, &mic_endpoint);
    if (dongle_usb_profile_dualsense_split_audio()) {
        usbd_add_endpoint(bus, &ds_out_endpoint);
    }
}
void dongle_usb_audio_attach_headphone(uint8_t id)
{
    bus = id;
    if (!dongle_usb_profile_dualsense_split_audio()) {
        return;
    }
    const uint8_t n_ent = (uint8_t)(sizeof(audio_entities) / sizeof(audio_entities[0]));
    usbd_add_interface(bus, usbd_audio_init_intf(bus, &hp_control, 0x100, audio_entities, n_ent));
    usbd_add_interface(bus, usbd_audio_init_intf(bus, &hp_stream, 0x100, audio_entities, n_ent));
}
static DONGLE_TCM_CODE void ds_audio_task(void *unused)
{
    (void)unused;
    static int16_t block[DONGLE_DS_AUDIO_ACCUM_SAMPLES * 4U];
    static int16_t spk_resamp[DONGLE_DS_AUDIO_OPUS_SAMPLES * 2U];
    static int8_t haptic_slots[2][DONGLE_DS_AUDIO_HAPTIC_BYTES];
    static uint8_t opus_slots[2][DONGLE_DS_AUDIO_OPUS_BYTES];
    static uint8_t pkt_39[DONGLE_DS_AUDIO_BT_REPORT_SIZE];
    uint8_t seq = 0U;
    uint8_t pkt_ctr = 0U;

    for (;;) {
        bool should_suspend = false;
        taskENTER_CRITICAL();
        if (ds_audio_ota_suspend_req) {
            ds_audio_ota_suspended = true;
            should_suspend = true;
        } else {
            ds_audio_ota_suspended = false;
        }
        taskEXIT_CRITICAL();
        if (should_suspend) {
            vTaskSuspend(NULL);
            continue;
        }

        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(25U));
        if (!ds_opened || !ds_pcm_ready ||
            (dongle_bluetooth_connected() & 0x01U) == 0U) {
            continue;
        }

        taskENTER_CRITICAL();
        const bool ready = ds_pcm_ready;
        const uint8_t rd_idx = ds_pcm_wr_idx ^ 1U;
        if (ready) {
            memcpy(block, ds_pcm_ring[rd_idx], sizeof(block));
            ds_pcm_ready = false;
        }
        taskEXIT_CRITICAL();
        if (!ready) {
            continue;
        }

        if (ds_opus_enc == NULL) {
            const int enc_size = opus_encoder_get_size(2);
            if (enc_size > 0 && (size_t)enc_size <= sizeof(ds_opus_enc_mem)) {
                OpusEncoder *enc = (OpusEncoder *)ds_opus_enc_mem;
                if (opus_encoder_init(enc, 48000, 2,
                                      OPUS_APPLICATION_RESTRICTED_LOWDELAY) == OPUS_OK) {
                    (void)opus_encoder_ctl(
                        enc, OPUS_SET_EXPERT_FRAME_DURATION(OPUS_FRAMESIZE_10_MS));
                    (void)opus_encoder_ctl(enc, OPUS_SET_BITRATE(200 * 8 * 100));
                    (void)opus_encoder_ctl(enc, OPUS_SET_VBR(0));
                    (void)opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(0));
                    (void)opus_encoder_ctl(enc, OPUS_SET_FORCE_CHANNELS(1));
                    ds_opus_force_channels = 1;
                    ds_opus_enc = enc;
                }
            }
        }

        const bool headset = dongle_bluetooth_dualsense_headset_plugged(0U);
        const int target_channels = headset ? 2 : 1;
        if (ds_opus_enc != NULL && target_channels != ds_opus_force_channels) {
            if (opus_encoder_ctl(ds_opus_enc,
                                 OPUS_SET_FORCE_CHANNELS(target_channels)) == OPUS_OK) {
                ds_opus_force_channels = target_channels;
            }
        }

        const bool muted = ds_playback_muted;
        const uint16_t gain_pct = dongle_usb_audio_get_rumble_gain();
        for (uint32_t slot = 0U; slot < 2U; ++slot) {
            const int16_t *slot_pcm =
                &block[slot * DONGLE_DS_AUDIO_HALF_SAMPLES * 4U];
            if (muted) {
                memset(haptic_slots[slot], 0, sizeof(haptic_slots[slot]));
                memset(opus_slots[slot], 0, sizeof(opus_slots[slot]));
            } else {
                dongle_ds_audio_decimate_haptics(
                    slot_pcm, gain_pct, haptic_slots[slot]);
                if (ds_opus_enc != NULL) {
                    dongle_ds_audio_resample_512_480(slot_pcm, spk_resamp);
                    const int encoded = opus_encode(
                        ds_opus_enc, spk_resamp, DONGLE_DS_AUDIO_OPUS_SAMPLES,
                        opus_slots[slot], DONGLE_DS_AUDIO_OPUS_BYTES);
                    if (encoded <= 0) {
                        memset(opus_slots[slot], 0, DONGLE_DS_AUDIO_OPUS_BYTES);
                    } else if ((size_t)encoded < DONGLE_DS_AUDIO_OPUS_BYTES) {
                        memset(opus_slots[slot] + encoded, 0,
                               DONGLE_DS_AUDIO_OPUS_BYTES - (size_t)encoded);
                    }
                } else {
                    memset(opus_slots[slot], 0, DONGLE_DS_AUDIO_OPUS_BYTES);
                }
            }
        }

        pkt_ctr = (uint8_t)(pkt_ctr + 2U);
        dongle_ds_audio_build_report_39(
            seq, pkt_ctr, dongle_bluetooth_dualsense_mic_active(0U), headset,
            !muted, haptic_slots[0], haptic_slots[1],
            opus_slots[0], opus_slots[1], pkt_39);
        seq = (uint8_t)((seq + 1U) & 0x0FU);
        (void)dongle_bluetooth_send_dualsense_audio_report(0U, pkt_39);
    }
}

int dongle_usb_audio_register(uint8_t id)
{
    const dongle_audio_format_t format = {.sample_rate_hz=44100, .channels=2, .bits_per_sample=16};
    const dongle_audio_format_t mic_format = {.sample_rate_hz=16000, .channels=1, .bits_per_sample=16};
    dongle_ds_haptics_init(&ds_haptics);
    if (dongle_audio_stream_init(&pcm, storage, sizeof(storage), &format) != DONGLE_AUDIO_STREAM_OK) return -1;
    if (dongle_audio_stream_init(&mic_pcm, mic_storage, sizeof(mic_storage), &mic_format) != DONGLE_AUDIO_STREAM_OK) return -1;
    dongle_usb_audio_attach(id);
    if (ds_audio_task_handle == NULL) {
        ds_audio_task_handle = xTaskCreateStatic(
            ds_audio_task, "ds_audio",
            (uint32_t)(sizeof(ds_audio_stack) / sizeof(ds_audio_stack[0])),
            NULL, 6, ds_audio_stack, &ds_audio_tcb);
        if (ds_audio_task_handle == NULL) {
            return -1;
        }
    }
    if (audio_task_handle != NULL) return 0;
    return xTaskCreate(audio_task, "usb_audio", 3072, NULL, 5, &audio_task_handle) == pdPASS ? 0 : -1;
}
void dongle_usb_audio_set_ota_suspended(bool suspended)
{
    if (suspended) {
        taskENTER_CRITICAL();
        audio_ota_suspend_req = true;
        ds_audio_ota_suspend_req = true;
        taskEXIT_CRITICAL();
        return;
    }
    taskENTER_CRITICAL();
    audio_ota_suspend_req = false;
    ds_audio_ota_suspend_req = false;
    const bool was_parking = audio_ota_suspended;
    const bool ds_was_parking = ds_audio_ota_suspended;
    taskEXIT_CRITICAL();
    if (audio_task_handle != NULL) {
        if (eTaskGetState(audio_task_handle) == eSuspended) {
            vTaskResume(audio_task_handle);
        } else if (was_parking) {
            for (unsigned i = 0U; i < 10U && audio_ota_suspended; ++i) {
                if (eTaskGetState(audio_task_handle) == eSuspended) {
                    vTaskResume(audio_task_handle);
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(1U));
            }
        }
    }
    if (ds_audio_task_handle != NULL) {
        if (eTaskGetState(ds_audio_task_handle) == eSuspended) {
            vTaskResume(ds_audio_task_handle);
        } else if (ds_was_parking) {
            for (unsigned i = 0U; i < 10U && ds_audio_ota_suspended; ++i) {
                if (eTaskGetState(ds_audio_task_handle) == eSuspended) {
                    vTaskResume(ds_audio_task_handle);
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(1U));
            }
        }
    }
}
bool dongle_usb_audio_ota_is_suspended(void)
{
    return (audio_task_handle == NULL ||
            eTaskGetState(audio_task_handle) == eSuspended) &&
           (ds_audio_task_handle == NULL ||
            eTaskGetState(ds_audio_task_handle) == eSuspended);
}
