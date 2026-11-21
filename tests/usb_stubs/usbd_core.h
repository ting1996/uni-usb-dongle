#ifndef TEST_USBD_CORE_H
#define TEST_USBD_CORE_H
#include <stdint.h>
#define USB_NOCACHE_RAM_SECTION
#define USB_MEM_ALIGNX _Alignas(64)
enum { USBD_EVENT_CONFIGURED, USBD_EVENT_RESUME, USBD_EVENT_SUSPEND,
       USBD_EVENT_RESET, USBD_EVENT_DISCONNECTED, USBD_EVENT_DEINIT };
struct usbd_interface { int unused; };
struct usbd_endpoint {
    uint8_t ep_addr;
    void (*ep_cb)(uint8_t, uint8_t, uint32_t);
};
void usbd_add_interface(uint8_t bus, struct usbd_interface *intf);
void usbd_add_endpoint(uint8_t bus, struct usbd_endpoint *ep);
int usbd_ep_start_write(uint8_t bus, uint8_t ep, const uint8_t *data,
                        uint32_t length);
int usbd_ep_start_read(uint8_t bus, uint8_t ep, uint8_t *data,
                       uint32_t length);
#endif
