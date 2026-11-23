#include "dongle/mic_pipeline.h"
#include "dongle/dualsense_mic.h"
#include "dongle/usb_audio.h"

#include "FreeRTOS.h"
#include "task.h"
#include "bflb_l1c.h"
#include "btble_lib_api.h"
#include "oi_codec_sbc.h"
#include "opus/opus.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define MIC_DEC_TASK_STACK_WORDS 4096U
#define MIC_DEC_TASK_PRIORITY (tskIDLE_PRIORITY + 1U)
#define MIC_CLOSE_DEBOUNCE_MS 300U

#define SCO_RING_CAPACITY 16U
#define SCO_SLOT_MAX_BYTES 64U
#define OPUS_RING_CAPACITY 8U
#define OPUS_SLOT_MAX_BYTES 128U

#define MIC_CODEC_NONE 0U
#define MIC_CODEC_CVSD 1U
#define MIC_CODEC_MSBC 2U
#define MIC_CODEC_OPUS 3U

#define BT_E_SCOTRCNTL0_ADDR 0x28000610U

typedef struct {
    uint8_t len;
    uint8_t is_msbc;
    uint8_t data[SCO_SLOT_MAX_BYTES];
} sco_slot_t;

typedef struct {
    uint8_t slot;
    uint8_t len;
    uint8_t data[OPUS_SLOT_MAX_BYTES];
} opus_slot_t;

typedef struct {
    uint32_t seq;
    uint16_t interval_halfslot;
    uint32_t tx0;
    uint32_t tx1;
    uint32_t rx0;
    uint32_t rx1;
    uint32_t tx_sz;
    uint32_t rx_sz;
    uint8_t index;
    uint8_t is_msbc;
    uint8_t rx_preview[16];
} sco_debug_snapshot_t;

static TaskHandle_t mic_task_handle;
static atomic_uint_fast8_t configured_source = DONGLE_MIC_SOURCE_AUTO;
static atomic_bool hfp_slc_ready_state;
static atomic_bool hfp_sco_open_state;
static atomic_bool hfp_codec_msbc_state;
static atomic_uint_fast8_t ds_connected_mask_state;
static atomic_bool usb_mic_debounced_open;

static atomic_bool sco_cb_seen;
static atomic_bool sco_debug_pending;
static atomic_uint_fast8_t sco_debug_remaining = 4U;
static atomic_uint_fast32_t sco_cb_count;
static sco_debug_snapshot_t sco_debug_snap;

static sco_slot_t sco_ring[SCO_RING_CAPACITY];
static atomic_uint_fast8_t sco_ring_head;
static atomic_uint_fast8_t sco_ring_tail;

static opus_slot_t opus_ring[OPUS_RING_CAPACITY];
static atomic_uint_fast8_t opus_ring_head;
static atomic_uint_fast8_t opus_ring_tail;

static atomic_uint_fast32_t decoded_frames;
static atomic_uint_fast32_t error_frames;

static OI_CODEC_SBC_DECODER_CONTEXT msbc_ctx;
static OI_CODEC_SBC_CODEC_DATA_MONO msbc_data;
static bool msbc_initialized;
static OpusDecoder *opus_dec;
static int16_t cvsd_prev_sample;
static uint8_t msbc_err_logs;
static uint8_t opus_ok_logs;
static uint8_t opus_err_logs;
static uint16_t local_decoded_peak;

static atomic_bool sco_hw_active;
static uint8_t * volatile sco_hw_tx_buf[2];
static const uint8_t * volatile sco_hw_rx_buf[2];
static volatile uint32_t sco_hw_tx_len;
static volatile uint32_t sco_hw_rx_len;
static volatile uint8_t sco_hw_last_idx;
static volatile uint8_t sco_tx_h2_seq;
static uint8_t sco_rx_nonzero_logs;

static void fill_sco_tx_silence(uint8_t *tx_ptr, uint32_t tx_len, bool is_msbc)
{
    static const uint8_t h2_seq[4] = { 0x08U, 0x38U, 0xC8U, 0xF8U };

    if (tx_ptr == NULL || tx_len == 0U) {
        return;
    }
    memset(tx_ptr, 0, tx_len);
    if (is_msbc && tx_len >= 6U) {
        /* H2 header + silent mSBC header so HF remote receives valid framing. */
        const uint8_t seq = sco_tx_h2_seq;
        sco_tx_h2_seq = (uint8_t)(seq + 1U);
        tx_ptr[0] = 0x01U;
        tx_ptr[1] = h2_seq[seq & 3U];
        tx_ptr[2] = OI_mSBC_SYNCWORD;
        tx_ptr[3] = 0x00U;
        tx_ptr[4] = 0x00U;
        tx_ptr[5] = 0x48U;
    }
    if ((uintptr_t)tx_ptr >= 0x62FC0000U) {
        bflb_l1c_dcache_clean_range((void *)tx_ptr, tx_len);
    }
}

static void write_mic_pcm(const int16_t *pcm, size_t samples)
{
    if (pcm == NULL || samples == 0U) {
        return;
    }
    for (size_t i = 0U; i < samples; ++i) {
        const int16_t s = pcm[i];
        const uint16_t mag = s == INT16_MIN ? 32767U : (uint16_t)(s < 0 ? -s : s);
        if (mag > local_decoded_peak) {
            local_decoded_peak = mag;
        }
    }
    (void)dongle_usb_audio_mic_write((const uint8_t *)pcm, samples * sizeof(int16_t));
}

static uint8_t *sco_map_addr(uint32_t addr)
{
    if (addr >= 0x28000000U) {
        return (uint8_t *)(uintptr_t)addr;
    }
    if (addr < 0x20000U) {
        return (uint8_t *)(uintptr_t)(0x28008000U + addr);
    }
    return NULL;
}

static void sco_codec_isr(uint16_t interval_halfslot,
                          uint32_t tx_buffer_0,
                          uint32_t tx_buffer_1,
                          uint32_t rx_buffer_0,
                          uint32_t rx_buffer_1,
                          uint32_t tx_buffer_size,
                          uint32_t rx_buffer_size,
                          uint32_t start_time_halfslot,
                          uint8_t buffer_index)
{
    (void)start_time_halfslot;

    const uint32_t rx_addr = (buffer_index & 1U) != 0U ? rx_buffer_1 : rx_buffer_0;
    const uint8_t *rx_ptr = (buffer_index < 2U) ? sco_map_addr(rx_addr) : NULL;
    const uint32_t rx_len = rx_buffer_size <= SCO_SLOT_MAX_BYTES
        ? rx_buffer_size : SCO_SLOT_MAX_BYTES;
    const uint32_t tx_len = tx_buffer_size <= SCO_SLOT_MAX_BYTES
        ? tx_buffer_size : SCO_SLOT_MAX_BYTES;
    const bool is_msbc = atomic_load_explicit(&hfp_codec_msbc_state, memory_order_relaxed);

    if (rx_ptr != NULL && rx_len > 0U && (uintptr_t)rx_ptr >= 0x62FC0000U) {
        bflb_l1c_dcache_invalidate_range((void *)rx_ptr, rx_len);
    }

    const uint32_t cb_seq = (uint32_t)atomic_fetch_add_explicit(
        &sco_cb_count, 1U, memory_order_relaxed) + 1U;
    atomic_store_explicit(&sco_cb_seen, true, memory_order_release);

    const uint8_t rem = (uint8_t)atomic_load_explicit(&sco_debug_remaining, memory_order_relaxed);
    if (rem > 0U && !atomic_load_explicit(&sco_debug_pending, memory_order_acquire)) {
        atomic_store_explicit(&sco_debug_remaining, (uint8_t)(rem - 1U), memory_order_relaxed);
        sco_debug_snap.seq = cb_seq;
        sco_debug_snap.interval_halfslot = interval_halfslot;
        sco_debug_snap.tx0 = tx_buffer_0;
        sco_debug_snap.tx1 = tx_buffer_1;
        sco_debug_snap.rx0 = rx_buffer_0;
        sco_debug_snap.rx1 = rx_buffer_1;
        sco_debug_snap.tx_sz = tx_buffer_size;
        sco_debug_snap.rx_sz = rx_buffer_size;
        sco_debug_snap.index = buffer_index;
        sco_debug_snap.is_msbc = is_msbc ? 1U : 0U;
        memset(sco_debug_snap.rx_preview, 0, sizeof(sco_debug_snap.rx_preview));
        if (rx_ptr != NULL && rx_len > 0U) {
            const size_t n = rx_len < sizeof(sco_debug_snap.rx_preview)
                ? rx_len : sizeof(sco_debug_snap.rx_preview);
            memcpy(sco_debug_snap.rx_preview, rx_ptr, n);
        }
        atomic_store_explicit(&sco_debug_pending, true, memory_order_release);
    }

    if (interval_halfslot == 0U || buffer_index >= 2U || rx_buffer_size == 0U) {
        atomic_store_explicit(&sco_hw_active, false, memory_order_release);
    } else {
        sco_hw_tx_buf[0] = sco_map_addr(tx_buffer_0);
        sco_hw_tx_buf[1] = sco_map_addr(tx_buffer_1);
        sco_hw_rx_buf[0] = sco_map_addr(rx_buffer_0);
        sco_hw_rx_buf[1] = sco_map_addr(rx_buffer_1);
        sco_hw_tx_len = tx_len;
        sco_hw_rx_len = rx_len;
        sco_hw_last_idx = buffer_index;
        sco_tx_h2_seq = 0U;
        fill_sco_tx_silence(sco_hw_tx_buf[0], tx_len, is_msbc);
        fill_sco_tx_silence(sco_hw_tx_buf[1], tx_len, is_msbc);
        atomic_store_explicit(&sco_hw_active, true, memory_order_release);
    }

    if (mic_task_handle != NULL) {
        BaseType_t woken = pdFALSE;
        vTaskNotifyGiveFromISR(mic_task_handle, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

static uint8_t resolve_active_source(void)
{
    const uint8_t cfg = (uint8_t)atomic_load_explicit(&configured_source, memory_order_relaxed);
    const bool hfp_ready = atomic_load_explicit(&hfp_slc_ready_state, memory_order_relaxed);
    const uint8_t ds_mask = (uint8_t)atomic_load_explicit(&ds_connected_mask_state, memory_order_relaxed);

    switch (cfg) {
        case DONGLE_MIC_SOURCE_AUTO:
            if (hfp_ready) {
                return DONGLE_MIC_SOURCE_HEADPHONE;
            }
            if ((ds_mask & 0x01U) != 0U) {
                return DONGLE_MIC_SOURCE_CONTROLLER_0;
            }
            if ((ds_mask & 0x02U) != 0U) {
                return DONGLE_MIC_SOURCE_CONTROLLER_1;
            }
            return DONGLE_MIC_SOURCE_OFF;
        case DONGLE_MIC_SOURCE_HEADPHONE:
            return hfp_ready ? DONGLE_MIC_SOURCE_HEADPHONE : DONGLE_MIC_SOURCE_OFF;
        case DONGLE_MIC_SOURCE_CONTROLLER_0:
            return (ds_mask & 0x01U) != 0U ? DONGLE_MIC_SOURCE_CONTROLLER_0 : DONGLE_MIC_SOURCE_OFF;
        case DONGLE_MIC_SOURCE_CONTROLLER_1:
            return (ds_mask & 0x02U) != 0U ? DONGLE_MIC_SOURCE_CONTROLLER_1 : DONGLE_MIC_SOURCE_OFF;
        default:
            return DONGLE_MIC_SOURCE_OFF;
    }
}

static void reset_msbc_decoder(void)
{
    (void)OI_CODEC_SBC_DecoderReset(
        &msbc_ctx, msbc_data.data, sizeof(msbc_data.data),
        1U, 1U, FALSE, TRUE);
    msbc_initialized = true;
}

static void feed_sco_msbc_chunk(uint8_t *reasm, size_t *reasm_len,
                                const uint8_t *chunk, size_t len)
{
    if (len >= 2U && chunk[0] == 0x01U &&
        (chunk[1] == 0x08U || chunk[1] == 0x38U ||
         chunk[1] == 0xC8U || chunk[1] == 0xF8U)) {
        chunk += 2U;
        len -= 2U;
    }
    if (len == 0U) {
        return;
    }
    if (*reasm_len + len > 128U) {
        /* Drop stale partial bytes to preserve live real-time alignment. */
        *reasm_len = 0U;
        atomic_fetch_add_explicit(&error_frames, 1U, memory_order_relaxed);
    }
    if (len > 128U) {
        len = 128U;
    }
    memcpy(&reasm[*reasm_len], chunk, len);
    *reasm_len += len;

    if (!msbc_initialized) {
        reset_msbc_decoder();
    }

    const OI_BYTE *ptr = reasm;
    OI_UINT32 rem = (OI_UINT32)*reasm_len;
    while (rem >= SBC_HEADER_LEN) {
        int16_t pcm[128];
        OI_UINT32 pcm_bytes = sizeof(pcm);
        const OI_BYTE *before = ptr;
        const OI_UINT32 before_rem = rem;
        const OI_STATUS st = OI_CODEC_SBC_DecodeFrame(
            &msbc_ctx, &ptr, &rem, pcm, &pcm_bytes);
        if (st == OI_OK) {
            if (pcm_bytes >= sizeof(int16_t)) {
                write_mic_pcm(pcm, pcm_bytes / sizeof(int16_t));
                atomic_fetch_add_explicit(&decoded_frames, 1U, memory_order_relaxed);
            }
            continue;
        }
        if (st == OI_CODEC_SBC_NOT_ENOUGH_HEADER_DATA ||
            st == OI_CODEC_SBC_NOT_ENOUGH_BODY_DATA) {
            break;
        }
        if (st == OI_CODEC_SBC_NO_SYNCWORD) {
            const bool all_zero =
                (before_rem == 0U || before[0] == 0U) &&
                (before_rem <= 1U || before[1] == 0U) &&
                (before_rem <= 2U || before[2] == 0U) &&
                (before_rem <= 3U || before[3] == 0U);
            if (!all_zero && msbc_err_logs < 4U) {
                ++msbc_err_logs;
                printf("[MIC] msbc err st=%d rem=%lu b0..3=%02x %02x %02x %02x\r\n",
                       (int)st, (unsigned long)before_rem,
                       before_rem > 0U ? before[0] : 0U,
                       before_rem > 1U ? before[1] : 0U,
                       before_rem > 2U ? before[2] : 0U,
                       before_rem > 3U ? before[3] : 0U);
            }
            rem = 0U;
            break;
        }
        /* False syncword or CRC mismatch: step past 1 byte and continue scanning. */
        if (msbc_err_logs < 4U) {
            ++msbc_err_logs;
            printf("[MIC] msbc err st=%d rem=%lu b0..3=%02x %02x %02x %02x\r\n",
                   (int)st, (unsigned long)before_rem,
                   before_rem > 0U ? before[0] : 0U,
                   before_rem > 1U ? before[1] : 0U,
                   before_rem > 2U ? before[2] : 0U,
                   before_rem > 3U ? before[3] : 0U);
        }
        atomic_fetch_add_explicit(&error_frames, 1U, memory_order_relaxed);
        if (ptr == before && rem == before_rem && rem > 0U) {
            ++ptr;
            --rem;
        }
    }

    if (rem > 0U && ptr != reasm) {
        memmove(reasm, ptr, rem);
    }
    *reasm_len = (size_t)rem;
}

static void drain_sco_ring(uint8_t *reasm, size_t *reasm_len, uint8_t active_src, bool usb_open)
{
    for (;;) {
        const uint8_t tail = (uint8_t)atomic_load_explicit(&sco_ring_tail, memory_order_relaxed);
        const uint8_t head = (uint8_t)atomic_load_explicit(&sco_ring_head, memory_order_acquire);
        if (tail == head) {
            break;
        }
        sco_slot_t item;
        memcpy(&item, &sco_ring[tail], sizeof(item));
        atomic_store_explicit(
            &sco_ring_tail, (uint8_t)((tail + 1U) % SCO_RING_CAPACITY), memory_order_release);

        if (!usb_open || active_src != DONGLE_MIC_SOURCE_HEADPHONE) {
            *reasm_len = 0U;
            continue;
        }
        if (item.is_msbc != 0U) {
            feed_sco_msbc_chunk(reasm, reasm_len, item.data, item.len);
        } else {
            const size_t samples_in = item.len / 2U;
            int16_t in_pcm[SCO_SLOT_MAX_BYTES / 2U];
            int16_t out_pcm[SCO_SLOT_MAX_BYTES];
            for (size_t i = 0U; i < samples_in; ++i) {
                in_pcm[i] = (int16_t)((uint16_t)item.data[i * 2U] |
                                      ((uint16_t)item.data[i * 2U + 1U] << 8U));
            }
            const size_t out_samples = dongle_pcm_upsample_8k_to_16k(
                in_pcm, samples_in, out_pcm, SCO_SLOT_MAX_BYTES, &cvsd_prev_sample);
            if (out_samples > 0U) {
                write_mic_pcm(out_pcm, out_samples);
                atomic_fetch_add_explicit(&decoded_frames, 1U, memory_order_relaxed);
            }
        }
    }
}

static void drain_opus_ring(uint8_t active_src, bool usb_open)
{
    static int16_t opus_pcm_48k[480];

    const uint8_t head = (uint8_t)atomic_load_explicit(&opus_ring_head, memory_order_acquire);
    uint8_t tail = (uint8_t)atomic_load_explicit(&opus_ring_tail, memory_order_relaxed);
    if (!usb_open || !dongle_usb_audio_mic_open() || opus_dec == NULL) {
        atomic_store_explicit(&opus_ring_tail, head, memory_order_release);
        return;
    }
    if (tail == head) {
        return;
    }
    const uint8_t queued = (uint8_t)((head + OPUS_RING_CAPACITY - tail) % OPUS_RING_CAPACITY);
    if (queued > 2U) {
        tail = (uint8_t)((head + OPUS_RING_CAPACITY - 1U) % OPUS_RING_CAPACITY);
    }

    opus_slot_t item;
    memcpy(&item, &opus_ring[tail], sizeof(item));
    atomic_store_explicit(
        &opus_ring_tail, (uint8_t)((tail + 1U) % OPUS_RING_CAPACITY), memory_order_release);

    const uint8_t expected_src = item.slot == 0U
        ? DONGLE_MIC_SOURCE_CONTROLLER_0 : DONGLE_MIC_SOURCE_CONTROLLER_1;
    if (active_src != expected_src) {
        return;
    }

    const int decoded = opus_decode(
        opus_dec, item.data, (opus_int32)item.len, opus_pcm_48k, 480, 0);
    if (decoded >= 3) {
        if (opus_ok_logs < 4U) {
            ++opus_ok_logs;
            printf("[MIC] opus ok slot=%u len=%u samples=%d\r\n",
                   (unsigned)item.slot, (unsigned)item.len, decoded);
        }
        int16_t opus_pcm_16k[160];
        size_t out_samples = (size_t)decoded / 3U;
        if (out_samples > 160U) {
            out_samples = 160U;
        }
        for (size_t i = 0U; i < out_samples; ++i) {
            const int32_t avg = ((int32_t)opus_pcm_48k[i * 3U] +
                                 (int32_t)opus_pcm_48k[i * 3U + 1U] +
                                 (int32_t)opus_pcm_48k[i * 3U + 2U]) / 3;
            opus_pcm_16k[i] = (int16_t)avg;
        }
        write_mic_pcm(opus_pcm_16k, out_samples);
        atomic_fetch_add_explicit(&decoded_frames, 1U, memory_order_relaxed);
    } else {
        if (opus_err_logs < 4U) {
            ++opus_err_logs;
            printf("[MIC] opus err slot=%u len=%u rc=%d\r\n",
                   (unsigned)item.slot, (unsigned)item.len, decoded);
        }
        atomic_fetch_add_explicit(&error_frames, 1U, memory_order_relaxed);
    }
}

static void mic_dec_task(void *unused)
{
    (void)unused;

    uint8_t msbc_reasm[128];
    size_t msbc_reasm_len = 0U;
    bool debounced = false;
    bool prev_sco_open = false;
    bool prev_usb_open = false;
    TickType_t closed_at = 0;
    TickType_t last_live_log_at = 0;

    for (;;) {
        const bool hw_on = atomic_load_explicit(&sco_hw_active, memory_order_acquire);
        const bool opus_pending = prev_usb_open &&
            (atomic_load_explicit(&opus_ring_tail, memory_order_relaxed) !=
             atomic_load_explicit(&opus_ring_head, memory_order_acquire));
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(opus_pending ? 1U : (hw_on ? 2U : 20U)));

        if (atomic_exchange_explicit(&sco_debug_pending, false, memory_order_acq_rel)) {
            printf("[MIC] sco_cb #%lu interval=%u msbc=%u tx=(%08lx,%08lx:%lu) rx=(%08lx,%08lx:%lu) idx=%u rx[0..15]=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\r\n",
                   (unsigned long)sco_debug_snap.seq,
                   (unsigned)sco_debug_snap.interval_halfslot,
                   (unsigned)sco_debug_snap.is_msbc,
                   (unsigned long)sco_debug_snap.tx0,
                   (unsigned long)sco_debug_snap.tx1,
                   (unsigned long)sco_debug_snap.tx_sz,
                   (unsigned long)sco_debug_snap.rx0,
                   (unsigned long)sco_debug_snap.rx1,
                   (unsigned long)sco_debug_snap.rx_sz,
                   (unsigned)sco_debug_snap.index,
                   sco_debug_snap.rx_preview[0], sco_debug_snap.rx_preview[1],
                   sco_debug_snap.rx_preview[2], sco_debug_snap.rx_preview[3],
                   sco_debug_snap.rx_preview[4], sco_debug_snap.rx_preview[5],
                   sco_debug_snap.rx_preview[6], sco_debug_snap.rx_preview[7],
                   sco_debug_snap.rx_preview[8], sco_debug_snap.rx_preview[9],
                   sco_debug_snap.rx_preview[10], sco_debug_snap.rx_preview[11],
                   sco_debug_snap.rx_preview[12], sco_debug_snap.rx_preview[13],
                   sco_debug_snap.rx_preview[14], sco_debug_snap.rx_preview[15]);
        }

        const bool usb_open_now = dongle_usb_audio_mic_open();
        if (usb_open_now != prev_usb_open) {
            prev_usb_open = usb_open_now;
            if (opus_dec != NULL) {
                (void)opus_decoder_ctl(opus_dec, OPUS_RESET_STATE);
            }
            if (!usb_open_now) {
                opus_ok_logs = 0U;
                opus_err_logs = 0U;
            }
        }
        const TickType_t now = xTaskGetTickCount();
        if (usb_open_now) {
            debounced = true;
            closed_at = now;
        } else if (debounced &&
                   (TickType_t)(now - closed_at) >= pdMS_TO_TICKS(MIC_CLOSE_DEBOUNCE_MS)) {
            debounced = false;
        }
        atomic_store_explicit(&usb_mic_debounced_open, debounced, memory_order_release);

        const bool sco_open_now = atomic_load_explicit(&hfp_sco_open_state, memory_order_relaxed);
        if (sco_open_now != prev_sco_open) {
            prev_sco_open = sco_open_now;
            msbc_reasm_len = 0U;
            cvsd_prev_sample = 0;
            msbc_err_logs = 0U;
            sco_rx_nonzero_logs = 0U;
            if (sco_open_now) {
                atomic_store_explicit(&sco_debug_remaining, 4U, memory_order_relaxed);
                if (atomic_load_explicit(&hfp_codec_msbc_state, memory_order_relaxed)) {
                    reset_msbc_decoder();
                }
            }
        }

        if (atomic_load_explicit(&sco_hw_active, memory_order_acquire)) {
            const uint32_t trcntl = *(volatile const uint32_t *)BT_E_SCOTRCNTL0_ADDR;
            const uint8_t cpu_idx = (uint8_t)(((trcntl >> 31U) & 1U) ^ 1U);
            if (cpu_idx != sco_hw_last_idx) {
                sco_hw_last_idx = cpu_idx;
                const uint32_t poll_seq = (uint32_t)atomic_fetch_add_explicit(
                    &sco_cb_count, 1U, memory_order_relaxed) + 1U;
                const uint8_t *rx_ptr = (const uint8_t *)sco_hw_rx_buf[cpu_idx];
                uint8_t *tx_ptr = (uint8_t *)sco_hw_tx_buf[cpu_idx];
                const uint32_t rx_len = sco_hw_rx_len;
                const uint32_t tx_len = sco_hw_tx_len;
                const bool is_msbc = atomic_load_explicit(&hfp_codec_msbc_state, memory_order_relaxed);
                if (rx_ptr != NULL && rx_len > 0U && rx_len <= SCO_SLOT_MAX_BYTES) {
                    uint8_t rx_copy[SCO_SLOT_MAX_BYTES];
                    if ((uintptr_t)rx_ptr >= 0x62FC0000U) {
                        bflb_l1c_dcache_invalidate_range((void *)rx_ptr, rx_len);
                    }
                    memcpy(rx_copy, rx_ptr, rx_len);
                    fill_sco_tx_silence(tx_ptr, tx_len, is_msbc);

                    const size_t preview_n = rx_len < 16U ? rx_len : 16U;
                    bool has_nonzero = false;
                    for (size_t k = 0U; k < preview_n; ++k) {
                        if (rx_copy[k] != 0U) {
                            has_nonzero = true;
                            break;
                        }
                    }
                    if (has_nonzero && sco_rx_nonzero_logs < 4U) {
                        ++sco_rx_nonzero_logs;
                        uint8_t p[16] = { 0 };
                        memcpy(p, rx_copy, preview_n);
                        printf("[MIC] sco_rx #%lu idx=%u trcntl=%08lx rx[0..15]=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\r\n",
                               (unsigned long)poll_seq, (unsigned)cpu_idx,
                               (unsigned long)trcntl,
                               p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
                               p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
                    }
                    if (debounced) {
                        const uint8_t head = (uint8_t)atomic_load_explicit(&sco_ring_head, memory_order_relaxed);
                        const uint8_t tail = (uint8_t)atomic_load_explicit(&sco_ring_tail, memory_order_acquire);
                        const uint8_t next = (uint8_t)((head + 1U) % SCO_RING_CAPACITY);
                        if (next != tail) {
                            sco_ring[head].len = (uint8_t)rx_len;
                            sco_ring[head].is_msbc = is_msbc ? 1U : 0U;
                            memcpy(sco_ring[head].data, rx_copy, rx_len);
                            atomic_store_explicit(&sco_ring_head, next, memory_order_release);
                        } else {
                            atomic_fetch_add_explicit(&error_frames, 1U, memory_order_relaxed);
                        }
                    }
                }
            }
        }

        const uint8_t active_src = resolve_active_source();
        drain_sco_ring(msbc_reasm, &msbc_reasm_len, active_src, usb_open_now);
        drain_opus_ring(active_src, usb_open_now);

        if (usb_open_now) {
            if (last_live_log_at == 0 ||
                (TickType_t)(now - last_live_log_at) >= pdMS_TO_TICKS(1000U)) {
                last_live_log_at = now;
                printf("[MIC] live src=%u sco=%u msbc=%u cb=%lu dec=%lu err=%lu peak=%u\r\n",
                       (unsigned)active_src,
                       sco_open_now ? 1U : 0U,
                       atomic_load_explicit(&hfp_codec_msbc_state, memory_order_relaxed) ? 1U : 0U,
                       (unsigned long)atomic_load_explicit(&sco_cb_count, memory_order_relaxed),
                       (unsigned long)atomic_load_explicit(&decoded_frames, memory_order_relaxed),
                       (unsigned long)atomic_load_explicit(&error_frames, memory_order_relaxed),
                       (unsigned)local_decoded_peak);
                local_decoded_peak = 0U;
            }
        } else {
            last_live_log_at = 0;
            opus_ok_logs = 0U;
            opus_err_logs = 0U;
            local_decoded_peak = 0U;
        }
    }
}

void dongle_mic_pipeline_init(uint8_t saved_source)
{
    atomic_store_explicit(&configured_source,
        saved_source < DONGLE_MIC_SOURCE_COUNT ? saved_source : DONGLE_MIC_SOURCE_AUTO,
        memory_order_relaxed);
    atomic_store_explicit(&hfp_slc_ready_state, false, memory_order_relaxed);
    atomic_store_explicit(&hfp_sco_open_state, false, memory_order_relaxed);
    atomic_store_explicit(&hfp_codec_msbc_state, false, memory_order_relaxed);
    atomic_store_explicit(&ds_connected_mask_state, 0U, memory_order_relaxed);
    atomic_store_explicit(&usb_mic_debounced_open, false, memory_order_relaxed);
    atomic_store_explicit(&sco_cb_seen, false, memory_order_relaxed);
    atomic_store_explicit(&sco_debug_pending, false, memory_order_relaxed);
    atomic_store_explicit(&sco_debug_remaining, 4U, memory_order_relaxed);
    atomic_store_explicit(&sco_cb_count, 0U, memory_order_relaxed);
    atomic_store_explicit(&sco_hw_active, false, memory_order_relaxed);
    sco_hw_tx_buf[0] = NULL;
    sco_hw_tx_buf[1] = NULL;
    sco_hw_rx_buf[0] = NULL;
    sco_hw_rx_buf[1] = NULL;
    sco_hw_tx_len = 0U;
    sco_hw_rx_len = 0U;
    sco_hw_last_idx = 0U;
    sco_tx_h2_seq = 0U;
    sco_rx_nonzero_logs = 0U;
    atomic_store_explicit(&sco_ring_head, 0U, memory_order_relaxed);
    atomic_store_explicit(&sco_ring_tail, 0U, memory_order_relaxed);
    atomic_store_explicit(&opus_ring_head, 0U, memory_order_relaxed);
    atomic_store_explicit(&opus_ring_tail, 0U, memory_order_relaxed);
    atomic_store_explicit(&decoded_frames, 0U, memory_order_relaxed);
    atomic_store_explicit(&error_frames, 0U, memory_order_relaxed);
    msbc_err_logs = 0U;
    opus_ok_logs = 0U;
    opus_err_logs = 0U;
    local_decoded_peak = 0U;

    reset_msbc_decoder();

    const int dec_size = opus_decoder_get_size(1);
    if (opus_dec == NULL && dec_size > 0) {
        opus_dec = (OpusDecoder *)pvPortMalloc((size_t)dec_size);
        if (opus_dec != NULL) {
            const int init_err = opus_decoder_init(opus_dec, 48000, 1);
            if (init_err != OPUS_OK) {
                printf("[MIC] opus_decoder_init failed: %d\r\n", init_err);
                vPortFree(opus_dec);
                opus_dec = NULL;
            }
        } else {
            printf("[MIC] opus decoder malloc(%d) failed\r\n", dec_size);
        }
    }

    if (mic_task_handle == NULL) {
        if (xTaskCreate(mic_dec_task, "mic_dec", MIC_DEC_TASK_STACK_WORDS, NULL,
                        MIC_DEC_TASK_PRIORITY, &mic_task_handle) != pdPASS) {
            mic_task_handle = NULL;
            printf("[MIC] mic_dec task create failed\r\n");
        }
    }

    btble_controller_sco_codec_callback_register(sco_codec_isr);
}

bool dongle_mic_pipeline_set_source(uint8_t source)
{
    if (source >= DONGLE_MIC_SOURCE_COUNT) {
        return false;
    }
    atomic_store_explicit(&configured_source, source, memory_order_release);
    if (mic_task_handle != NULL) {
        xTaskNotifyGive(mic_task_handle);
    }
    return true;
}

uint8_t dongle_mic_pipeline_get_source(void)
{
    return (uint8_t)atomic_load_explicit(&configured_source, memory_order_relaxed);
}

void dongle_mic_pipeline_set_hfp_state(bool slc_ready, bool sco_open, bool codec_msbc)
{
    atomic_store_explicit(&hfp_slc_ready_state, slc_ready, memory_order_release);
    atomic_store_explicit(&hfp_sco_open_state, sco_open, memory_order_release);
    atomic_store_explicit(&hfp_codec_msbc_state, codec_msbc, memory_order_release);
    if (mic_task_handle != NULL) {
        xTaskNotifyGive(mic_task_handle);
    }
}

void dongle_mic_pipeline_set_ds_connected(uint8_t connected_mask)
{
    atomic_store_explicit(&ds_connected_mask_state, connected_mask & 0x03U, memory_order_release);
    if (mic_task_handle != NULL) {
        xTaskNotifyGive(mic_task_handle);
    }
}

bool dongle_mic_pipeline_desired_hfp_sco(void)
{
    return atomic_load_explicit(&usb_mic_debounced_open, memory_order_acquire) &&
           resolve_active_source() == DONGLE_MIC_SOURCE_HEADPHONE;
}

int8_t dongle_mic_pipeline_desired_ds_slot(void)
{
    if (!atomic_load_explicit(&usb_mic_debounced_open, memory_order_acquire)) {
        return -1;
    }
    const uint8_t active_src = resolve_active_source();
    if (active_src == DONGLE_MIC_SOURCE_CONTROLLER_0) {
        return 0;
    }
    if (active_src == DONGLE_MIC_SOURCE_CONTROLLER_1) {
        return 1;
    }
    return -1;
}

void dongle_mic_pipeline_feed_dualsense_opus(uint8_t slot, const uint8_t *opus, size_t len)
{
    if (slot >= 2U || opus == NULL || len == 0U || len > OPUS_SLOT_MAX_BYTES) {
        return;
    }
    if (!atomic_load_explicit(&usb_mic_debounced_open, memory_order_relaxed)) {
        return;
    }
    const uint8_t expected_src = slot == 0U
        ? DONGLE_MIC_SOURCE_CONTROLLER_0 : DONGLE_MIC_SOURCE_CONTROLLER_1;
    if (resolve_active_source() != expected_src) {
        return;
    }

    const uint8_t head = (uint8_t)atomic_load_explicit(&opus_ring_head, memory_order_relaxed);
    const uint8_t tail = (uint8_t)atomic_load_explicit(&opus_ring_tail, memory_order_acquire);
    const uint8_t next = (uint8_t)((head + 1U) % OPUS_RING_CAPACITY);
    if (next == tail) {
        atomic_fetch_add_explicit(&error_frames, 1U, memory_order_relaxed);
        return;
    }
    opus_ring[head].slot = slot;
    opus_ring[head].len = (uint8_t)len;
    memcpy(opus_ring[head].data, opus, len);
    atomic_store_explicit(&opus_ring_head, next, memory_order_release);
    if (mic_task_handle != NULL) {
        xTaskNotifyGive(mic_task_handle);
    }
}

size_t dongle_mic_pipeline_metrics(uint8_t out[DONGLE_MIC_STATE_PAYLOAD_SIZE])
{
    if (out == NULL) {
        return 0U;
    }
    bool usb_open = false;
    bool usb_muted = false;
    uint16_t peak = 0U;
    uint32_t sent_bytes = 0U;
    uint32_t usb_err = 0U;
    dongle_usb_audio_mic_metrics(&usb_open, &usb_muted, &peak, &sent_bytes, &usb_err);
    (void)sent_bytes;
    (void)usb_err;

    const uint8_t cfg = (uint8_t)atomic_load_explicit(&configured_source, memory_order_relaxed);
    const uint8_t active_src = resolve_active_source();
    const bool hfp_ready = atomic_load_explicit(&hfp_slc_ready_state, memory_order_relaxed);
    const bool sco_open = atomic_load_explicit(&hfp_sco_open_state, memory_order_relaxed);
    const bool codec_msbc = atomic_load_explicit(&hfp_codec_msbc_state, memory_order_relaxed);
    const uint8_t ds_mask = (uint8_t)atomic_load_explicit(&ds_connected_mask_state, memory_order_relaxed);
    const bool cb_seen = atomic_load_explicit(&sco_cb_seen, memory_order_relaxed);

    uint8_t active_codec = MIC_CODEC_NONE;
    if (active_src == DONGLE_MIC_SOURCE_HEADPHONE && sco_open) {
        active_codec = codec_msbc ? MIC_CODEC_MSBC : MIC_CODEC_CVSD;
    } else if (active_src == DONGLE_MIC_SOURCE_CONTROLLER_0 ||
               active_src == DONGLE_MIC_SOURCE_CONTROLLER_1) {
        active_codec = MIC_CODEC_OPUS;
    }

    const uint32_t dec_cnt = (uint32_t)atomic_load_explicit(&decoded_frames, memory_order_relaxed);
    const uint32_t err_cnt = (uint32_t)atomic_load_explicit(&error_frames, memory_order_relaxed);

    out[0] = 0U;
    out[1] = cfg;
    out[2] = active_src;
    out[3] = active_codec;
    out[4] = (uint8_t)((usb_open ? 0x01U : 0U) |
                       (usb_muted ? 0x02U : 0U) |
                       (hfp_ready ? 0x04U : 0U) |
                       (sco_open ? 0x08U : 0U) |
                       ((ds_mask & 0x01U) != 0U ? 0x10U : 0U) |
                       ((ds_mask & 0x02U) != 0U ? 0x20U : 0U) |
                       (cb_seen ? 0x40U : 0U));
    out[5] = 0U;
    out[6] = (uint8_t)peak;
    out[7] = (uint8_t)(peak >> 8U);
    out[8] = (uint8_t)dec_cnt;
    out[9] = (uint8_t)(dec_cnt >> 8U);
    out[10] = (uint8_t)(dec_cnt >> 16U);
    out[11] = (uint8_t)(dec_cnt >> 24U);
    out[12] = (uint8_t)err_cnt;
    out[13] = (uint8_t)(err_cnt >> 8U);
    out[14] = (uint8_t)(err_cnt >> 16U);
    out[15] = (uint8_t)(err_cnt >> 24U);
    return DONGLE_MIC_STATE_PAYLOAD_SIZE;
}
