#include "dongle/dualsense.h"
#include "dongle/usb_gamepad.h"
#include "dongle/usb_hid_transport.h"
#include "dongle/usb_profiles.h"

#include "task.h"
#include "usbd_core.h"
#include "usbd_hid.h"

#ifdef BL616
#include "bflb_core.h"
#include "bl616_memorymap.h"
#include "hardware/usb_v2_reg.h"
#endif

#include <string.h>

#define GAMEPAD_FIRST_EP 0x81U
#define GAMEPAD_FIRST_OUT_EP 0x01U
#define GAMEPAD_TASK_STACK_WORDS 512U
#define GAMEPAD_TEST_MS 500U

extern void dongle_bluetooth_local_address(uint8_t out[6]);
extern size_t dongle_bluetooth_dualsense_feature(
    uint8_t slot, uint8_t report_id, uint8_t *out, size_t capacity);
extern void dongle_bluetooth_send_dualsense_output_ex(
    uint8_t slot, const uint8_t *usb_report, size_t size, uint8_t path_flags);
extern bool dongle_bluetooth_send_dualsense_feature(
    uint8_t slot, const uint8_t *usb_report, size_t size);

static const uint8_t generic_report_descriptor[] = {
    0x05, 0x01,             /* Generic Desktop */
    0x09, 0x05,             /* Game Pad */
    0xa1, 0x01,             /* Application collection */
    0x85, DONGLE_USB_GAMEPAD_INPUT_ID,
    0x15, 0x00,             /* Logical minimum 0 */
    0x26, 0xff, 0x00,       /* Logical maximum 255 */
    0x75, 0x08, 0x95, 0x06, /* Six bytes */
    0x09, 0x30, 0x09, 0x31, /* X, Y */
    0x09, 0x33, 0x09, 0x34, /* Rx, Ry */
    0x09, 0x32, 0x09, 0x35, /* Z, Rz */
    0x81, 0x02,             /* Input: data, variable, absolute */
    0x05, 0x09,             /* Buttons */
    0x19, 0x01, 0x29, 0x10, /* Buttons 1..16 */
    0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x10,
    0x81, 0x02,
    0x05, 0x01, 0x09, 0x39, /* Hat switch */
    0x15, 0x00, 0x25, 0x07,
    0x35, 0x00, 0x46, 0x3b, 0x01, /* Physical range 0..315 */
    0x65, 0x14,             /* Degrees */
    0x75, 0x04, 0x95, 0x01,
    0x81, 0x42,             /* Input with null state (8) */
    0x65, 0x00,
    0x75, 0x04, 0x95, 0x01,
    0x81, 0x03,             /* Four padding bits */
    0xc0
};

static const uint8_t mgmt_report_descriptor[] = {
    /* Vendor-defined management collection, feature report over endpoint 0. */
    0x06, 0x00, 0xff, 0x09, 0x01, 0xa1, 0x01,
    0x85, DONGLE_HID_REPORT_ID,
    0x15, 0x00, 0x26, 0xff, 0x00,
    0x75, 0x08, 0x95, DONGLE_HID_FEATURE_SIZE,
    0x09, 0x01, 0xb1, 0x02, 0xc0
};

_Static_assert(sizeof(generic_report_descriptor) ==
                   DONGLE_USB_GAMEPAD_INPUT_DESCRIPTOR_SIZE,
               "Generic gamepad HID descriptor length must match interface descriptor");
_Static_assert(sizeof(mgmt_report_descriptor) ==
                   DONGLE_USB_MANAGEMENT_DESCRIPTOR_SIZE,
               "Management HID descriptor length must match interface descriptor");

static const uint8_t neutral_report[DONGLE_USB_GAMEPAD_REPORT_SIZE] = {
    128, 128, 128, 128, 0, 0, 0, 0, 8
};

static uint8_t gamepad_bus;
static volatile bool configured;
static volatile bool reset_pending;
static volatile bool out_ep_dedicated;
static volatile bool busy[DONGLE_USB_GAMEPAD_COUNT];
static volatile bool rx_busy[DONGLE_USB_GAMEPAD_COUNT];
static bool pending[DONGLE_USB_GAMEPAD_COUNT];
static bool test_active[DONGLE_USB_GAMEPAD_COUNT];
static TickType_t test_started[DONGLE_USB_GAMEPAD_COUNT];
static TickType_t last_sent[DONGLE_USB_GAMEPAD_COUNT];
static uint8_t idle_rate[DONGLE_USB_GAMEPAD_COUNT];
static uint8_t tx_counter[DONGLE_USB_GAMEPAD_COUNT];
static uint32_t ds_pkt_seq[DONGLE_USB_GAMEPAD_COUNT];
static uint32_t ds_sensor_ts[DONGLE_USB_GAMEPAD_COUNT];
static uint8_t ds_poll_phase[DONGLE_USB_GAMEPAD_COUNT];
static bool ds_first_sent[DONGLE_USB_GAMEPAD_COUNT];
static volatile bool switch_streaming[DONGLE_USB_GAMEPAD_COUNT];
static volatile bool switch_reply_pending[DONGLE_USB_GAMEPAD_COUNT];
static uint8_t switch_reply_buf[DONGLE_USB_GAMEPAD_COUNT][64];
static uint8_t raw_ds_reports[DONGLE_USB_GAMEPAD_COUNT][64];
static bool raw_ds_valid[DONGLE_USB_GAMEPAD_COUNT];
static uint8_t reports[DONGLE_USB_GAMEPAD_COUNT][DONGLE_USB_GAMEPAD_REPORT_SIZE];
/* Separate cache-aligned rows; DMA must not read/write active buffers. */
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX
    uint8_t tx_buffer[DONGLE_USB_GAMEPAD_COUNT][64];
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX
    uint8_t rx_buffer[DONGLE_USB_GAMEPAD_COUNT][64];
static struct usbd_interface mgmt_interface;
static struct usbd_interface interfaces[DONGLE_USB_GAMEPAD_COUNT];
static TaskHandle_t gamepad_task_handle;
static uint32_t submitted[DONGLE_USB_GAMEPAD_COUNT];
static volatile uint32_t completed[DONGLE_USB_GAMEPAD_COUNT];

uint16_t dongle_usb_gamepad_generic_descriptor(const uint8_t **descriptor)
{
    if (descriptor != NULL) {
        *descriptor = generic_report_descriptor;
    }
    return (uint16_t)sizeof(generic_report_descriptor);
}

static void slot_address(uint8_t slot, uint8_t out[6])
{
    static const uint8_t zero[6] = { 0 };
    dongle_bluetooth_local_address(out);
    if (memcmp(out, zero, sizeof(zero)) == 0) {
        out[0] = 0x02U;
        out[1] = 0x00U;
        out[2] = 0x00U;
        out[3] = 0x00U;
        out[4] = 0x00U;
        out[5] = (uint8_t)(1U + slot);
    } else {
        out[5] ^= slot;
    }
}

#ifndef BL616
static uint32_t out_fifo_bytes_override[DONGLE_USB_GAMEPAD_COUNT];
#endif

static uint32_t out_fifo_bytes(uint8_t i)
{
#ifdef BL616
    const uint32_t isg1 = getreg32(USB_BASE + USB_DEV_ISG1_OFFSET);
    if ((isg1 & ((USB_F0_OUT_INT | USB_F0_SPK_INT) << (2U * i))) == 0U) {
        return 0U;
    }
    return getreg32(USB_BASE + USB_DEV_FIBC0_OFFSET + 4U * i) & USB_BC_F0_MASK;
#else
    return i < DONGLE_USB_GAMEPAD_COUNT ? out_fifo_bytes_override[i] : 0U;
#endif
}

static bool fifo_is_empty(uint8_t i)
{
#ifdef BL616
    return (getreg32(USB_BASE + USB_DEV_CXCFE_OFFSET) & (USB_F0_EMP << i)) != 0U;
#else
    (void)i;
    return true;
#endif
}

static void gamepad_complete(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)nbytes;
    if (busid == gamepad_bus && ep >= GAMEPAD_FIRST_EP &&
        ep < GAMEPAD_FIRST_EP + DONGLE_USB_GAMEPAD_COUNT) {
        busy[ep - GAMEPAD_FIRST_EP] = false;
        ++completed[ep - GAMEPAD_FIRST_EP];
    }
}

static void put_le16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
}

static void put_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
    p[2] = (uint8_t)(value >> 16U);
    p[3] = (uint8_t)(value >> 24U);
}

static TickType_t ds_poll_min_gap_ticks(dongle_dualsense_poll_rate_t rate,
                                        uint8_t phase)
{
    if (rate == DONGLE_DS_POLL_500HZ) {
        return pdMS_TO_TICKS(2U);
    }
    if (rate == DONGLE_DS_POLL_750HZ) {
        return phase == 2U ? pdMS_TO_TICKS(2U) : pdMS_TO_TICKS(1U);
    }
    return pdMS_TO_TICKS(4U);
}

static uint32_t ds_sensor_ts_step(dongle_dualsense_poll_rate_t rate)
{
    if (rate == DONGLE_DS_POLL_500HZ) {
        return 6000U;
    }
    if (rate == DONGLE_DS_POLL_750HZ) {
        return 4000U;
    }
    return 12000U;
}

static size_t encode_gamepad_report(
    uint8_t slot, dongle_output_profile_t profile, uint8_t out[64])
{
    if (profile == DONGLE_PROFILE_DUALSENSE && raw_ds_valid[slot] &&
        !test_active[slot]) {
        memcpy(out, raw_ds_reports[slot], 64U);
        return 64U;
    }
    const size_t len =
        dongle_output_encode(profile, reports[slot], tx_counter[slot], out);
    if (profile == DONGLE_PROFILE_DUALSENSE && len == 64U) {
        put_le32(&out[12], ds_pkt_seq[slot]++);
        put_le32(&out[28], ds_sensor_ts[slot]);
        ds_sensor_ts[slot] +=
            ds_sensor_ts_step(dongle_usb_profile_dualsense_poll_rate());
        out[54] = 0x08U;
    }
    return len;
}

static uint8_t active_gamepad_count(void)
{
    return dongle_usb_profile_gamepad_count() == 2U ? 2U : 1U;
}

static void gamepad_out_complete(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    if (busid != gamepad_bus || ep < GAMEPAD_FIRST_OUT_EP ||
        ep >= GAMEPAD_FIRST_OUT_EP + DONGLE_USB_GAMEPAD_COUNT) {
        return;
    }
    const uint8_t i = out_ep_dedicated
        ? (ep == dongle_usb_profile_gamepad_out_ep(0U) ? 0U : DONGLE_USB_GAMEPAD_COUNT)
        : (uint8_t)(ep - GAMEPAD_FIRST_OUT_EP);
    if (i >= active_gamepad_count()) {
        return;
    }
    rx_busy[i] = false;
    const dongle_output_profile_t profile = dongle_usb_profile();
    const uint8_t path_flags = (uint8_t)(0x01U | (out_ep_dedicated ? 0x04U : 0U));
    if (profile == DONGLE_PROFILE_DUALSENSE) {
        if (nbytes >= DONGLE_DUALSENSE_USB_OUTPUT_SIZE &&
            nbytes <= sizeof(rx_buffer[i]) && rx_buffer[i][0] == 0x02U) {
            dongle_bluetooth_send_dualsense_output_ex(
                i, rx_buffer[i], nbytes, path_flags);
        }
    } else if (profile == DONGLE_PROFILE_XINPUT) {
        uint8_t ds_out[DONGLE_DUALSENSE_USB_OUTPUT_SIZE];
        if (nbytes <= sizeof(rx_buffer[i]) &&
            dongle_dualsense_from_xinput_output(rx_buffer[i], nbytes, ds_out)) {
            dongle_bluetooth_send_dualsense_output_ex(
                i, ds_out, sizeof(ds_out), (uint8_t)(path_flags | 0x08U));
        }
    } else if (profile == DONGLE_PROFILE_PS4) {
        uint8_t ds_out[DONGLE_DUALSENSE_USB_OUTPUT_SIZE];
        if (nbytes <= sizeof(rx_buffer[i]) &&
            dongle_dualsense_from_ps4_output(rx_buffer[i], nbytes, ds_out)) {
            dongle_bluetooth_send_dualsense_output_ex(
                i, ds_out, sizeof(ds_out), (uint8_t)(path_flags | 0x08U));
        }
    } else if (profile == DONGLE_PROFILE_SWITCH && nbytes > 0U &&
               nbytes <= sizeof(rx_buffer[i])) {
        uint8_t ds_out[DONGLE_DUALSENSE_USB_OUTPUT_SIZE];
        if (dongle_dualsense_from_switch_rumble(rx_buffer[i], nbytes, ds_out)) {
            dongle_bluetooth_send_dualsense_output_ex(
                i, ds_out, sizeof(ds_out), (uint8_t)(path_flags | 0x08U));
        }
        if (nbytes >= 2U && rx_buffer[i][0] == 0x80U) {
            if (rx_buffer[i][1] == 0x04U) {
                switch_streaming[i] = true;
            } else if (rx_buffer[i][1] == 0x05U) {
                switch_streaming[i] = false;
            }
        }
        uint8_t mac[6];
        slot_address(i, mac);
        const size_t reply_len = dongle_switch_reply(
            rx_buffer[i], nbytes, reports[i], tx_counter[i], mac,
            switch_reply_buf[i]);
        if (reply_len > 0U) {
            switch_reply_pending[i] = true;
        }
    }
    if (out_ep_dedicated && configured && !reset_pending) {
        rx_busy[0] = true;
        if (usbd_ep_start_read(gamepad_bus, dongle_usb_profile_gamepad_out_ep(0U),
                               rx_buffer[0], sizeof(rx_buffer[0])) != 0) {
            rx_busy[0] = false;
        }
    }
}

static struct usbd_endpoint endpoints[DONGLE_USB_GAMEPAD_COUNT] = {
    { .ep_addr = GAMEPAD_FIRST_EP, .ep_cb = gamepad_complete },
    { .ep_addr = GAMEPAD_FIRST_EP + 1U, .ep_cb = gamepad_complete },
};

static struct usbd_endpoint out_endpoints[DONGLE_USB_GAMEPAD_COUNT] = {
    { .ep_addr = GAMEPAD_FIRST_OUT_EP, .ep_cb = gamepad_out_complete },
    { .ep_addr = GAMEPAD_FIRST_OUT_EP + 1U, .ep_cb = gamepad_out_complete },
};

void dongle_usb_gamepad_event(uint8_t event)
{
    switch (event) {
        case USBD_EVENT_CONFIGURED:
            dongle_usb_hid_reset();
            reset_pending = true;
            configured = true;
            break;
        case USBD_EVENT_RESUME:
            configured = true;
            break;
        case USBD_EVENT_SUSPEND:
            configured = false;
            break;
        case USBD_EVENT_RESET:
        case USBD_EVENT_DISCONNECTED:
        case USBD_EVENT_DEINIT:
            dongle_usb_hid_reset();
            configured = false;
            reset_pending = true;
            /* These events cancel outstanding endpoint transfers. */
            busy[0] = false;
            busy[1] = false;
            rx_busy[0] = false;
            rx_busy[1] = false;
            switch_streaming[0] = false;
            switch_streaming[1] = false;
            switch_reply_pending[0] = false;
            switch_reply_pending[1] = false;
            raw_ds_valid[0] = false;
            raw_ds_valid[1] = false;
            break;
        default:
            break;
    }
}

static bool submit(uint8_t index, const uint8_t *report, size_t size,
                   bool diagnostic)
{
    if (index >= DONGLE_USB_GAMEPAD_COUNT || report == NULL ||
        size != DONGLE_USB_GAMEPAD_REPORT_SIZE || report[8] > 8U) {
        return false;
    }
    bool accepted = false;
    taskENTER_CRITICAL();
    if (configured && !reset_pending) {
        memcpy(reports[index], report, size);
        pending[index] = true;
        test_active[index] = diagnostic;
        test_started[index] = xTaskGetTickCount();
        accepted = true;
    }
    taskEXIT_CRITICAL();
    return accepted;
}

bool dongle_usb_gamepad_sink(void *context, uint8_t index,
                             const uint8_t *report, size_t size)
{
    (void)context;
    return submit(index, report, size, false);
}

bool dongle_usb_gamepad_dualsense_raw(uint8_t index, const uint8_t report[64])
{
    if (index >= DONGLE_USB_GAMEPAD_COUNT || report == NULL ||
        report[0] != 1U) {
        return false;
    }
    bool accepted = false;
    taskENTER_CRITICAL();
    if (configured && !reset_pending) {
        memcpy(raw_ds_reports[index], report, 64U);
        raw_ds_valid[index] = true;
        test_active[index] = false;
        pending[index] = true;
        accepted = true;
    }
    taskEXIT_CRITICAL();
    return accepted;
}

void dongle_usb_gamepad_dualsense_clear(uint8_t index)
{
    if (index >= DONGLE_USB_GAMEPAD_COUNT) {
        return;
    }
    taskENTER_CRITICAL();
    raw_ds_valid[index] = false;
    pending[index] = true;
    taskEXIT_CRITICAL();
}

bool dongle_usb_gamepad_test(uint8_t index, const uint8_t *report, size_t size)
{
    return submit(index, report, size, true);
}

bool dongle_usb_gamepad_diagnostics(uint8_t index, uint8_t *payload)
{
    if (index >= DONGLE_USB_GAMEPAD_COUNT || payload == NULL) {
        return false;
    }
    taskENTER_CRITICAL();
    payload[0] = (configured ? 1U : 0U) | (busy[index] ? 2U : 0U) |
        (pending[index] ? 4U : 0U) | (test_active[index] ? 8U : 0U);
    memcpy(&payload[1], reports[index], DONGLE_USB_GAMEPAD_REPORT_SIZE);
    const uint32_t counters[] = { submitted[index], completed[index],
        (uint32_t)xTaskGetTickCount(), (uint32_t)test_started[index] };
    for (unsigned i = 0; i < 4; ++i) {
        for (unsigned b = 0; b < 4; ++b) {
            payload[10 + i * 4 + b] = (uint8_t)(counters[i] >> (b * 8));
        }
    }
    taskEXIT_CRITICAL();
    return true;
}

static void gamepad_task(void *argument)
{
    (void)argument;
    for (;;) {
        taskENTER_CRITICAL();
        const TickType_t now = xTaskGetTickCount();
        const dongle_output_profile_t profile = dongle_usb_profile();
        const dongle_dualsense_poll_rate_t ds_poll =
            dongle_usb_profile_dualsense_poll_rate();
        const uint8_t gamepad_count = active_gamepad_count();
        if (reset_pending) {
            for (uint8_t i = 0; i < DONGLE_USB_GAMEPAD_COUNT; ++i) {
                memcpy(reports[i], neutral_report, sizeof(neutral_report));
                test_active[i] = false;
                idle_rate[i] = 0;
                tx_counter[i] = 0;
                ds_pkt_seq[i] = 0U;
                ds_sensor_ts[i] = 10200000U;
                ds_poll_phase[i] = 0U;
                ds_first_sent[i] = false;
                switch_streaming[i] = false;
                switch_reply_pending[i] = false;
                raw_ds_valid[i] = false;
                pending[i] = true;
            }
            reset_pending = false;
        }
        for (uint8_t i = 0; i < gamepad_count; ++i) {
            if (test_active[i] && (TickType_t)(now - test_started[i]) >=
                                      pdMS_TO_TICKS(GAMEPAD_TEST_MS)) {
                memcpy(reports[i], neutral_report, sizeof(neutral_report));
                pending[i] = true;
                test_active[i] = false;
            }
            const bool idle_due = idle_rate[i] != 0U &&
                (TickType_t)(now - last_sent[i]) >=
                    pdMS_TO_TICKS((uint32_t)idle_rate[i] * 4U);
            if (!configured) {
                continue;
            }
            if (out_ep_dedicated) {
                /* Dedicated FIFO F1 (EP 0x02): keep OUT read permanently armed
                 * independently of IN FIFO F0 (EP 0x81). */
                if (!rx_busy[0]) {
                    rx_busy[0] = true;
                    if (usbd_ep_start_read(gamepad_bus, dongle_usb_profile_gamepad_out_ep(0U),
                                           rx_buffer[0], sizeof(rx_buffer[0])) != 0) {
                        rx_busy[0] = false;
                    }
                }
                if (busy[0]) {
                    continue;
                }
                if (profile == DONGLE_PROFILE_SWITCH) {
                    if (switch_reply_pending[0]) {
                        memcpy(tx_buffer[0], switch_reply_buf[0], sizeof(tx_buffer[0]));
                        busy[0] = true;
                        if (usbd_ep_start_write(gamepad_bus, GAMEPAD_FIRST_EP,
                                                tx_buffer[0], sizeof(tx_buffer[0])) == 0) {
                            switch_reply_pending[0] = false;
                            ++submitted[0];
                            ++tx_counter[0];
                            last_sent[0] = now;
                        } else {
                            busy[0] = false;
                        }
                        continue;
                    }
                    if (!switch_streaming[0]) {
                        continue;
                    }
                }
            } else {
                /* Shared BL616 FIFO: never overlap IN write and OUT read on same ep_idx. */
                if (busy[i] || rx_busy[i]) {
                    continue;
                }
                if (profile == DONGLE_PROFILE_SWITCH) {
                    if (switch_reply_pending[i]) {
                        memcpy(tx_buffer[i], switch_reply_buf[i], sizeof(tx_buffer[i]));
                        busy[i] = true;
                        if (usbd_ep_start_write(gamepad_bus, (uint8_t)(GAMEPAD_FIRST_EP + i),
                                                tx_buffer[i], sizeof(tx_buffer[i])) == 0) {
                            switch_reply_pending[i] = false;
                            ++submitted[i];
                            ++tx_counter[i];
                            last_sent[i] = now;
                        } else {
                            busy[i] = false;
                        }
                        continue;
                    }
                    if (!switch_streaming[i] || out_fifo_bytes(i) > 0U) {
                        rx_busy[i] = true;
                        if (usbd_ep_start_read(gamepad_bus, dongle_usb_profile_gamepad_out_ep(i),
                                               rx_buffer[i], sizeof(rx_buffer[i])) != 0) {
                            rx_busy[i] = false;
                        }
                        continue;
                    }
                } else if (profile != DONGLE_PROFILE_GENERIC && out_fifo_bytes(i) > 0U) {
                    rx_busy[i] = true;
                    if (usbd_ep_start_read(gamepad_bus, dongle_usb_profile_gamepad_out_ep(i),
                                           rx_buffer[i], sizeof(rx_buffer[i])) != 0) {
                        rx_busy[i] = false;
                    }
                    continue;
                }
                if (profile != DONGLE_PROFILE_GENERIC && !fifo_is_empty(i)) {
                    continue;
                }
            }
            bool ds_periodic_due = false;
            if (profile == DONGLE_PROFILE_DUALSENSE) {
                const bool ds_pace_ready = !ds_first_sent[i] ||
                    (TickType_t)(now - last_sent[i]) >=
                        ds_poll_min_gap_ticks(ds_poll, ds_poll_phase[i]);
                if (!ds_pace_ready) {
                    continue;
                }
                ds_periodic_due = ds_poll != DONGLE_DS_POLL_250HZ ||
                    !raw_ds_valid[i] ||
                    (TickType_t)(now - last_sent[i]) >= pdMS_TO_TICKS(12U);
            }
            if (!pending[i] && !idle_due && !ds_periodic_due) {
                continue;
            }
            const size_t report_len = encode_gamepad_report(i, profile, tx_buffer[i]);
            if (report_len == 0U) {
                continue;
            }
            busy[i] = true;
            if (usbd_ep_start_write(gamepad_bus, (uint8_t)(GAMEPAD_FIRST_EP + i),
                                     tx_buffer[i], (uint32_t)report_len) == 0) {
                pending[i] = false;
                ++submitted[i];
                ++tx_counter[i];
                last_sent[i] = now;
                ds_first_sent[i] = true;
                ds_poll_phase[i] = (profile == DONGLE_PROFILE_DUALSENSE &&
                                    ds_poll == DONGLE_DS_POLL_750HZ)
                    ? (uint8_t)((ds_poll_phase[i] + 1U) % 3U)
                    : 0U;
            } else {
                busy[i] = false;
            }
        }
        const uint32_t delay_ms =
            (profile == DONGLE_PROFILE_DUALSENSE &&
             ds_poll != DONGLE_DS_POLL_250HZ) ? 1U : 4U;
        taskEXIT_CRITICAL();
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

void usbd_hid_get_report(uint8_t busid, uint8_t intf, uint8_t report_id,
                         uint8_t report_type, uint8_t **data, uint32_t *len)
{
    *len = 0;
    if (busid != gamepad_bus || data == NULL || *data == NULL) {
        return;
    }
    const uint8_t mgmt_intf = dongle_usb_profile_mgmt_interface();
    const uint8_t first_gp_intf = dongle_usb_profile_gamepad_first_interface();
    if (intf == mgmt_intf &&
        report_id == DONGLE_HID_REPORT_ID && report_type == 3U) {
        (*data)[0] = DONGLE_HID_REPORT_ID;
        dongle_usb_hid_get_feature(&(*data)[1]);
        *len = 1U + DONGLE_HID_FEATURE_SIZE;
        return;
    }
    if (intf < first_gp_intf ||
        intf >= first_gp_intf + active_gamepad_count()) {
        return;
    }
    const uint8_t slot = (uint8_t)(intf - first_gp_intf);
    const dongle_output_profile_t profile = dongle_usb_profile();
    if (report_type == 1U && profile != DONGLE_PROFILE_XINPUT) {
        const uint8_t expected_id =
            profile == DONGLE_PROFILE_SWITCH ? 0x30U : DONGLE_USB_GAMEPAD_INPUT_ID;
        if (report_id == expected_id || report_id == 0U) {
            *len = (uint32_t)encode_gamepad_report(slot, profile, *data);
        }
    } else if (profile == DONGLE_PROFILE_PS4 && report_type == 3U) {
        uint32_t feature_len = 0U;
        if (report_id == 0x02U) feature_len = 37U;
        else if (report_id == 0x03U) feature_len = 48U;
        else if (report_id == 0x12U) feature_len = 16U;
        else if (report_id == 0xa3U) feature_len = 49U;
        if (feature_len > 0U) {
            memset(*data, 0, feature_len);
            (*data)[0] = report_id;
            if (report_id == 0x12U) {
                uint8_t mac[6];
                slot_address(slot, mac);
                memcpy(&(*data)[1], mac, sizeof(mac));
            }
            *len = feature_len;
        }
    } else if (profile == DONGLE_PROFILE_DUALSENSE && report_type == 3U) {
        if (report_id == 0x85U) {
            (*data)[0] = 0x85U;
            (*data)[1] = 0U;
            (*data)[2] = 0U;
            *len = 3U;
            return;
        }
        const uint32_t feature_len = (uint32_t)dongle_dualsense_feature_size(report_id);
        if (feature_len > 0U) {
            memset(*data, 0, feature_len);
            (*data)[0] = report_id;
            const size_t cached = dongle_bluetooth_dualsense_feature(
                slot, report_id, *data, feature_len);
            if (cached == 0U) {
                if (report_id == 0x05U) {
                    static const int16_t cal[16] = {
                        0, 0, 0,
                        540, -540, 540, -540, 540, -540,
                        540, 540,
                        8192, -8192, 8192, -8192, 8192
                    };
                    for (unsigned i = 0U; i < 16U; ++i) {
                        put_le16(&(*data)[1U + i * 2U], (uint16_t)cal[i]);
                    }
                    put_le16(&(*data)[33], (uint16_t)(-8192));
                } else if (report_id == 0x09U) {
                    uint8_t dev_mac[6];
                    uint8_t host_mac[6];
                    slot_address(slot, dev_mac);
                    dongle_bluetooth_local_address(host_mac);
                    for (unsigned k = 0U; k < 6U; ++k) {
                        (*data)[1U + k] = dev_mac[5U - k];
                        (*data)[10U + k] = host_mac[5U - k];
                    }
                } else if (report_id == 0x20U) {
                    memcpy(&(*data)[1], "Jan 01 2025", 11U);
                    memcpy(&(*data)[12], "00:00:00", 8U);
                    put_le16(&(*data)[20], 2U);
                    put_le32(&(*data)[24], 0x00000314U);
                    put_le32(&(*data)[28], 0x01000000U);
                    put_le16(&(*data)[44], 0x0402U);
                }
            }
            *len = feature_len;
        }
    }
}

void usbd_hid_set_report(uint8_t busid, uint8_t intf, uint8_t report_id,
                         uint8_t report_type, uint8_t *report, uint32_t length)
{
    const uint8_t mgmt_intf = dongle_usb_profile_mgmt_interface();
    const uint8_t first_gp_intf = dongle_usb_profile_gamepad_first_interface();
    if (busid == gamepad_bus && intf == mgmt_intf &&
        report_id == DONGLE_HID_REPORT_ID && report_type == 3U &&
        length == 1U + DONGLE_HID_FEATURE_SIZE &&
        report[0] == DONGLE_HID_REPORT_ID) {
        dongle_usb_hid_set_feature(&report[1], DONGLE_HID_FEATURE_SIZE);
    } else if (busid == gamepad_bus &&
               intf >= first_gp_intf &&
               intf < first_gp_intf + active_gamepad_count() &&
               report != NULL) {
        const uint8_t slot = (uint8_t)(intf - first_gp_intf);
        const dongle_output_profile_t profile = dongle_usb_profile();
        if (profile == DONGLE_PROFILE_DUALSENSE) {
            if (report_type == 2U && report_id == 0x02U &&
                length >= DONGLE_DUALSENSE_USB_OUTPUT_SIZE && report[0] == 0x02U) {
                dongle_bluetooth_send_dualsense_output_ex(slot, report, length, 0x02U);
            } else if (report_type == 3U && length >= 2U && report[0] == report_id) {
                (void)dongle_bluetooth_send_dualsense_feature(slot, report, length);
            }
        } else if (profile == DONGLE_PROFILE_PS4 && report_type == 2U &&
                   report_id == 0x05U) {
            uint8_t ds_out[DONGLE_DUALSENSE_USB_OUTPUT_SIZE];
            if (dongle_dualsense_from_ps4_output(report, length, ds_out)) {
                dongle_bluetooth_send_dualsense_output_ex(
                    slot, ds_out, sizeof(ds_out), 0x02U | 0x08U);
            }
        }
    }
}

uint8_t usbd_hid_get_idle(uint8_t busid, uint8_t intf, uint8_t report_id)
{
    const uint8_t first_gp_intf = dongle_usb_profile_gamepad_first_interface();
    if (busid != gamepad_bus || (report_id != 0U && report_id != 1U && report_id != 0x30U) ||
        intf < first_gp_intf ||
        intf >= first_gp_intf + active_gamepad_count()) {
        return 0;
    }
    return idle_rate[intf - first_gp_intf];
}

void usbd_hid_set_idle(uint8_t busid, uint8_t intf, uint8_t report_id,
                       uint8_t duration)
{
    const uint8_t first_gp_intf = dongle_usb_profile_gamepad_first_interface();
    if (busid == gamepad_bus && (report_id == 0U || report_id == 1U || report_id == 0x30U) &&
        intf >= first_gp_intf &&
        intf < first_gp_intf + active_gamepad_count()) {
        idle_rate[intf - first_gp_intf] = duration;
    }
}

void dongle_usb_gamepad_attach(uint8_t busid)
{
    gamepad_bus = busid;
    configured = false;
    reset_pending = false;

    const dongle_output_profile_t profile = dongle_usb_profile();
    const uint8_t gamepad_count = active_gamepad_count();
    out_ep_dedicated = gamepad_count == 1U && profile != DONGLE_PROFILE_GENERIC;
    const uint8_t *desc = NULL;
    const uint16_t desc_len = dongle_usb_profile_report_descriptor(&desc);
    for (uint8_t i = 0U; i < gamepad_count; ++i) {
        busy[i] = false;
        rx_busy[i] = false;
        switch_streaming[i] = false;
        switch_reply_pending[i] = false;
        raw_ds_valid[i] = false;
        ds_poll_phase[i] = 0U;
        ds_first_sent[i] = false;
        tx_counter[i] = 0U;
        if (profile == DONGLE_PROFILE_XINPUT) {
            memset(&interfaces[i], 0, sizeof(interfaces[i]));
            usbd_add_interface(busid, &interfaces[i]);
        } else {
            usbd_add_interface(busid, usbd_hid_init_intf(
                busid, &interfaces[i], desc, desc_len));
        }
        usbd_add_endpoint(busid, &endpoints[i]);
        if (profile != DONGLE_PROFILE_GENERIC) {
            out_endpoints[i].ep_addr = dongle_usb_profile_gamepad_out_ep(i);
            usbd_add_endpoint(busid, &out_endpoints[i]);
        }
    }
    usbd_add_interface(busid, usbd_hid_init_intf(busid, &mgmt_interface,
        mgmt_report_descriptor, sizeof(mgmt_report_descriptor)));
}

BaseType_t dongle_usb_gamepad_register(uint8_t busid)
{
    if (gamepad_task_handle != NULL) {
        return pdFAIL;
    }
    for (uint8_t i = 0U; i < DONGLE_USB_GAMEPAD_COUNT; ++i) {
        memcpy(reports[i], neutral_report, sizeof(neutral_report));
    }
    dongle_usb_gamepad_attach(busid);
    return xTaskCreate(gamepad_task, "usb_hid", GAMEPAD_TASK_STACK_WORDS, NULL,
                        tskIDLE_PRIORITY + 2U, &gamepad_task_handle);
}
