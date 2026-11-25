#include "dongle/usb_ota_service.h"

#include "FreeRTOS.h"
#include "dongle/usb_ota_protocol.h"
#include "dongle/settings_store.h"
#include "dongle/prefs_codec.h"
#include "dongle/device_blocklist.h"
#include "dongle/mic_pipeline.h"
#include "dongle/usb_gamepad.h"
#include "dongle/usb_hid_transport.h"
#include "dongle/bluetooth.h"
#include "dongle/usb_audio.h"
#include "dongle/usb_profiles.h"
#include "easyflash.h"
#include "ota/ota.h"
#ifdef CONFIG_M62_STATUS_LEDS
#include "dongle/status_led.h"
#endif
#include "portmacro.h"
#include "semphr.h"
#include "stream_buffer.h"
#include "task.h"
#include "usbd_core.h"
#include "usbd_cdc_acm.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define USB_OTA_BUS_ID 0U
#define USB_OTA_IN_EP 0x83U
#define USB_OTA_OUT_EP 0x04U
#define USB_OTA_RX_BUFFER_SIZE 2048U
#define USB_OTA_RX_STREAM_SIZE 8192U
#define USB_OTA_PACKET_CAPACITY \
    (DONGLE_USB_OTA_HEADER_SIZE + DONGLE_USB_OTA_MAX_PAYLOAD_SIZE)
#define USB_OTA_BEGIN_PAYLOAD_SIZE (4U + sizeof(ota_header_t))
#define USB_OTA_HEADER_SIZE ((uint32_t)sizeof(ota_header_t))
#define USB_OTA_IDLE_POLL_MS 10U
#define USB_OTA_FRAME_TIMEOUT_MS 5000U
#define USB_OTA_SESSION_TIMEOUT_MS 60000U
#define USB_OTA_TX_TIMEOUT_MS 3000U
#define USB_OTA_TASK_STACK_WORDS 2048U
#define USB_OTA_TASK_PRIORITY (tskIDLE_PRIORITY + 2U)

extern int bl_sys_reset_por(void);

typedef enum {
    FRAME_READ_IDLE,
    FRAME_READ_VALID,
    FRAME_READ_INVALID,
    FRAME_READ_TIMEOUT
} frame_read_result_t;

typedef struct {
    ota_handle_t volatile handle;
    bool over_hid;
    uint32_t image_size;
    uint32_t expected_offset;
    uint32_t begin_checksum;
    uint32_t last_data_offset;
    uint32_t last_data_checksum;
    uint16_t last_data_size;
    TickType_t last_activity;
} ota_session_t;

static ota_session_t session;
bool dongle_usb_ota_active(void) { return session.handle != NULL; }

static StreamBufferHandle_t usb_rx_stream;
static StaticStreamBuffer_t usb_rx_stream_storage;
static uint8_t usb_rx_stream_data[USB_OTA_RX_STREAM_SIZE];
static SemaphoreHandle_t usb_tx_complete;
static StaticSemaphore_t usb_tx_complete_storage;
static TaskHandle_t usb_ota_task_handle;
static SemaphoreHandle_t device_catalog_mutex;
static dongle_device_catalog_t *device_catalog;
static dongle_settings_store_t *device_settings_store;
static dongle_prefs_t saved_prefs;
static dongle_device_blocklist_t device_blocklist;
static volatile bool usb_configured;
static volatile bool usb_rx_armed;
static volatile bool usb_rx_overflow;
static volatile bool usb_rx_rearm_error;
/* Only the protocol task changes response routing. */
static bool response_over_hid;

static bool load_prefs(void)
{
    (void)dongle_prefs_init_defaults(&saved_prefs);
    uint8_t blob[DONGLE_PREFS_BLOB_SIZE] = { 0 };
    size_t saved = 0U;
    if (ef_get_env_blob("dongle-prefs", blob, sizeof(blob), &saved) == sizeof(blob) &&
        saved == sizeof(blob)) {
        dongle_prefs_t decoded;
        if (dongle_prefs_decode(blob, sizeof(blob), &decoded) == DONGLE_PREFS_OK) {
            saved_prefs = decoded;
            return true;
        }
    }
    return false;
}

static bool save_prefs(void)
{
    uint8_t blob[DONGLE_PREFS_BLOB_SIZE] = { 0 };
    if (dongle_prefs_encode(&saved_prefs, blob, sizeof(blob)) != DONGLE_PREFS_OK) {
        return false;
    }
    return ef_set_env_blob("dongle-prefs", blob, sizeof(blob)) == EF_NO_ERR;
}

static void load_blocklist(void)
{
    (void)dongle_blocklist_init(&device_blocklist);
    uint8_t blob[DONGLE_BLOCKLIST_BLOB_SIZE] = { 0 };
    size_t saved = 0U;
    if (ef_get_env_blob("dongle-blocklist", blob, sizeof(blob), &saved) == sizeof(blob) &&
        saved == sizeof(blob)) {
        dongle_device_blocklist_t decoded;
        if (dongle_blocklist_decode(blob, sizeof(blob), &decoded) == DONGLE_BLOCKLIST_OK) {
            device_blocklist = decoded;
        }
    }
}

static bool save_blocklist(void)
{
    uint8_t blob[DONGLE_BLOCKLIST_BLOB_SIZE] = { 0 };
    if (dongle_blocklist_encode(&device_blocklist, blob, sizeof(blob)) != DONGLE_BLOCKLIST_OK) {
        return false;
    }
    return ef_set_env_blob("dongle-blocklist", blob, sizeof(blob)) == EF_NO_ERR;
}

static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX
    uint8_t usb_rx_buffer[USB_OTA_RX_BUFFER_SIZE];
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX
    uint8_t usb_tx_buffer[DONGLE_USB_OTA_HEADER_SIZE +
                          DONGLE_USB_OTA_MAX_PAYLOAD_SIZE];

#ifdef CONFIG_USB_HS
static const uint8_t usb_ota_device_qualifier_descriptor[] = {
    USB_DEVICE_QUALIFIER_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, 0x01)
};
#endif

static const char *usb_ota_string_descriptors[] = {
    (const char[]){ 0x09, 0x04 },
    "Uni USB Dongle",
    "Bluetooth Gamepads + Stereo Audio",
    "00000001",
};

static const uint8_t *usb_ota_device_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return dongle_usb_profile_device();
}

static const uint8_t *usb_ota_config_descriptor_callback(uint8_t speed)
{
    return dongle_usb_profile_config(speed, false);
}

static const char *usb_ota_string_descriptor_callback(uint8_t speed,
                                                       uint8_t index)
{
    (void)speed;
    if (index >= sizeof(usb_ota_string_descriptors) /
                     sizeof(usb_ota_string_descriptors[0])) {
        return NULL;
    }
    if (dongle_usb_profile() == DONGLE_PROFILE_DUALSENSE) {
        if (index == 1U) {
            return "Sony Interactive Entertainment";
        }
        if (index == 2U) {
            return "DualSense Wireless Controller";
        }
    }
    return usb_ota_string_descriptors[index];
}

#ifdef CONFIG_USB_HS
static const uint8_t *usb_ota_device_qualifier_descriptor_callback(
    uint8_t speed)
{
    (void)speed;
    return usb_ota_device_qualifier_descriptor;
}

static const uint8_t *usb_ota_other_speed_descriptor_callback(uint8_t speed)
{
    return dongle_usb_profile_config(speed, true);
}
#endif

static struct usb_descriptor usb_ota_descriptor = {
    .device_descriptor_callback = usb_ota_device_descriptor_callback,
    .config_descriptor_callback = usb_ota_config_descriptor_callback,
#ifdef CONFIG_USB_HS
    .device_quality_descriptor_callback =
        usb_ota_device_qualifier_descriptor_callback,
    .other_speed_descriptor_callback = usb_ota_other_speed_descriptor_callback,
#endif
    .string_descriptor_callback = usb_ota_string_descriptor_callback,
};

static void notify_ota_task(void)
{
    if (usb_ota_task_handle == NULL) {
        return;
    }
    if (xPortIsInsideInterrupt()) {
        BaseType_t higher_priority_task_woken = pdFALSE;
        vTaskNotifyGiveFromISR(usb_ota_task_handle, &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    } else {
        xTaskNotifyGive(usb_ota_task_handle);
    }
}

static void usb_ota_event_handler(uint8_t busid, uint8_t event)
{
    if (busid != USB_OTA_BUS_ID) {
        return;
    }

    dongle_usb_gamepad_event(event);
    dongle_usb_audio_event(event);

    switch (event) {
        case USBD_EVENT_CONFIGURED:
        case USBD_EVENT_RESUME:
            usb_configured = true;
            notify_ota_task();
            break;
        case USBD_EVENT_RESET:
        case USBD_EVENT_DISCONNECTED:
        case USBD_EVENT_SUSPEND:
        case USBD_EVENT_DEINIT:
            usb_configured = false;
            usb_rx_armed = false;
            notify_ota_task();
            break;
        default:
            break;
    }
}

static int attach_usb_stack(bool first_boot)
{
    usb_ota_descriptor.msosv1_descriptor =
        dongle_usb_profile() == DONGLE_PROFILE_XINPUT
            ? dongle_usb_profile_msos()
            : NULL;
    usbd_desc_register(USB_OTA_BUS_ID, &usb_ota_descriptor);
    if (first_boot) {
        if (dongle_usb_audio_register(USB_OTA_BUS_ID) != 0) {
            return -1;
        }
        if (dongle_usb_gamepad_register(USB_OTA_BUS_ID) != pdPASS) {
            return -1;
        }
    } else {
        dongle_usb_audio_attach(USB_OTA_BUS_ID);
        dongle_usb_gamepad_attach(USB_OTA_BUS_ID);
    }
    return usbd_initialize(USB_OTA_BUS_ID, 0U, usb_ota_event_handler);
}

static uint16_t read_u16_le(const uint8_t *buffer)
{
    return (uint16_t)((uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8U));
}

static uint32_t read_u32_le(const uint8_t *buffer)
{
    return (uint32_t)buffer[0] |
           ((uint32_t)buffer[1] << 8U) |
           ((uint32_t)buffer[2] << 16U) |
           ((uint32_t)buffer[3] << 24U);
}

static void write_u32_le(uint8_t *buffer, uint32_t value)
{
    buffer[0] = (uint8_t)value;
    buffer[1] = (uint8_t)(value >> 8U);
    buffer[2] = (uint8_t)(value >> 16U);
    buffer[3] = (uint8_t)(value >> 24U);
}

static bool usb_read_byte(uint32_t timeout_ms, uint8_t *byte)
{
    return xStreamBufferReceive(usb_rx_stream, byte, 1U,
                                pdMS_TO_TICKS(timeout_ms)) == 1U;
}

static bool usb_read_exact(uint8_t *buffer, size_t size, uint32_t timeout_ms)
{
    for (size_t i = 0; i < size; ++i) {
        if (!usb_read_byte(timeout_ms, &buffer[i])) {
            return false;
        }
    }
    return true;
}

static frame_read_result_t read_frame(
    uint8_t *sync_match,
    uint8_t *packet,
    dongle_usb_ota_frame_t *frame)
{
    static const uint8_t magic[4] = { 'U', 'O', 'T', 'A' };

    while (*sync_match < sizeof(magic)) {
        uint8_t byte;
        if (!usb_read_byte(USB_OTA_IDLE_POLL_MS, &byte)) {
            return FRAME_READ_IDLE;
        }

        if (byte == magic[*sync_match]) {
            ++*sync_match;
        } else {
            *sync_match = byte == magic[0] ? 1U : 0U;
        }
    }

    memcpy(packet, magic, sizeof(magic));
    *sync_match = 0U;
    if (!usb_read_exact(&packet[sizeof(magic)],
                        DONGLE_USB_OTA_HEADER_SIZE - sizeof(magic),
                        USB_OTA_FRAME_TIMEOUT_MS)) {
        return FRAME_READ_TIMEOUT;
    }

    const uint16_t payload_size = read_u16_le(&packet[10]);
    if (payload_size > DONGLE_USB_OTA_MAX_PAYLOAD_SIZE) {
        return FRAME_READ_INVALID;
    }
    if (!usb_read_exact(&packet[DONGLE_USB_OTA_HEADER_SIZE], payload_size,
                        USB_OTA_FRAME_TIMEOUT_MS)) {
        return FRAME_READ_TIMEOUT;
    }

    return dongle_usb_ota_decode(
               packet, DONGLE_USB_OTA_HEADER_SIZE + payload_size, frame) ==
                   DONGLE_USB_OTA_PROTOCOL_OK
               ? FRAME_READ_VALID
               : FRAME_READ_INVALID;
}

static uint32_t packet_checksum(const uint8_t *packet)
{
    return read_u32_le(&packet[12]);
}

static bool usb_write(const uint8_t *buffer, size_t size)
{
    while (xSemaphoreTake(usb_tx_complete, 0U) == pdTRUE) {
    }

    if (!usb_configured ||
        usbd_ep_start_write(USB_OTA_BUS_ID, USB_OTA_IN_EP, buffer,
                            (uint32_t)size) != 0) {
        return false;
    }
    return xSemaphoreTake(usb_tx_complete,
                          pdMS_TO_TICKS(USB_OTA_TX_TIMEOUT_MS)) == pdTRUE;
}

static bool send_response_payload(
    const dongle_usb_ota_frame_t *request,
    const uint8_t *payload,
    uint16_t payload_size)
{
    size_t packet_size;

    if (dongle_usb_ota_encode(
            usb_tx_buffer, sizeof(usb_tx_buffer),
            request->command | DONGLE_USB_OTA_RESPONSE_FLAG,
            request->sequence, payload, payload_size, &packet_size) !=
        DONGLE_USB_OTA_PROTOCOL_OK) {
        return false;
    }

    return response_over_hid
        ? dongle_usb_hid_reply(usb_tx_buffer, packet_size)
        : usb_write(usb_tx_buffer, packet_size);
}

static bool send_response(
    const dongle_usb_ota_frame_t *request,
    dongle_usb_ota_status_t status,
    uint32_t expected_offset)
{
    uint8_t payload[5];
    payload[0] = (uint8_t)status;
    write_u32_le(&payload[1], expected_offset);
    return send_response_payload(request, payload, sizeof(payload));
}

static void handle_device_state(const dongle_usb_ota_frame_t *request)
{
    uint8_t payload[DONGLE_DEVICE_STATE_PAYLOAD_SIZE] = { 0 };
    dongle_device_catalog_t snapshot;
    if (request->payload_size != 0U) {
        payload[0] = DONGLE_USB_OTA_STATUS_INVALID_FRAME;
        (void)send_response_payload(request, payload, 1U);
        return;
    }
    if (device_catalog_mutex == NULL ||
        xSemaphoreTake(device_catalog_mutex, portMAX_DELAY) != pdTRUE) {
        payload[0] = DONGLE_USB_OTA_STATUS_BUSY;
        (void)send_response_payload(request, payload, 1U);
        return;
    }

    snapshot = *device_catalog;
    (void)xSemaphoreGive(device_catalog_mutex);

    payload[0] = DONGLE_USB_OTA_STATUS_OK;
    payload[1] = DONGLE_DEVICE_CAPABILITY_USB_GAMEPAD |
        (dongle_bluetooth_ready() ? DONGLE_DEVICE_CAPABILITY_BT_HID | DONGLE_DEVICE_CAPABILITY_AUDIO : 0);
    uint8_t connected = dongle_bluetooth_connected();
    payload[2] = snapshot.selected_slot;
    size_t offset = 3U;
    for (uint8_t slot = 0U; slot < DONGLE_CONTROLLER_SLOT_COUNT; ++slot) {
        const dongle_controller_slot_t *controller =
            &snapshot.controllers[slot];
        payload[offset++] = controller->paired ? 1U : 0U;
        payload[offset++] = (connected >> slot) & 1U;
        payload[offset++] = controller->usb_gamepad_index;
        memcpy(&payload[offset], controller->address,
               DONGLE_BLUETOOTH_ADDRESS_SIZE);
        offset += DONGLE_BLUETOOTH_ADDRESS_SIZE;
    }
    (void)send_response_payload(request, payload, sizeof(payload));
}

static dongle_usb_ota_status_t mutate_device_catalog(
    uint8_t command,
    uint8_t slot)
{
    if (device_catalog_mutex == NULL ||
        xSemaphoreTake(device_catalog_mutex, portMAX_DELAY) != pdTRUE) {
        return DONGLE_USB_OTA_STATUS_BUSY;
    }

    const dongle_device_catalog_t before = *device_catalog;
    dongle_catalog_result_t catalog_result;
    uint8_t removed[6] = {0};
    if (slot < 2) memcpy(removed, before.controllers[slot].address, 6);
    if (command == DONGLE_USB_OTA_CMD_SELECT_CONTROLLER) {
        catalog_result = dongle_catalog_select_controller(device_catalog, slot);
    } else {
        catalog_result = dongle_catalog_remove_controller(device_catalog, slot);
    }

    dongle_usb_ota_status_t status = DONGLE_USB_OTA_STATUS_OK;
    if (catalog_result == DONGLE_CATALOG_SLOT_EMPTY) {
        status = DONGLE_USB_OTA_STATUS_NOT_FOUND;
    } else if (catalog_result != DONGLE_CATALOG_OK) {
        status = DONGLE_USB_OTA_STATUS_INVALID_FRAME;
    } else if (device_settings_store == NULL ||
               !device_settings_store->initialized ||
               dongle_settings_store_save(device_settings_store,
                                          device_catalog) != DONGLE_STORE_OK) {
        *device_catalog = before;
        status = DONGLE_USB_OTA_STATUS_STORAGE_ERROR;
    }

    (void)xSemaphoreGive(device_catalog_mutex);
    if (status == DONGLE_USB_OTA_STATUS_OK && command == DONGLE_USB_OTA_CMD_REMOVE_CONTROLLER)
        dongle_bluetooth_removed(removed);
    return status;
}

static void handle_device_command(const dongle_usb_ota_frame_t *request)
{
    uint8_t payload[2] = { 0U, DONGLE_NO_SELECTED_SLOT };
    if (request->command == 0x13 || request->command == 0x14 ||
        (request->command >= 0x17 && request->command <= 0x1a) ||
        request->command == 0x1c || request->command == 0x1d) {
        uint8_t reply[44] = {0};
        size_t n = dongle_bluetooth_command(request->command, request->payload,
                                           request->payload_size, reply);
        (void)send_response_payload(request, reply, n);
        return;
    }
    if (request->payload_size != 1U) {
        payload[0] = DONGLE_USB_OTA_STATUS_INVALID_FRAME;
        (void)send_response_payload(request, payload, 1U);
        return;
    }

    payload[0] = (uint8_t)mutate_device_catalog(request->command,
                                                request->payload[0]);
    if (device_catalog_mutex != NULL &&
        xSemaphoreTake(device_catalog_mutex, portMAX_DELAY) == pdTRUE) {
        payload[1] = device_catalog->selected_slot;
        (void)xSemaphoreGive(device_catalog_mutex);
    }
    (void)send_response_payload(request, payload, sizeof(payload));
}

static void set_ota_indicator(bool in_progress)
{
#ifdef CONFIG_M62_STATUS_LEDS
    if (dongle_status_led_set_ota(in_progress) != pdPASS) {
        LOG_E("Unable to update OTA status LED\r\n");
    }
#else
    (void)in_progress;
#endif
}

static void abort_session(ota_session_t *session)
{
    if (session->handle != NULL) {
        if (ota_abort(session->handle) != 0) {
            LOG_E("USB OTA abort failed\r\n");
        }
    }
    memset(session, 0, sizeof(*session));
    set_ota_indicator(false);
}

static bool package_header_is_valid(
    const uint8_t *payload,
    uint16_t payload_size,
    uint32_t *image_size,
    uint32_t *body_size)
{
    if (payload_size != USB_OTA_BEGIN_PAYLOAD_SIZE) {
        return false;
    }

    *image_size = read_u32_le(payload);
    const uint8_t *header = &payload[4];
    if (memcmp(header, "BL60X_OTA", 9U) != 0 ||
        memcmp(&header[16], "XZ", 2U) != 0) {
        return false;
    }

    *body_size = read_u32_le(&header[20]);
    return *body_size > 0U &&
           *image_size == USB_OTA_HEADER_SIZE + *body_size;
}

static dongle_usb_ota_status_t begin_update(
    ota_session_t *session,
    const dongle_usb_ota_frame_t *frame,
    uint32_t checksum)
{
    if (session->handle != NULL) {
        return frame->sequence == 0U && checksum == session->begin_checksum
                   ? DONGLE_USB_OTA_STATUS_OK
                   : DONGLE_USB_OTA_STATUS_BUSY;
    }
    if (frame->sequence != 0U) {
        return DONGLE_USB_OTA_STATUS_BAD_OFFSET;
    }

    uint32_t image_size;
    uint32_t body_size;
    if (!package_header_is_valid(frame->payload, frame->payload_size,
                                 &image_size, &body_size)) {
        return DONGLE_USB_OTA_STATUS_INVALID_IMAGE;
    }

    ota_handle_t handle = ota_start();
    if (handle == NULL) {
        return DONGLE_USB_OTA_STATUS_UPDATE_FAILED;
    }
    if (body_size > handle->part_size ||
        ota_update(handle, (uint8_t *)&frame->payload[4],
                   USB_OTA_HEADER_SIZE) != 0) {
        if (ota_abort(handle) != 0) {
            LOG_E("USB OTA setup abort failed\r\n");
        }
        set_ota_indicator(false);
        return DONGLE_USB_OTA_STATUS_INVALID_IMAGE;
    }

    session->handle = handle;
    session->over_hid = response_over_hid;
    session->image_size = image_size;
    session->expected_offset = USB_OTA_HEADER_SIZE;
    session->begin_checksum = checksum;
    session->last_activity = xTaskGetTickCount();
    set_ota_indicator(true);
    return DONGLE_USB_OTA_STATUS_OK;
}

static dongle_usb_ota_status_t write_data(
    ota_session_t *session,
    const dongle_usb_ota_frame_t *frame,
    uint32_t checksum)
{
    if (session->handle == NULL) {
        return DONGLE_USB_OTA_STATUS_INVALID_COMMAND;
    }
    if (frame->payload_size == 0U) {
        return DONGLE_USB_OTA_STATUS_INVALID_FRAME;
    }

    if (frame->sequence == session->last_data_offset &&
        frame->payload_size == session->last_data_size &&
        checksum == session->last_data_checksum &&
        frame->sequence + frame->payload_size == session->expected_offset) {
        session->last_activity = xTaskGetTickCount();
        return DONGLE_USB_OTA_STATUS_OK;
    }
    if (frame->sequence != session->expected_offset ||
        frame->sequence > session->image_size ||
        frame->payload_size > session->image_size - frame->sequence) {
        return DONGLE_USB_OTA_STATUS_BAD_OFFSET;
    }

    if (ota_update(session->handle, (uint8_t *)frame->payload,
                   frame->payload_size) != 0) {
        abort_session(session);
        return DONGLE_USB_OTA_STATUS_UPDATE_FAILED;
    }

    session->last_data_offset = frame->sequence;
    session->last_data_size = frame->payload_size;
    session->last_data_checksum = checksum;
    session->expected_offset += frame->payload_size;
    session->last_activity = xTaskGetTickCount();
    return DONGLE_USB_OTA_STATUS_OK;
}

static void finish_update(
    ota_session_t *session,
    const dongle_usb_ota_frame_t *frame)
{
    if (session->handle == NULL) {
        (void)send_response(frame, DONGLE_USB_OTA_STATUS_INVALID_COMMAND, 0U);
        return;
    }
    if (frame->payload_size != 0U ||
        frame->sequence != session->image_size ||
        session->expected_offset != session->image_size) {
        (void)send_response(frame, DONGLE_USB_OTA_STATUS_INCOMPLETE,
                            session->expected_offset);
        return;
    }

    ota_handle_t handle = session->handle;
    if (ota_finish(handle, 1U, 0U) != 0) {
        session->handle = NULL;
        if (ota_abort(handle) != 0) {
            LOG_E("USB OTA finish abort failed\r\n");
        }
        memset(session, 0, sizeof(*session));
        set_ota_indicator(false);
        (void)send_response(frame, DONGLE_USB_OTA_STATUS_UPDATE_FAILED, 0U);
        return;
    }

    const uint32_t completed_size = session->image_size;
    session->handle = NULL;
    memset(session, 0, sizeof(*session));
    set_ota_indicator(false);
    if (!send_response(frame, DONGLE_USB_OTA_STATUS_OK, completed_size)) {
        LOG_E("USB OTA completion response write failed\r\n");
    }

    if (response_over_hid) {
        const TickType_t start = xTaskGetTickCount();
        while (!dongle_usb_hid_reply_seen() &&
               (TickType_t)(xTaskGetTickCount() - start) <
                   pdMS_TO_TICKS(USB_OTA_TX_TIMEOUT_MS)) {
            vTaskDelay(pdMS_TO_TICKS(1U));
        }
    }
    vTaskDelay(pdMS_TO_TICKS(100U));
    bl_sys_reset_por();
    for (;;) {
        vTaskDelay(portMAX_DELAY);
    }
}

static void usb_ota_task(void *argument)
{
    (void)argument;
    uint8_t packet[USB_OTA_PACKET_CAPACITY];
    uint8_t sync_match = 0U;
    dongle_usb_ota_frame_t frame;

    for (;;) {
        if (session.handle == NULL) {
            (void)dongle_usb_profile_set_connected_controllers(
                dongle_bluetooth_connected());
        }
        if (session.handle == NULL && dongle_usb_profile_pending()) {
            const TickType_t wait_start = xTaskGetTickCount();
            while (!dongle_usb_hid_reply_seen() &&
                   (TickType_t)(xTaskGetTickCount() - wait_start) <
                       pdMS_TO_TICKS(300U)) {
                vTaskDelay(pdMS_TO_TICKS(1U));
            }
            dongle_usb_profile_activate();
            (void)usbd_deinitialize(USB_OTA_BUS_ID);
            usb_configured = false;
            usb_rx_armed = false;
            vTaskDelay(pdMS_TO_TICKS(250U));
            const int attach_err = attach_usb_stack(false);
            dongle_usb_profile_complete(attach_err == 0);
        }
        if (usb_rx_overflow) {
            usb_rx_overflow = false;
            usb_rx_armed = false;
            (void)xStreamBufferReset(usb_rx_stream);
            abort_session(&session);
            sync_match = 0U;
            LOG_E("USB OTA receive buffer overflow\r\n");
        }
        if (usb_rx_rearm_error) {
            usb_rx_rearm_error = false;
            LOG_E("USB OTA receive endpoint rearm failed\r\n");
        }
        if (!usb_configured && session.handle != NULL) {
            abort_session(&session);
            (void)xStreamBufferReset(usb_rx_stream);
            sync_match = 0U;
        }
        if (false && usb_configured && !usb_rx_armed) { /* Audio profile uses HID management only. */
            if (usbd_ep_start_read(USB_OTA_BUS_ID, USB_OTA_OUT_EP,
                                   usb_rx_buffer,
                                   sizeof(usb_rx_buffer)) == 0) {
                usb_rx_armed = true;
            } else {
                LOG_E("USB OTA could not arm receive endpoint\r\n");
                vTaskDelay(pdMS_TO_TICKS(USB_OTA_IDLE_POLL_MS));
                continue;
            }
        }

        const size_t hid_size = dongle_usb_hid_take(packet, sizeof(packet));
        frame_read_result_t result;
        if (hid_size != 0) {
            response_over_hid = true;
            result = dongle_usb_ota_decode(packet, hid_size, &frame) ==
                DONGLE_USB_OTA_PROTOCOL_OK ? FRAME_READ_VALID : FRAME_READ_INVALID;
        } else {
            response_over_hid = false;
            result = read_frame(&sync_match, packet, &frame);
        }
        if (result == FRAME_READ_IDLE) {
            if (session.handle != NULL &&
                (TickType_t)(xTaskGetTickCount() - session.last_activity) >=
                    pdMS_TO_TICKS(USB_OTA_SESSION_TIMEOUT_MS)) {
                abort_session(&session);
            }
            continue;
        }
        if (result != FRAME_READ_VALID) {
            continue;
        }

        const uint32_t checksum = packet_checksum(packet);
        dongle_usb_ota_status_t status;
        if (session.handle != NULL && session.over_hid != response_over_hid &&
            frame.command != DONGLE_USB_OTA_CMD_GET_DEVICE_STATE &&
            frame.command != DONGLE_USB_OTA_CMD_GET_GAMEPAD_DIAGNOSTICS &&
            frame.command != DONGLE_USB_OTA_CMD_GET_PROFILE_STATE &&
            frame.command != DONGLE_USB_OTA_CMD_GET_AUDIO_METRICS &&
            frame.command != DONGLE_USB_OTA_CMD_GET_DUALSENSE_METRICS) {
            (void)send_response(&frame, DONGLE_USB_OTA_STATUS_BUSY,
                                session.expected_offset);
            continue;
        }
        if (session.handle != NULL &&
            (frame.command == DONGLE_USB_OTA_CMD_SELECT_CONTROLLER ||
             frame.command == DONGLE_USB_OTA_CMD_REMOVE_CONTROLLER ||
             frame.command == DONGLE_USB_OTA_CMD_START_DISCOVERY ||
             frame.command == DONGLE_USB_OTA_CMD_PAIR_CONTROLLER ||
             frame.command == DONGLE_USB_OTA_CMD_PAIR_AUDIO ||
             frame.command == DONGLE_USB_OTA_CMD_DISCONNECT_PEER ||
             frame.command == DONGLE_USB_OTA_CMD_SET_PROFILE ||
             frame.command == DONGLE_USB_OTA_CMD_SET_MIC_SOURCE ||
             frame.command == DONGLE_USB_OTA_CMD_REMOVE_AUDIO ||
             frame.command == DONGLE_USB_OTA_CMD_BLOCK_DEVICE ||
             frame.command == DONGLE_USB_OTA_CMD_UNBLOCK_DEVICE)) {
            (void)send_response(&frame, DONGLE_USB_OTA_STATUS_BUSY, session.expected_offset);
            continue;
        }
        if (frame.command == DONGLE_USB_OTA_CMD_GET_DEVICE_STATE) {
            handle_device_state(&frame);
            continue;
        } else if (frame.command == DONGLE_USB_OTA_CMD_GET_PROFILE_STATE) {
            uint8_t reply[DONGLE_PROFILE_STATE_PAYLOAD_SIZE] = { 0 };
            if (frame.payload_size != 0U) {
                reply[0] = DONGLE_USB_OTA_STATUS_INVALID_FRAME;
                (void)send_response_payload(&frame, reply, 1U);
            } else {
                const size_t n = dongle_usb_profile_state(reply);
                (void)send_response_payload(&frame, reply, (uint16_t)n);
            }
            continue;
        } else if (frame.command == DONGLE_USB_OTA_CMD_SET_PROFILE) {
            uint8_t reply[DONGLE_PROFILE_STATE_PAYLOAD_SIZE] = { 0 };
            if (session.handle != NULL) {
                reply[0] = DONGLE_USB_OTA_STATUS_BUSY;
                (void)send_response_payload(&frame, reply, 1U);
            } else if (frame.payload_size < 2U || frame.payload_size > 4U) {
                reply[0] = DONGLE_USB_OTA_STATUS_INVALID_FRAME;
                (void)send_response_payload(&frame, reply, 1U);
            } else {
                const bool persist = frame.payload_size >= 3U &&
                                     (frame.payload[2] & 0x01U) != 0U;
                const uint8_t ds_poll = frame.payload_size == 4U
                    ? frame.payload[3] : 0xffU;
                const uint8_t rc = dongle_usb_profile_request(
                    frame.payload[0], frame.payload[1], persist, ds_poll);
                if (rc != DONGLE_USB_OTA_STATUS_OK) {
                    reply[0] = rc;
                    (void)send_response_payload(&frame, reply, 1U);
                } else {
                    if (persist && device_catalog_mutex != NULL &&
                        xSemaphoreTake(device_catalog_mutex, portMAX_DELAY) == pdTRUE) {
                        saved_prefs.boot_profile = frame.payload[0];
                        saved_prefs.audio_codec = frame.payload[1];
                        if (ds_poll != 0xffU) {
                            saved_prefs.dualsense_poll_rate = ds_poll;
                        }
                        (void)save_prefs();
                        (void)xSemaphoreGive(device_catalog_mutex);
                    }
                    const size_t n = dongle_usb_profile_state(reply);
                    (void)send_response_payload(&frame, reply, (uint16_t)n);
                }
            }
            continue;
        } else if (frame.command == DONGLE_USB_OTA_CMD_GET_AUDIO_METRICS) {
            uint8_t reply[DONGLE_AUDIO_METRICS_PAYLOAD_SIZE] = { 0 };
            if (frame.payload_size != 0U) {
                reply[0] = DONGLE_USB_OTA_STATUS_INVALID_FRAME;
                (void)send_response_payload(&frame, reply, 1U);
            } else {
                dongle_bluetooth_audio_metrics(reply);
                (void)send_response_payload(&frame, reply, sizeof(reply));
            }
            continue;
        } else if (frame.command == DONGLE_USB_OTA_CMD_GET_DUALSENSE_METRICS) {
            uint8_t reply[DONGLE_DUALSENSE_METRICS_PAYLOAD_SIZE] = { 0 };
            if (frame.payload_size != 1U ||
                !dongle_bluetooth_dualsense_metrics(frame.payload[0], reply)) {
                reply[0] = DONGLE_USB_OTA_STATUS_INVALID_FRAME;
                (void)send_response_payload(&frame, reply, 1U);
            } else {
                (void)send_response_payload(&frame, reply, sizeof(reply));
            }
            continue;
        } else if (frame.command == DONGLE_USB_OTA_CMD_GET_MIC_CONFIG) {
            uint8_t reply[DONGLE_MIC_STATE_PAYLOAD_SIZE] = { 0 };
            if (frame.payload_size != 0U) {
                reply[0] = DONGLE_USB_OTA_STATUS_INVALID_FRAME;
                (void)send_response_payload(&frame, reply, 1U);
            } else {
                const size_t n = dongle_mic_pipeline_metrics(reply);
                (void)send_response_payload(&frame, reply, (uint16_t)n);
            }
            continue;
        } else if (frame.command == DONGLE_USB_OTA_CMD_SET_MIC_SOURCE) {
            uint8_t reply[DONGLE_MIC_STATE_PAYLOAD_SIZE] = { 0 };
            if (session.handle != NULL) {
                reply[0] = DONGLE_USB_OTA_STATUS_BUSY;
                (void)send_response_payload(&frame, reply, 1U);
            } else if (frame.payload_size != 1U ||
                       !dongle_mic_pipeline_set_source(frame.payload[0])) {
                reply[0] = DONGLE_USB_OTA_STATUS_INVALID_FRAME;
                (void)send_response_payload(&frame, reply, 1U);
            } else {
                if (device_catalog_mutex != NULL &&
                    xSemaphoreTake(device_catalog_mutex, portMAX_DELAY) == pdTRUE) {
                    const uint8_t prev = saved_prefs.mic_source;
                    saved_prefs.mic_source = frame.payload[0];
                    if (!save_prefs()) {
                        saved_prefs.mic_source = prev;
                        (void)dongle_mic_pipeline_set_source(prev);
                        reply[0] = DONGLE_USB_OTA_STATUS_STORAGE_ERROR;
                        (void)xSemaphoreGive(device_catalog_mutex);
                        (void)send_response_payload(&frame, reply, 1U);
                        continue;
                    }
                    (void)xSemaphoreGive(device_catalog_mutex);
                }
                const size_t n = dongle_mic_pipeline_metrics(reply);
                (void)send_response_payload(&frame, reply, (uint16_t)n);
            }
            continue;
        } else if (frame.command == DONGLE_USB_OTA_CMD_REMOVE_AUDIO) {
            uint8_t rc = DONGLE_USB_OTA_STATUS_OK;
            if (session.handle != NULL) {
                rc = DONGLE_USB_OTA_STATUS_BUSY;
            } else if (frame.payload_size != 0U) {
                rc = DONGLE_USB_OTA_STATUS_INVALID_FRAME;
            } else {
                rc = dongle_bluetooth_remove_audio();
            }
            (void)send_response_payload(&frame, &rc, 1U);
            continue;
        } else if (frame.command == DONGLE_USB_OTA_CMD_GET_BLOCKLIST) {
            uint8_t reply[DONGLE_BLOCKLIST_MAX_PAYLOAD_SIZE] = { 0 };
            if (frame.payload_size != 0U) {
                reply[0] = DONGLE_USB_OTA_STATUS_INVALID_FRAME;
                (void)send_response_payload(&frame, reply, 1U);
            } else if (device_catalog_mutex == NULL ||
                       xSemaphoreTake(device_catalog_mutex, portMAX_DELAY) != pdTRUE) {
                reply[0] = DONGLE_USB_OTA_STATUS_BUSY;
                (void)send_response_payload(&frame, reply, 1U);
            } else {
                const uint8_t count = device_blocklist.count <= DONGLE_BLOCKLIST_MAX_ENTRIES
                    ? device_blocklist.count : DONGLE_BLOCKLIST_MAX_ENTRIES;
                reply[0] = DONGLE_USB_OTA_STATUS_OK;
                reply[1] = count;
                reply[2] = DONGLE_BLUETOOTH_ADDRESS_SIZE;
                for (uint8_t i = 0U; i < count; ++i) {
                    memcpy(&reply[3U + i * 6U], device_blocklist.addresses[i], 6U);
                }
                (void)xSemaphoreGive(device_catalog_mutex);
                (void)send_response_payload(&frame, reply, (uint16_t)(3U + count * 6U));
            }
            continue;
        } else if (frame.command == DONGLE_USB_OTA_CMD_BLOCK_DEVICE) {
            uint8_t reply[DONGLE_BLOCKLIST_MAX_PAYLOAD_SIZE] = { 0 };
            if (session.handle != NULL) {
                reply[0] = DONGLE_USB_OTA_STATUS_BUSY;
                (void)send_response_payload(&frame, reply, 1U);
                continue;
            }
            if (frame.payload_size != 6U ||
                !dongle_catalog_address_is_valid(frame.payload)) {
                reply[0] = DONGLE_USB_OTA_STATUS_INVALID_FRAME;
                (void)send_response_payload(&frame, reply, 1U);
                continue;
            }
            if (device_catalog_mutex == NULL ||
                xSemaphoreTake(device_catalog_mutex, portMAX_DELAY) != pdTRUE) {
                reply[0] = DONGLE_USB_OTA_STATUS_BUSY;
                (void)send_response_payload(&frame, reply, 1U);
                continue;
            }
            const dongle_device_blocklist_t before_bl = device_blocklist;
            const dongle_blocklist_result_t bl_res =
                dongle_blocklist_add(&device_blocklist, frame.payload);
            uint8_t status_out = DONGLE_USB_OTA_STATUS_OK;
            if (bl_res == DONGLE_BLOCKLIST_FULL) {
                status_out = DONGLE_USB_OTA_STATUS_UPDATE_FAILED;
            } else if (bl_res == DONGLE_BLOCKLIST_OK) {
                if (!save_blocklist()) {
                    device_blocklist = before_bl;
                    status_out = DONGLE_USB_OTA_STATUS_STORAGE_ERROR;
                }
            }
            if (status_out == DONGLE_USB_OTA_STATUS_OK && device_catalog != NULL) {
                bool catalog_changed = false;
                const dongle_device_catalog_t before_cat = *device_catalog;
                for (uint8_t slot = 0U; slot < 2U; ++slot) {
                    if (device_catalog->controllers[slot].paired &&
                        memcmp(device_catalog->controllers[slot].address,
                               frame.payload, 6U) == 0) {
                        if (dongle_catalog_remove_controller(device_catalog, slot) ==
                            DONGLE_CATALOG_OK) {
                            catalog_changed = true;
                        }
                    }
                }
                if (catalog_changed &&
                    (device_settings_store == NULL || !device_settings_store->initialized ||
                     dongle_settings_store_save(device_settings_store,
                                                device_catalog) != DONGLE_STORE_OK)) {
                    *device_catalog = before_cat;
                }
            }
            const uint8_t count = device_blocklist.count <= DONGLE_BLOCKLIST_MAX_ENTRIES
                ? device_blocklist.count : DONGLE_BLOCKLIST_MAX_ENTRIES;
            reply[0] = status_out;
            if (status_out == DONGLE_USB_OTA_STATUS_OK) {
                reply[1] = count;
                reply[2] = DONGLE_BLUETOOTH_ADDRESS_SIZE;
                for (uint8_t i = 0U; i < count; ++i) {
                    memcpy(&reply[3U + i * 6U], device_blocklist.addresses[i], 6U);
                }
            }
            (void)xSemaphoreGive(device_catalog_mutex);
            if (status_out == DONGLE_USB_OTA_STATUS_OK) {
                uint8_t radio_rsp[44] = { 0 };
                (void)dongle_bluetooth_command(0x18U, NULL, 0U, radio_rsp);
                if (memcmp(&radio_rsp[7], frame.payload, 6U) == 0) {
                    (void)dongle_bluetooth_remove_audio();
                }
                dongle_bluetooth_disconnect_address(frame.payload);
                (void)send_response_payload(&frame, reply, (uint16_t)(3U + count * 6U));
            } else {
                (void)send_response_payload(&frame, reply, 1U);
            }
            continue;
        } else if (frame.command == DONGLE_USB_OTA_CMD_UNBLOCK_DEVICE) {
            uint8_t reply[DONGLE_BLOCKLIST_MAX_PAYLOAD_SIZE] = { 0 };
            if (session.handle != NULL) {
                reply[0] = DONGLE_USB_OTA_STATUS_BUSY;
                (void)send_response_payload(&frame, reply, 1U);
                continue;
            }
            if (frame.payload_size != 6U ||
                !dongle_catalog_address_is_valid(frame.payload)) {
                reply[0] = DONGLE_USB_OTA_STATUS_INVALID_FRAME;
                (void)send_response_payload(&frame, reply, 1U);
                continue;
            }
            if (device_catalog_mutex == NULL ||
                xSemaphoreTake(device_catalog_mutex, portMAX_DELAY) != pdTRUE) {
                reply[0] = DONGLE_USB_OTA_STATUS_BUSY;
                (void)send_response_payload(&frame, reply, 1U);
                continue;
            }
            const dongle_device_blocklist_t before_bl = device_blocklist;
            const dongle_blocklist_result_t bl_res =
                dongle_blocklist_remove(&device_blocklist, frame.payload);
            uint8_t status_out = DONGLE_USB_OTA_STATUS_OK;
            if (bl_res == DONGLE_BLOCKLIST_NOT_FOUND) {
                status_out = DONGLE_USB_OTA_STATUS_NOT_FOUND;
            } else if (bl_res != DONGLE_BLOCKLIST_OK) {
                status_out = DONGLE_USB_OTA_STATUS_INVALID_FRAME;
            } else if (!save_blocklist()) {
                device_blocklist = before_bl;
                status_out = DONGLE_USB_OTA_STATUS_STORAGE_ERROR;
            }
            const uint8_t count = device_blocklist.count <= DONGLE_BLOCKLIST_MAX_ENTRIES
                ? device_blocklist.count : DONGLE_BLOCKLIST_MAX_ENTRIES;
            reply[0] = status_out;
            if (status_out == DONGLE_USB_OTA_STATUS_OK) {
                reply[1] = count;
                reply[2] = DONGLE_BLUETOOTH_ADDRESS_SIZE;
                for (uint8_t i = 0U; i < count; ++i) {
                    memcpy(&reply[3U + i * 6U], device_blocklist.addresses[i], 6U);
                }
            }
            (void)xSemaphoreGive(device_catalog_mutex);
            (void)send_response_payload(
                &frame, reply, status_out == DONGLE_USB_OTA_STATUS_OK
                                   ? (uint16_t)(3U + count * 6U) : 1U);
            continue;
        } else if (frame.command == DONGLE_USB_OTA_CMD_SELECT_CONTROLLER ||
                   frame.command == DONGLE_USB_OTA_CMD_REMOVE_CONTROLLER ||
                   frame.command == DONGLE_USB_OTA_CMD_START_DISCOVERY ||
                   frame.command == DONGLE_USB_OTA_CMD_PAIR_CONTROLLER ||
                   (frame.command >= 0x17 && frame.command <= 0x1a) ||
                   frame.command == 0x1c || frame.command == 0x1d) {
            handle_device_command(&frame);
            continue;
        } else if (frame.command == DONGLE_USB_OTA_CMD_GET_GAMEPAD_DIAGNOSTICS) {
            uint8_t payload[1U + DONGLE_USB_GAMEPAD_DIAGNOSTIC_SIZE] = { 0 };
            if (frame.payload_size != 1U ||
                !dongle_usb_gamepad_diagnostics(frame.payload[0], &payload[1])) {
                payload[0] = DONGLE_USB_OTA_STATUS_INVALID_FRAME;
                (void)send_response_payload(&frame, payload, 1U);
            } else {
                (void)send_response_payload(&frame, payload, sizeof(payload));
            }
            continue;
        } else if (frame.command == DONGLE_USB_OTA_CMD_TEST_GAMEPAD) {
            if (session.handle != NULL) {
                status = DONGLE_USB_OTA_STATUS_BUSY;
            } else if (frame.payload_size != 1U + DONGLE_USB_GAMEPAD_REPORT_SIZE ||
                       frame.payload[0] >= DONGLE_USB_GAMEPAD_COUNT ||
                       frame.payload[9] > 8U) {
                status = DONGLE_USB_OTA_STATUS_INVALID_FRAME;
            } else {
                status = dongle_usb_gamepad_test(frame.payload[0],
                    &frame.payload[1], DONGLE_USB_GAMEPAD_REPORT_SIZE)
                    ? DONGLE_USB_OTA_STATUS_OK : DONGLE_USB_OTA_STATUS_BUSY;
            }
        } else if (frame.command == DONGLE_USB_OTA_CMD_BEGIN) {
            status = begin_update(&session, &frame, checksum);
        } else if (frame.command == DONGLE_USB_OTA_CMD_DATA) {
            status = write_data(&session, &frame, checksum);
        } else if (frame.command == DONGLE_USB_OTA_CMD_ABORT) {
            if (frame.payload_size != 0U) {
                status = DONGLE_USB_OTA_STATUS_INVALID_FRAME;
            } else {
                abort_session(&session);
                status = DONGLE_USB_OTA_STATUS_OK;
            }
        } else if (frame.command == DONGLE_USB_OTA_CMD_FINISH) {
            finish_update(&session, &frame);
            continue;
        } else {
            status = DONGLE_USB_OTA_STATUS_INVALID_COMMAND;
        }

        const uint32_t next_offset =
            session.handle != NULL ? session.expected_offset : 0U;
        if (!send_response(&frame, status, next_offset)) {
            LOG_E("USB OTA response write failed\r\n");
        }
    }
}

BaseType_t dongle_usb_ota_service_start(
    dongle_device_catalog_t *catalog,
    dongle_settings_store_t *settings_store)
{
    if (catalog == NULL || usb_ota_task_handle != NULL) {
        return pdFAIL;
    }

    device_catalog_mutex = xSemaphoreCreateMutex();
    if (device_catalog_mutex == NULL) {
        return pdFAIL;
    }
    device_catalog = catalog;
    device_settings_store = settings_store;

    usb_rx_stream = xStreamBufferCreateStatic(
        sizeof(usb_rx_stream_data), 1U, usb_rx_stream_data,
        &usb_rx_stream_storage);
    usb_tx_complete = xSemaphoreCreateBinaryStatic(&usb_tx_complete_storage);
    if (usb_rx_stream == NULL || usb_tx_complete == NULL) {
        vSemaphoreDelete(device_catalog_mutex);
        device_catalog_mutex = NULL;
        device_catalog = NULL;
        device_settings_store = NULL;
        return pdFAIL;
    }

    dongle_bluetooth_storage_bind(settings_store);
    const bool has_saved_prefs = load_prefs();
    load_blocklist();
    dongle_usb_profile_init(
        has_saved_prefs ? saved_prefs.boot_profile : 0xffU,
        saved_prefs.audio_codec,
        saved_prefs.dualsense_poll_rate);
    dongle_bluetooth_bind_blocklist(&device_blocklist);

    int radio_error = dongle_bluetooth_start(catalog, settings_store, device_catalog_mutex);
    LOG_I("Bluetooth startup result: %d\r\n", radio_error);
    dongle_mic_pipeline_init(saved_prefs.mic_source);
    if (attach_usb_stack(true) != 0) {
        vSemaphoreDelete(device_catalog_mutex);
        device_catalog_mutex = NULL;
        device_catalog = NULL;
        device_settings_store = NULL;
        return pdFAIL;
    }

    if (xTaskCreate(usb_ota_task, "usb_ota", USB_OTA_TASK_STACK_WORDS, NULL,
                    USB_OTA_TASK_PRIORITY, &usb_ota_task_handle) != pdPASS) {
        usbd_deinitialize(USB_OTA_BUS_ID);
        vSemaphoreDelete(device_catalog_mutex);
        device_catalog_mutex = NULL;
        device_catalog = NULL;
        device_settings_store = NULL;
        return pdFAIL;
    }
    return pdPASS;
}
