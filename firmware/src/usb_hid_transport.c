#include "dongle/usb_hid_transport.h"
#include "FreeRTOS.h"
#include "task.h"
#include "portmacro.h"

static dongle_hid_transport_t mailbox;

/* USB callbacks run in interrupt context in the current SDK configuration.
 * Task callers exclude the interrupt; EP0-thread configurations also lock.
 * This SDK's portSET_INTERRUPT_MASK_FROM_ISR macro is a no-op. */
static bool lock(void)
{
    const bool is_task = !xPortIsInsideInterrupt();
    if (is_task) { taskENTER_CRITICAL(); }
    return is_task;
}
static void unlock(bool is_task)
{
    if (is_task) { taskEXIT_CRITICAL(); }
}

void dongle_usb_hid_reset(void)
{
    const bool is_task = lock();
    dongle_hid_transport_reset(&mailbox);
    unlock(is_task);
}
void dongle_usb_hid_set_feature(const uint8_t *data, size_t size)
{
    const bool is_task = lock();
    (void)dongle_hid_transport_receive(&mailbox, data, size);
    unlock(is_task);
}
void dongle_usb_hid_get_feature(uint8_t *data)
{
    const bool is_task = lock();
    dongle_hid_transport_read(&mailbox, data);
    unlock(is_task);
}
size_t dongle_usb_hid_take(uint8_t *packet, size_t capacity)
{
    const bool is_task = lock();
    const size_t size = dongle_hid_transport_take(&mailbox, packet, capacity);
    unlock(is_task);
    return size;
}
bool dongle_usb_hid_reply(const uint8_t *packet, size_t size)
{
    const bool is_task = lock();
    const bool ok = dongle_hid_transport_reply(&mailbox, packet, size);
    unlock(is_task);
    return ok;
}
bool dongle_usb_hid_reply_seen(void)
{
    const bool is_task = lock();
    const bool seen = mailbox.reply_seen;
    unlock(is_task);
    return seen;
}
