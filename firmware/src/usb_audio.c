#include "dongle/usb_audio.h"
#include "dongle/audio_stream.h"
#include "FreeRTOS.h"
#include "task.h"
#include "usbd_core.h"
#include "bluetooth.h"
#include "conn.h"
#include "l2cap.h"
#include "a2dp.h"
#include "bflb_mtimer.h"
#include <stdio.h>
#include <string.h>

static uint8_t bus;
static volatile bool playing, configured, opened;
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
static volatile uint32_t mic_sent_bytes, mic_errors;
static TaskHandle_t audio_task_handle;
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t packet[256];
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t mic_packet[64];
static struct usbd_interface control, stream, mic_stream;
static struct audio_entity_info audio_entities[] = {
    { .bEntityId = 4, .bDescriptorSubtype = AUDIO_CONTROL_FEATURE_UNIT, .ep = 0x84 },
};
/* This SDK A2DP source encoder uses fixed 44.1 kHz joint stereo PCM. */

void dongle_usb_audio_streaming(bool active) { playing = active;
    printf("[AUDIO] streaming=%u SBC 44100 Hz stereo\r\n", active);
}
bool dongle_usb_audio_is_streaming(void) { return playing; }
bool dongle_usb_audio_playback_open(void) { return configured && opened; }
bool dongle_usb_audio_mic_open(void) { return configured && mic_opened; }
size_t dongle_usb_audio_mic_write(const uint8_t *data, size_t size)
{
    if (!data || !size) return 0U;
    size_t written = 0U;
    (void)dongle_audio_stream_write(&mic_pcm, data, size, &written);
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
static void audio_received(uint8_t id, uint8_t ep, uint32_t size)
{
    if (id != bus || ep != 3 || !opened) return;
    size_t written = 0;
    if (size <= 180 && !(size % 4)) {
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
    if (usbd_ep_start_read(bus, 3, packet, 180)) ++errors;
}
static void fill_mic_frame(uint8_t out[32])
{
    size_t read_bytes = 0U;
    (void)dongle_audio_stream_read(&mic_pcm, out, 32U, &read_bytes);
    if (read_bytes < 32U) {
        memset(&out[read_bytes], 0, 32U - read_bytes);
    }
    if (mic_muted || mic_volume_db <= -100) {
        memset(out, 0, 32U);
    }
}
static void mic_sent(uint8_t id, uint8_t ep, uint32_t size)
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
static struct usbd_endpoint mic_endpoint = {.ep_addr=0x84, .ep_cb=mic_sent};
void usbd_audio_open(uint8_t id, uint8_t intf)
{
    if (id != bus) return;
    if (intf == 1) {
        opened = true;
        if (usbd_ep_start_read(bus, 3, packet, 180)) ++errors;
    } else if (intf == 2) {
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
    if (intf == 1) { opened = false; }
    else if (intf == 2) { mic_opened = false; mic_busy = false; }
}
uint32_t usbd_audio_get_sampling_freq(uint8_t id, uint8_t ep)
{
    (void)id;
    return ep == 0x84 ? 16000U : 44100U;
}
void usbd_audio_set_mute(uint8_t id, uint8_t ep, uint8_t ch, bool mute)
{
    (void)id; (void)ch;
    if (ep == 0x84) { mic_muted = mute; }
}
bool usbd_audio_get_mute(uint8_t id, uint8_t ep, uint8_t ch)
{
    (void)id; (void)ch;
    return ep == 0x84 ? mic_muted : false;
}
void usbd_audio_set_volume(uint8_t id, uint8_t ep, uint8_t ch, int volume_db)
{
    (void)id; (void)ch;
    if (ep == 0x84) {
        if (volume_db < -100) volume_db = -100;
        if (volume_db > 0) volume_db = 0;
        mic_volume_db = volume_db;
    }
}
int usbd_audio_get_volume(uint8_t id, uint8_t ep, uint8_t ch)
{
    (void)id; (void)ch;
    return ep == 0x84 ? mic_volume_db : 0;
}
void dongle_usb_audio_event(uint8_t event)
{
    if (event == USBD_EVENT_CONFIGURED || event == USBD_EVENT_RESUME) configured = true;
    else if (event == USBD_EVENT_RESET || event == USBD_EVENT_DISCONNECTED ||
             event == USBD_EVENT_SUSPEND || event == USBD_EVENT_DEINIT) {
        configured = opened = mic_opened = mic_busy = false;
    }
}
static void audio_task(void *unused)
{
    (void)unused;
    /* The SDK queues borrowed PCM pointers; alternating buffers keep the
     * previous block alive until the next submission consumes its semaphore. */
    static int16_t blocks[2][256];
    unsigned next = 0;
    uint64_t epoch = 0, frames = 0;
    for (;;) {
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
            epoch = frames = 0;
            if (available) {
                size_t discarded;
                dongle_audio_stream_read(&pcm, (uint8_t *)blocks[next], sizeof(blocks[0]), &discarded);
            }
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        uint64_t now = bflb_mtimer_get_time_us();
        if (!epoch) {
            if (available < 7056) { vTaskDelay(1); continue; }
            epoch = now; frames = 0;
        }
        if ((now - epoch) * 44100 < frames * 1000000ULL) { vTaskDelay(1); continue; }
        if (available < sizeof(blocks[0])) {
            ++underflows; epoch = frames = 0; vTaskDelay(1); continue;
        }
        size_t n;
        dongle_audio_stream_read(&pcm, (uint8_t *)blocks[next], sizeof(blocks[0]), &n);
        if (playing && opened && n == sizeof(blocks[0])) {
            if (bt_a2dp_send_media(blocks[next], n)) ++errors;
            else sent += n;
            next ^= 1;
            frames += 128;
            /* If the radio stalls for over 60 ms, rebuffer instead of sending
             * an unbounded burst of stale audio to catch up. */
            if ((bflb_mtimer_get_time_us() - epoch) * 44100 >
                frames * 1000000ULL + 2646000000ULL) epoch = frames = 0;
        }
    }
}
void dongle_usb_audio_attach(uint8_t id)
{
    bus = id;
    configured = false;
    opened = false;
    mic_opened = false;
    mic_busy = false;
    usbd_add_interface(bus, usbd_audio_init_intf(bus, &control, 0x100, audio_entities, 1));
    usbd_add_interface(bus, usbd_audio_init_intf(bus, &stream, 0x100, audio_entities, 1));
    usbd_add_interface(bus, usbd_audio_init_intf(bus, &mic_stream, 0x100, audio_entities, 1));
    usbd_add_endpoint(bus, &out_endpoint); usbd_add_endpoint(bus, &mic_endpoint);
}
int dongle_usb_audio_register(uint8_t id)
{
    const dongle_audio_format_t format = {.sample_rate_hz=44100, .channels=2, .bits_per_sample=16};
    const dongle_audio_format_t mic_format = {.sample_rate_hz=16000, .channels=1, .bits_per_sample=16};
    if (dongle_audio_stream_init(&pcm, storage, sizeof(storage), &format) != DONGLE_AUDIO_STREAM_OK) return -1;
    if (dongle_audio_stream_init(&mic_pcm, mic_storage, sizeof(mic_storage), &mic_format) != DONGLE_AUDIO_STREAM_OK) return -1;
    dongle_usb_audio_attach(id);
    if (audio_task_handle != NULL) return 0;
    return xTaskCreate(audio_task, "usb_audio", 1536, NULL, 5, &audio_task_handle) == pdPASS ? 0 : -1;
}
