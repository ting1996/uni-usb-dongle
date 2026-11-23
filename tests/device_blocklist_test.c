#include "dongle/device_blocklist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            fprintf(stderr, "Check failed at line %d: %s\n", __LINE__, #expression); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

int main(void)
{
    dongle_device_blocklist_t list;
    const uint8_t zero[6] = { 0 };
    const uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    const uint8_t macs[7][6] = {
        { 0x11, 0x22, 0x33, 0x44, 0x55, 0x01 },
        { 0x11, 0x22, 0x33, 0x44, 0x55, 0x02 },
        { 0x11, 0x22, 0x33, 0x44, 0x55, 0x03 },
        { 0x11, 0x22, 0x33, 0x44, 0x55, 0x04 },
        { 0x11, 0x22, 0x33, 0x44, 0x55, 0x05 },
        { 0x11, 0x22, 0x33, 0x44, 0x55, 0x06 },
        { 0x11, 0x22, 0x33, 0x44, 0x55, 0x07 },
    };

    CHECK(dongle_blocklist_init(NULL) == DONGLE_BLOCKLIST_INVALID_ARGUMENT);
    CHECK(dongle_blocklist_init(&list) == DONGLE_BLOCKLIST_OK);
    CHECK(list.count == 0U);
    CHECK(!dongle_blocklist_contains(&list, macs[0]));

    CHECK(dongle_blocklist_add(&list, zero) == DONGLE_BLOCKLIST_INVALID_ADDRESS);
    CHECK(dongle_blocklist_add(&list, bcast) == DONGLE_BLOCKLIST_INVALID_ADDRESS);

    for (unsigned i = 0U; i < DONGLE_BLOCKLIST_MAX_ENTRIES; ++i) {
        CHECK(dongle_blocklist_add(&list, macs[i]) == DONGLE_BLOCKLIST_OK);
        CHECK(dongle_blocklist_contains(&list, macs[i]));
    }
    CHECK(list.count == DONGLE_BLOCKLIST_MAX_ENTRIES);
    CHECK(dongle_blocklist_add(&list, macs[0]) == DONGLE_BLOCKLIST_DUPLICATE);
    CHECK(dongle_blocklist_add(&list, macs[6]) == DONGLE_BLOCKLIST_FULL);

    /* Remove middle item and verify compaction + trailing zeroing. */
    CHECK(dongle_blocklist_remove(&list, macs[2]) == DONGLE_BLOCKLIST_OK);
    CHECK(list.count == 5U);
    CHECK(!dongle_blocklist_contains(&list, macs[2]));
    CHECK(dongle_blocklist_contains(&list, macs[5]));
    CHECK(dongle_blocklist_remove(&list, macs[2]) == DONGLE_BLOCKLIST_NOT_FOUND);
    CHECK(memcmp(list.addresses[5], zero, 6U) == 0);

    uint8_t blob[DONGLE_BLOCKLIST_BLOB_SIZE];
    CHECK(dongle_blocklist_encode(&list, blob, sizeof(blob) - 1U) ==
          DONGLE_BLOCKLIST_INVALID_ARGUMENT);
    CHECK(dongle_blocklist_encode(&list, blob, sizeof(blob)) == DONGLE_BLOCKLIST_OK);

    dongle_device_blocklist_t decoded;
    CHECK(dongle_blocklist_init(&decoded) == DONGLE_BLOCKLIST_OK);
    CHECK(dongle_blocklist_decode(blob, sizeof(blob), &decoded) == DONGLE_BLOCKLIST_OK);
    CHECK(decoded.count == list.count);
    CHECK(memcmp(&decoded, &list, sizeof(list)) == 0);

    /* Corrupt byte -> rejected without mutating decoded state. */
    blob[10] ^= 0x01U;
    CHECK(dongle_blocklist_decode(blob, sizeof(blob), &decoded) ==
          DONGLE_BLOCKLIST_INVALID_DATA);
    CHECK(decoded.count == list.count);
    CHECK(memcmp(&decoded, &list, sizeof(list)) == 0);

    puts("Device blocklist tests passed");
    return 0;
}
