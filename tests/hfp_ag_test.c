#include "dongle/hfp_ag.h"

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

typedef struct {
    char buf[512];
    size_t len;
} tx_capture_t;

static void capture_tx(void *ctx, const uint8_t *data, size_t size)
{
    tx_capture_t *cap = (tx_capture_t *)ctx;
    CHECK(cap->len + size < sizeof(cap->buf));
    memcpy(cap->buf + cap->len, data, size);
    cap->len += size;
    cap->buf[cap->len] = '\0';
}

static void reset_cap(tx_capture_t *cap)
{
    cap->len = 0U;
    cap->buf[0] = '\0';
}

int main(void)
{
    dongle_hfp_ag_t ag;
    tx_capture_t cap;
    dongle_hfp_ag_init(&ag);
    reset_cap(&cap);

    /* Fragmented AT+BRSF across multiple RFCOMM chunks. */
    CHECK(dongle_hfp_ag_feed(&ag, (const uint8_t *)"AT+BR", 5U, capture_tx, &cap) ==
          DONGLE_HFP_AG_ACTION_NONE);
    CHECK(cap.len == 0U);
    CHECK(dongle_hfp_ag_feed(&ag, (const uint8_t *)"SF=1023\r", 8U, capture_tx, &cap) ==
          DONGLE_HFP_AG_ACTION_NONE);
    CHECK(strstr(cap.buf, "+BRSF: 2560\r\n") != NULL);
    CHECK(strstr(cap.buf, "\r\nOK\r\n") != NULL);
    CHECK(ag.hf_features == 1023U);

    reset_cap(&cap);
    CHECK(dongle_hfp_ag_feed(&ag, (const uint8_t *)"AT+BAC=1,2\r", 11U, capture_tx, &cap) ==
          DONGLE_HFP_AG_ACTION_NONE);
    CHECK(strcmp(cap.buf, "\r\nOK\r\n") == 0);
    CHECK((ag.hf_codec_mask & 0x03U) == 0x03U);

    reset_cap(&cap);
    CHECK(dongle_hfp_ag_feed(&ag, (const uint8_t *)"AT+CIND=?\r", 10U, capture_tx, &cap) ==
          DONGLE_HFP_AG_ACTION_NONE);
    CHECK(strstr(cap.buf, "+CIND: (\"service\"") != NULL);
    CHECK(strstr(cap.buf, "OK\r\n") != NULL);

    reset_cap(&cap);
    CHECK(dongle_hfp_ag_feed(&ag, (const uint8_t *)"AT+CIND?\r", 9U, capture_tx, &cap) ==
          DONGLE_HFP_AG_ACTION_NONE);
    CHECK(strstr(cap.buf, "+CIND: 1,0,0,0,5,0,5\r\n") != NULL);

    reset_cap(&cap);
    CHECK(dongle_hfp_ag_feed(&ag, (const uint8_t *)"AT+CMER=3,0,0,1\r", 16U, capture_tx, &cap) ==
          DONGLE_HFP_AG_ACTION_SLC_READY);
    CHECK(ag.slc_ready);
    CHECK(strcmp(cap.buf, "\r\nOK\r\n") == 0);

    /* Sony headset vendor AT commands should not stall SLC. */
    reset_cap(&cap);
    CHECK(dongle_hfp_ag_feed(&ag, (const uint8_t *)"AT+XAPL=054C-0CE6-0100,7\rAT+VGM=12\r",
                             35U, capture_tx, &cap) == DONGLE_HFP_AG_ACTION_NONE);
    CHECK(ag.hf_vgm == 12U);
    CHECK(strcmp(cap.buf, "\r\nOK\r\n\r\nOK\r\n") == 0);

    /* Trigger codec negotiation -> +BCS: 2 -> AT+BCS=2 -> OPEN_SCO_MSBC. */
    reset_cap(&cap);
    CHECK(dongle_hfp_ag_request_codec(&ag, capture_tx, &cap) == DONGLE_HFP_AG_ACTION_NONE);
    CHECK(strcmp(cap.buf, "\r\n+BCS: 2\r\n") == 0);
    CHECK(ag.bcs_pending);

    reset_cap(&cap);
    CHECK(dongle_hfp_ag_feed(&ag, (const uint8_t *)"AT+BCS=2\r", 9U, capture_tx, &cap) ==
          DONGLE_HFP_AG_ACTION_OPEN_SCO_MSBC);
    CHECK(strcmp(cap.buf, "\r\nOK\r\n") == 0);
    CHECK(!ag.bcs_pending);
    CHECK(ag.selected_codec == 2U);

    /* CVSD-only HF without codec negotiation opens SCO immediately. */
    dongle_hfp_ag_init(&ag);
    reset_cap(&cap);
    CHECK(dongle_hfp_ag_feed(&ag, (const uint8_t *)"AT+BRSF=0\rAT+CMER=3,0,0,1\r",
                             26U, capture_tx, &cap) == DONGLE_HFP_AG_ACTION_SLC_READY);
    reset_cap(&cap);
    CHECK(dongle_hfp_ag_request_codec(&ag, capture_tx, &cap) ==
          DONGLE_HFP_AG_ACTION_OPEN_SCO_CVSD);
    CHECK(ag.selected_codec == 1U);

    puts("HFP-AG AT parser tests passed");
    return 0;
}
