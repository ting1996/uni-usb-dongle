#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Compile the real backend against a deterministic USB/RTOS harness. */
#include "../firmware/src/usb_gamepad.c"

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #condition); exit(1); \
} } while (0)

static uint8_t feature_data[DONGLE_HID_FEATURE_SIZE];
void dongle_usb_hid_reset(void) { memset(feature_data, 0, sizeof(feature_data)); }
void dongle_usb_hid_set_feature(const uint8_t *data, size_t size)
{ CHECK(size == sizeof(feature_data)); memcpy(feature_data, data, size); }
void dongle_usb_hid_get_feature(uint8_t *data)
{ memcpy(data, feature_data, sizeof(feature_data)); }

static dongle_output_profile_t test_profile = DONGLE_PROFILE_GENERIC;
static uint8_t test_gamepad_count = DONGLE_USB_GAMEPAD_COUNT;
static uint8_t test_local_mac[6] = { 0xb4, 0x0e, 0xcf, 0x35, 0x9b, 0xc8 };
static bool mock_ds_feat_enabled;
static uint8_t mock_ds_feat_05[41];
static bool mock_ds_feat_81_enabled;
static uint8_t mock_ds_feat_81[64];
static unsigned bt_ds_out_count;
static uint8_t bt_ds_out_slot;
static uint8_t bt_ds_out_buf[64];
static uint32_t bt_ds_out_len;
static unsigned bt_ds_feat_count;
static uint8_t bt_ds_feat_slot;
static uint8_t bt_ds_feat_buf[64];
static uint32_t bt_ds_feat_len;
dongle_output_profile_t dongle_usb_profile(void) { return test_profile; }
uint8_t dongle_usb_profile_gamepad_count(void) { return test_gamepad_count; }
uint16_t dongle_usb_profile_report_descriptor(const uint8_t **descriptor)
{
    static const uint8_t dummy_ps4[148] = { 0x05, 0x01, 0x09, 0x05 };
    static const uint8_t dummy_switch[94] = { 0x05, 0x01, 0x09, 0x05 };
    static const uint8_t dummy_dualsense[154] = { 0x05, 0x01, 0x09, 0x05 };
    if (test_profile == DONGLE_PROFILE_PS4) {
        *descriptor = dummy_ps4;
        return sizeof(dummy_ps4);
    }
    if (test_profile == DONGLE_PROFILE_SWITCH) {
        *descriptor = dummy_switch;
        return sizeof(dummy_switch);
    }
    if (test_profile == DONGLE_PROFILE_DUALSENSE) {
        *descriptor = dummy_dualsense;
        return sizeof(dummy_dualsense);
    }
    return dongle_usb_gamepad_generic_descriptor(descriptor);
}
void dongle_bluetooth_local_address(uint8_t out[6])
{
    memcpy(out, test_local_mac, sizeof(test_local_mac));
}
size_t dongle_bluetooth_dualsense_feature(
    uint8_t slot, uint8_t report_id, uint8_t *out, size_t capacity)
{
    if (slot == 0U && mock_ds_feat_enabled && report_id == 0x05U &&
        capacity >= sizeof(mock_ds_feat_05)) {
        memcpy(out, mock_ds_feat_05, sizeof(mock_ds_feat_05));
        return sizeof(mock_ds_feat_05);
    }
    if (slot == 0U && mock_ds_feat_81_enabled && report_id == 0x81U &&
        capacity >= sizeof(mock_ds_feat_81)) {
        memcpy(out, mock_ds_feat_81, sizeof(mock_ds_feat_81));
        return sizeof(mock_ds_feat_81);
    }
    return 0U;
}
void dongle_bluetooth_send_dualsense_output(
    uint8_t slot, const uint8_t *usb_report, size_t size)
{
    ++bt_ds_out_count;
    bt_ds_out_slot = slot;
    bt_ds_out_len = (uint32_t)size;
    if (usb_report && size <= sizeof(bt_ds_out_buf)) {
        memcpy(bt_ds_out_buf, usb_report, size);
    }
}
bool dongle_bluetooth_send_dualsense_feature(
    uint8_t slot, const uint8_t *usb_report, size_t size)
{
    ++bt_ds_feat_count;
    bt_ds_feat_slot = slot;
    bt_ds_feat_len = (uint32_t)size;
    if (usb_report && size <= sizeof(bt_ds_feat_buf)) {
        memcpy(bt_ds_feat_buf, usb_report, size);
    }
    return true;
}

static jmp_buf task_yield;
static TickType_t clock_ticks;
static unsigned sends[2];
static unsigned reads[2];
static uint32_t last_send_len[2];
static uint8_t sent[2][64];
static uint8_t *pending_rx_buf[2];
static uint32_t pending_rx_cap[2];
static unsigned interface_count;
static unsigned in_ep_count;
static unsigned out_ep_count;
static bool reject_write;

TickType_t xTaskGetTickCount(void) { return clock_ticks; }
void vTaskDelay(TickType_t ticks) { (void)ticks; longjmp(task_yield, 1); }
BaseType_t xTaskCreate(void (*entry)(void *), const char *name, unsigned stack,
                       void *arg, unsigned priority, TaskHandle_t *handle)
{
    (void)entry; (void)name; (void)stack; (void)arg; (void)priority;
    *handle = &clock_ticks;
    return pdPASS;
}
void usbd_add_interface(uint8_t bus, struct usbd_interface *intf)
{
    (void)bus; (void)intf; ++interface_count;
}
void usbd_add_endpoint(uint8_t bus, struct usbd_endpoint *ep)
{
    (void)bus;
    if (ep->ep_addr & 0x80U) {
        CHECK(ep->ep_addr == 0x81U || ep->ep_addr == 0x82U);
        ++in_ep_count;
    } else {
        CHECK(ep->ep_addr == 0x01U || ep->ep_addr == 0x02U);
        ++out_ep_count;
    }
}
struct usbd_interface *usbd_hid_init_intf(uint8_t bus,
    struct usbd_interface *intf, const uint8_t *descriptor, uint32_t length)
{
    (void)bus;
    if (descriptor == mgmt_report_descriptor) {
        CHECK(length == DONGLE_USB_MANAGEMENT_DESCRIPTOR_SIZE);
    } else {
        if (test_profile == DONGLE_PROFILE_PS4) CHECK(length == 148U);
        else if (test_profile == DONGLE_PROFILE_SWITCH) CHECK(length == 94U);
        else if (test_profile == DONGLE_PROFILE_DUALSENSE) CHECK(length == 154U);
        else CHECK(length == DONGLE_USB_GAMEPAD_INPUT_DESCRIPTOR_SIZE);
        CHECK(descriptor[3] == 5U);
    }
    return intf;
}
static void assert_fifo_exclusivity(void)
{
    for (unsigned i = 0; i < DONGLE_USB_GAMEPAD_COUNT; ++i) {
        CHECK(!(busy[i] && rx_busy[i]));
    }
}
int usbd_ep_start_write(uint8_t bus, uint8_t ep, const uint8_t *data,
                        uint32_t length)
{
    CHECK(bus == 0 && ep >= 0x81U && ep <= 0x82U);
    const unsigned slot = (unsigned)(ep - 0x81U);
    CHECK(!rx_busy[slot]);
    if (test_profile == DONGLE_PROFILE_GENERIC) CHECK(length == 10U && data[0] == 1U);
    else if (test_profile == DONGLE_PROFILE_XINPUT) CHECK(length == 20U && data[1] == 20U);
    else if (test_profile == DONGLE_PROFILE_PS4) CHECK(length == 64U && data[0] == 1U);
    else if (test_profile == DONGLE_PROFILE_SWITCH) CHECK(length == 64U);
    else if (test_profile == DONGLE_PROFILE_DUALSENSE) CHECK(length == 64U && data[0] == 1U);
    if (reject_write) { return -1; }
    ++sends[slot];
    last_send_len[slot] = length;
    memcpy(sent[slot], data, length);
    return 0;
}
int usbd_ep_start_read(uint8_t bus, uint8_t ep, uint8_t *data, uint32_t length)
{
    CHECK(bus == 0 && ep >= 0x01U && ep <= 0x02U && length == 64U && data != NULL);
    const unsigned slot = (unsigned)(ep - 0x01U);
    CHECK(!busy[slot]);
    ++reads[slot];
    pending_rx_buf[slot] = data;
    pending_rx_cap[slot] = length;
    return 0;
}
static void poll_at(TickType_t tick)
{
    clock_ticks = tick;
    if (setjmp(task_yield) == 0) { gamepad_task(NULL); }
    assert_fifo_exclusivity();
}
static void complete_both(void)
{
    gamepad_complete(0, 0x81U, last_send_len[0]);
    gamepad_complete(0, 0x82U, last_send_len[1]);
    assert_fifo_exclusivity();
}
static void deliver_out(uint8_t slot, const uint8_t *bytes, uint32_t len)
{
    CHECK(rx_busy[slot] && len <= pending_rx_cap[slot]);
    memcpy(pending_rx_buf[slot], bytes, len);
    gamepad_out_complete(0, (uint8_t)(GAMEPAD_FIRST_OUT_EP + slot), len);
    assert_fifo_exclusivity();
}

int main(void)
{
    const uint8_t input[9] = { 10, 20, 30, 40, 50, 60, 1, 0, 3 };
    uint8_t replacement[9];
    memcpy(replacement, input, 9);
    replacement[0] = 240;

    CHECK(dongle_usb_gamepad_register(0) == pdPASS);
    CHECK(interface_count == 3U && in_ep_count == 2U && out_ep_count == 0U);
    CHECK(dongle_usb_gamepad_register(0) == pdFAIL);
    CHECK(!dongle_usb_gamepad_sink(NULL, 0, input, 9));
    dongle_usb_gamepad_event(USBD_EVENT_CONFIGURED);
    poll_at(0);
    CHECK(sends[0] == 1 && sends[1] == 1);
    CHECK(memcmp(&sent[0][1], neutral_report, 9) == 0);
    CHECK(!dongle_usb_gamepad_test(2, input, 9));
    CHECK(!dongle_usb_gamepad_test(0, NULL, 9));
    CHECK(!dongle_usb_gamepad_test(0, input, 8));
    uint8_t invalid[9] = { 0 };
    invalid[8] = 9;
    CHECK(!dongle_usb_gamepad_test(0, invalid, 9));

    /* A busy endpoint owns its DMA buffer; later reports replace the queue. */
    CHECK(dongle_usb_gamepad_sink(NULL, 0, input, 9));
    poll_at(4);
    CHECK(sends[0] == 1);
    CHECK(memcmp(&tx_buffer[0][1], neutral_report, 9) == 0);
    CHECK(dongle_usb_gamepad_sink(NULL, 0, replacement, 9));
    complete_both();
    poll_at(8);
    CHECK(sends[0] == 2 && sends[1] == 1);
    CHECK(memcmp(&sent[0][1], replacement, 9) == 0);
    CHECK(memcmp(reports[1], neutral_report, 9) == 0);

    /* Failed transfers are retried without losing a report. */
    CHECK(dongle_usb_gamepad_sink(NULL, 1, input, 9));
    reject_write = true;
    poll_at(12);
    CHECK(sends[1] == 1 && !busy[1]);
    reject_write = false;
    poll_at(16);
    CHECK(sends[1] == 2 && memcmp(&sent[1][1], input, 9) == 0);
    complete_both();

    /* Transient input expires even across tick wraparound and USB suspend. */
    clock_ticks = UINT32_MAX - 100U;
    CHECK(dongle_usb_gamepad_test(0, input, 9));
    poll_at(clock_ticks);
    complete_both();
    poll_at(398);
    CHECK(memcmp(reports[0], input, 9) == 0);
    dongle_usb_gamepad_event(USBD_EVENT_SUSPEND);
    CHECK(!dongle_usb_gamepad_sink(NULL, 0, input, 9));
    const unsigned before_expiry = sends[0];
    poll_at(399);
    CHECK(memcmp(reports[0], neutral_report, 9) == 0);
    CHECK(sends[0] == before_expiry);
    dongle_usb_gamepad_event(USBD_EVENT_RESUME);
    poll_at(403);
    CHECK(sends[0] == before_expiry + 1);
    CHECK(memcmp(&sent[0][1], neutral_report, 9) == 0);
    complete_both();

    /* Control reports use dedicated management intf and gamepad intfs 4..5. */
    uint8_t control[64] = { 0 };
    uint8_t *control_ptr = control;
    uint32_t length = sizeof(control);
    usbd_hid_get_report(0, DONGLE_USB_GAMEPAD_FIRST_INTERFACE, 1, 1, &control_ptr, &length);
    CHECK(length == 10 && control[0] == 1 && memcmp(&control[1], neutral_report, 9) == 0);
    usbd_hid_get_report(0, 1, 0, 1, &control_ptr, &length);
    CHECK(length == 0);
    usbd_hid_set_idle(0, DONGLE_USB_GAMEPAD_FIRST_INTERFACE, 0, 2);
    CHECK(usbd_hid_get_idle(0, DONGLE_USB_GAMEPAD_FIRST_INTERFACE, 0) == 2);
    const unsigned before_idle = sends[0];
    poll_at(411);
    CHECK(sends[0] == before_idle + 1);
    complete_both();

    /* Reset discards queued inputs and clears busy transfers and idle rates. */
    CHECK(dongle_usb_gamepad_sink(NULL, 1, input, 9));
    dongle_usb_gamepad_event(USBD_EVENT_RESET);
    CHECK(!busy[0] && !busy[1]);
    dongle_usb_gamepad_event(USBD_EVENT_CONFIGURED);
    poll_at(415);
    CHECK(memcmp(&sent[1][1], neutral_report, 9) == 0);
    CHECK(usbd_hid_get_idle(0, DONGLE_USB_GAMEPAD_FIRST_INTERFACE, 0) == 0);
    uint8_t diagnostics[DONGLE_USB_GAMEPAD_DIAGNOSTIC_SIZE];
    CHECK(!dongle_usb_gamepad_diagnostics(2, diagnostics));
    CHECK(!dongle_usb_gamepad_diagnostics(0, NULL));
    CHECK(dongle_usb_gamepad_diagnostics(1, diagnostics));
    CHECK(memcmp(&diagnostics[1], neutral_report, 9) == 0);
    uint8_t feature[64] = { DONGLE_HID_REPORT_ID };
    feature[4] = 0x55;
    usbd_hid_set_report(0, DONGLE_USB_GAMEPAD_FIRST_INTERFACE, 2, 3, feature, sizeof(feature));
    CHECK(feature_data[3] == 0); /* Gamepad interfaces reject mgmt feature reports. */
    usbd_hid_set_report(0, DONGLE_MANAGEMENT_INTERFACE, 2, 3, feature, 63);
    CHECK(feature_data[3] == 0);
    usbd_hid_set_report(0, DONGLE_MANAGEMENT_INTERFACE, 2, 3, feature, sizeof(feature));
    CHECK(feature_data[3] == 0x55);
    usbd_hid_get_report(0, DONGLE_MANAGEMENT_INTERFACE, 2, 3, &control_ptr, &length);
    CHECK(length == 64 && control[0] == 2 && control[4] == 0x55);

    /* XInput profile attachment sends 20-byte reports and never arms OUT reads. */
    test_profile = DONGLE_PROFILE_XINPUT;
    dongle_usb_gamepad_attach(0);
    CHECK(out_ep_count == 2U);
    dongle_usb_gamepad_event(USBD_EVENT_CONFIGURED);
    poll_at(420);
    CHECK(last_send_len[0] == 20U && sent[0][1] == 20U);
    CHECK(reads[0] == 0U && reads[1] == 0U);
    complete_both();

    /* PS4 profile sends 64-byte input reports and answers PS4 feature reports. */
    test_profile = DONGLE_PROFILE_PS4;
    dongle_usb_gamepad_attach(0);
    dongle_usb_gamepad_event(USBD_EVENT_CONFIGURED);
    poll_at(425);
    CHECK(last_send_len[0] == 64U && sent[0][0] == 1U && sent[0][30] == 0x1bU);
    complete_both();
    usbd_hid_get_report(0, DONGLE_USB_GAMEPAD_FIRST_INTERFACE, 0x02, 3, &control_ptr, &length);
    CHECK(length == 37U && control[0] == 0x02U);
    usbd_hid_get_report(0, DONGLE_USB_GAMEPAD_FIRST_INTERFACE, 0x03, 3, &control_ptr, &length);
    CHECK(length == 48U && control[0] == 0x03U);
    usbd_hid_get_report(0, DONGLE_USB_GAMEPAD_FIRST_INTERFACE + 1U, 0x12, 3, &control_ptr, &length);
    CHECK(length == 16U && control[0] == 0x12U && control[6] == (test_local_mac[5] ^ 1U));
    usbd_hid_get_report(0, DONGLE_USB_GAMEPAD_FIRST_INTERFACE, 0xa3, 3, &control_ptr, &length);
    CHECK(length == 49U && control[0] == 0xa3U);

    /* Switch profile serializes EP OUT handshakes and EP IN replies on shared FIFOs. */
    test_profile = DONGLE_PROFILE_SWITCH;
    dongle_usb_gamepad_attach(0);
    dongle_usb_gamepad_event(USBD_EVENT_CONFIGURED);
    poll_at(430);
    CHECK(rx_busy[0] && rx_busy[1]);
    CHECK(!busy[0] && !busy[1]);
    const uint8_t sw_mac[2] = { 0x80U, 0x01U };
    deliver_out(0, sw_mac, sizeof(sw_mac));
    CHECK(switch_reply_pending[0]);
    poll_at(434);
    CHECK(sent[0][0] == 0x81U && sent[0][1] == 0x01U && sent[0][4] == test_local_mac[0]);
    complete_both();

    poll_at(438);
    CHECK(rx_busy[0]);
    const uint8_t sw_stream[2] = { 0x80U, 0x04U };
    deliver_out(0, sw_stream, sizeof(sw_stream));
    poll_at(442);
    CHECK(sent[0][0] == 0x81U && sent[0][1] == 0x04U && switch_streaming[0]);
    complete_both();

    /* Once streaming is active, normal input reports use 0x30 format. */
    poll_at(446);
    CHECK(last_send_len[0] == 64U && sent[0][0] == 0x30U);
    complete_both();

    /* Subcommand replies also reuse EP IN without overlapping RX. */
    const uint8_t sub_info[12] = { 0x01U, 0,0,0,0,0,0,0,0,0, 0x02U, 0 };
    deliver_out(1, sub_info, sizeof(sub_info));
    poll_at(450);
    CHECK(sent[1][0] == 0x21U && sent[1][13] == 0x82U);
    complete_both();

    /* DualSense profile: raw 64B input passthrough, features, and OUT forwarding. */
    test_profile = DONGLE_PROFILE_DUALSENSE;
    dongle_usb_gamepad_attach(0);
    dongle_usb_gamepad_event(USBD_EVENT_CONFIGURED);
    poll_at(455);
    CHECK(last_send_len[0] == 64U && sent[0][0] == 1U && sent[0][53] == 0x1aU);
    complete_both();

    uint8_t raw_ds[64] = { 1U, 0x11U, 0x22U };
    raw_ds[16] = 0x77U; raw_ds[33] = 0x42U; raw_ds[53] = 0x18U;
    CHECK(dongle_usb_gamepad_dualsense_raw(0, raw_ds));
    poll_at(460);
    CHECK(last_send_len[0] == 64U && sent[0][16] == 0x77U &&
          sent[0][33] == 0x42U && sent[0][53] == 0x18U);
    complete_both();
    dongle_usb_gamepad_dualsense_clear(0);
    poll_at(464);
    CHECK(sent[0][16] == 0U && sent[0][53] == 0x1aU);
    complete_both();

    /* DualSense feature fallbacks and cached override. */
    usbd_hid_get_report(0, DONGLE_USB_GAMEPAD_FIRST_INTERFACE, 0x05, 3, &control_ptr, &length);
    CHECK(length == 41U && control[0] == 0x05U && (control[7] | control[8]) != 0U);
    mock_ds_feat_05[0] = 0x05U; mock_ds_feat_05[1] = 0x5aU;
    mock_ds_feat_enabled = true;
    usbd_hid_get_report(0, DONGLE_USB_GAMEPAD_FIRST_INTERFACE, 0x05, 3, &control_ptr, &length);
    CHECK(length == 41U && control[1] == 0x5aU);
    mock_ds_feat_enabled = false;
    usbd_hid_get_report(0, DONGLE_USB_GAMEPAD_FIRST_INTERFACE + 1U, 0x09, 3, &control_ptr, &length);
    CHECK(length == 20U && control[0] == 0x09U &&
          control[1] == (test_local_mac[5] ^ 1U) && control[10] == test_local_mac[5]);
    usbd_hid_get_report(0, DONGLE_USB_GAMEPAD_FIRST_INTERFACE, 0x20, 3, &control_ptr, &length);
    CHECK(length == 64U && control[0] == 0x20U && control[24] == 0x14U && control[44] == 0x02U);

    /* Output report 0x02 forwarded both via Interrupt OUT (FIFO gated) and SET_REPORT. */
    uint8_t ds_out[48] = { 0x02U, 0x01U, 0x02U, 0x55U };
    out_fifo_bytes_override[0] = sizeof(ds_out);
    poll_at(468);
    CHECK(rx_busy[0] && !busy[0]);
    out_fifo_bytes_override[0] = 0U;
    deliver_out(0, ds_out, sizeof(ds_out));
    CHECK(bt_ds_out_count == 1U && bt_ds_out_slot == 0U && bt_ds_out_buf[3] == 0x55U);

    ds_out[3] = 0xaaU;
    usbd_hid_set_report(0, DONGLE_USB_GAMEPAD_FIRST_INTERFACE + 1U, 0x02, 2, ds_out, sizeof(ds_out));
    CHECK(bt_ds_out_count == 2U && bt_ds_out_slot == 1U && bt_ds_out_buf[3] == 0xaaU);

    /* Bidirectional SET_FEATURE (0x80/0x08) and GET_FEATURE (0x81) on gamepad interfaces. */
    uint8_t ds_feat_80[64] = { 0x80U, 0x01U, 0x13U };
    usbd_hid_set_report(0, DONGLE_USB_GAMEPAD_FIRST_INTERFACE, 0x80U, 3U, ds_feat_80, sizeof(ds_feat_80));
    CHECK(bt_ds_feat_count == 1U && bt_ds_feat_slot == 0U &&
          bt_ds_feat_len == 64U && bt_ds_feat_buf[1] == 0x01U && bt_ds_feat_buf[2] == 0x13U);

    uint8_t ds_feat_08[48] = { 0x08U, 0x02U, 0x04U };
    usbd_hid_set_report(0, DONGLE_USB_GAMEPAD_FIRST_INTERFACE + 1U, 0x08U, 3U, ds_feat_08, sizeof(ds_feat_08));
    CHECK(bt_ds_feat_count == 2U && bt_ds_feat_slot == 1U &&
          bt_ds_feat_len == 48U && bt_ds_feat_buf[1] == 0x02U && bt_ds_feat_buf[2] == 0x04U);

    /* Management interface does not forward 0x80 as a DualSense feature report. */
    usbd_hid_set_report(0, DONGLE_MANAGEMENT_INTERFACE, 0x80U, 3U, ds_feat_80, sizeof(ds_feat_80));
    CHECK(bt_ds_feat_count == 2U);

    mock_ds_feat_81[0] = 0x81U; mock_ds_feat_81[1] = 0x01U; mock_ds_feat_81[2] = 0x13U;
    mock_ds_feat_81[3] = 0x02U; mock_ds_feat_81[4] = 'S';
    mock_ds_feat_81_enabled = true;
    usbd_hid_get_report(0, DONGLE_USB_GAMEPAD_FIRST_INTERFACE, 0x81U, 3U, &control_ptr, &length);
    CHECK(length == 64U && control[0] == 0x81U && control[3] == 0x02U && control[4] == 'S');
    mock_ds_feat_81_enabled = false;

    /* Single-controller USB mode exposes only MI_04 and EP 0x81/0x01. */
    interface_count = in_ep_count = out_ep_count = 0U;
    const unsigned before_sends_0 = sends[0];
    const unsigned before_sends_1 = sends[1];
    test_gamepad_count = 1U;
    dongle_usb_gamepad_attach(0);
    CHECK(interface_count == 2U && in_ep_count == 1U && out_ep_count == 1U);
    dongle_usb_gamepad_event(USBD_EVENT_CONFIGURED);
    poll_at(500);
    CHECK(sends[0] == before_sends_0 + 1U && sends[1] == before_sends_1);
    complete_both();
    usbd_hid_get_report(0, DONGLE_USB_GAMEPAD_FIRST_INTERFACE + 1U, 0x05U, 3U, &control_ptr, &length);
    CHECK(length == 0U);

    puts("USB gamepad backend tests passed");
    return 0;
}
