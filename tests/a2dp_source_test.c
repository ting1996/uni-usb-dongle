#include "dongle/a2dp_source.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "Check failed at line %d: %s\n", __LINE__, #expr); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

typedef struct {
    uint8_t last_tx[64];
    size_t last_tx_len;
    unsigned tx_count;
    bool open_media_called;
    bool close_media_called;
    dongle_a2dp_stream_state_t last_state;
    uint8_t active_codec;
    uint32_t sample_rate_hz;
} test_harness_t;

static int mock_send_signal(void *user, const uint8_t *data, size_t len)
{
    test_harness_t *h = (test_harness_t *)user;
    memcpy(h->last_tx, data, len);
    h->last_tx_len = len;
    ++h->tx_count;
    return 0;
}

static int mock_open_media(void *user)
{
    ((test_harness_t *)user)->open_media_called = true;
    return 0;
}

static void mock_close_media(void *user)
{
    ((test_harness_t *)user)->close_media_called = true;
}

static void mock_on_state(void *user, dongle_a2dp_stream_state_t state,
                          uint8_t active_codec, uint32_t sample_rate_hz)
{
    test_harness_t *h = (test_harness_t *)user;
    h->last_state = state;
    h->active_codec = active_codec;
    h->sample_rate_hz = sample_rate_hz;
}

static void feed_accept(dongle_a2dp_source_t *ctx, test_harness_t *h,
                        uint8_t sig_id, const uint8_t *payload, size_t len)
{
    uint8_t buf[64];
    const uint8_t label = (uint8_t)(h->last_tx[0] >> 4U);
    buf[0] = (uint8_t)((label << 4U) | 0x02U);
    buf[1] = sig_id;
    if (len > 0U && payload != NULL) {
        memcpy(&buf[2], payload, len);
    }
    dongle_a2dp_source_recv_signal(ctx, buf, 2U + len);
}

static void test_auto_negotiation_selects_ldac_96k(void)
{
    test_harness_t h;
    memset(&h, 0, sizeof(h));
    const dongle_a2dp_source_callbacks_t cb = {
        .send_signal = mock_send_signal,
        .open_media = mock_open_media,
        .close_media = mock_close_media,
        .on_state = mock_on_state,
    };
    dongle_a2dp_source_t ctx;
    dongle_a2dp_source_init(&ctx, &cb, &h);
    dongle_a2dp_source_set_preferences(&ctx, DONGLE_AUDIO_CODEC_AUTO, 96000U);

    CHECK(dongle_a2dp_source_connect(&ctx, true) == 0);
    CHECK(h.last_tx[1] == AVDTP_SIG_DISCOVER);

    /* Remote advertises SEID 1 (SBC), SEID 2 (AAC), SEID 3 (LDAC). */
    const uint8_t seps[] = { 0x04U, 0x08U, 0x08U, 0x08U, 0x0CU, 0x08U };
    feed_accept(&ctx, &h, AVDTP_SIG_DISCOVER, seps, sizeof(seps));

    CHECK(h.last_tx[1] == AVDTP_SIG_GET_ALL_CAPABILITIES);
    CHECK((h.last_tx[2] >> 2U) == 1U);
    const uint8_t sbc_caps[] = { 0x01U, 0x00U, 0x07U, 0x06U, 0x00U, 0x00U, 0x3FU, 0xFFU, 2U, 53U };
    feed_accept(&ctx, &h, AVDTP_SIG_GET_ALL_CAPABILITIES, sbc_caps, sizeof(sbc_caps));

    CHECK(h.last_tx[1] == AVDTP_SIG_GET_ALL_CAPABILITIES);
    CHECK((h.last_tx[2] >> 2U) == 2U);
    const uint8_t aac_caps[] = { 0x01U, 0x00U, 0x07U, 0x08U, 0x00U, 0x02U, 0x80U, 0x01U, 0x84U, 0x83U, 0xE8U, 0x00U };
    feed_accept(&ctx, &h, AVDTP_SIG_GET_ALL_CAPABILITIES, aac_caps, sizeof(aac_caps));

    CHECK(h.last_tx[1] == AVDTP_SIG_GET_ALL_CAPABILITIES);
    CHECK((h.last_tx[2] >> 2U) == 3U);
    const uint8_t ldac_caps[] = {
        0x01U, 0x00U, 0x07U, 0x0AU, 0x00U, 0xFFU, 0x2DU, 0x01U, 0x00U, 0x00U, 0xAAU, 0x00U, 0x34U, 0x03U
    };
    feed_accept(&ctx, &h, AVDTP_SIG_GET_ALL_CAPABILITIES, ldac_caps, sizeof(ldac_caps));

    /* Should configure LDAC (remote SEID 3, local SEID 1) at 96 kHz (0x04). */
    CHECK(h.last_tx[1] == AVDTP_SIG_SET_CONFIGURATION);
    CHECK((h.last_tx[2] >> 2U) == 3U);
    CHECK((h.last_tx[3] >> 2U) == 1U);
    CHECK(h.last_tx[16] == 0x04U);

    feed_accept(&ctx, &h, AVDTP_SIG_SET_CONFIGURATION, NULL, 0U);
    CHECK(h.last_tx[1] == AVDTP_SIG_OPEN);
    feed_accept(&ctx, &h, AVDTP_SIG_OPEN, NULL, 0U);
    CHECK(h.open_media_called);

    CHECK(dongle_a2dp_source_media_connected(&ctx) == 0);
    CHECK(h.last_tx[1] == AVDTP_SIG_START);
    feed_accept(&ctx, &h, AVDTP_SIG_START, NULL, 0U);
    CHECK(ctx.state == DONGLE_A2DP_STATE_STREAMING);
    CHECK(h.active_codec == DONGLE_AUDIO_CODEC_LDAC);
    CHECK(h.sample_rate_hz == 96000U);
}

static void test_label_zero_wrap_and_preferred_aac(void)
{
    test_harness_t h;
    memset(&h, 0, sizeof(h));
    const dongle_a2dp_source_callbacks_t cb = {
        .send_signal = mock_send_signal,
        .open_media = mock_open_media,
        .close_media = mock_close_media,
        .on_state = mock_on_state,
    };
    dongle_a2dp_source_t ctx;
    dongle_a2dp_source_init(&ctx, &cb, &h);
    ctx.tx_label = 15U;
    dongle_a2dp_source_set_preferences(&ctx, DONGLE_AUDIO_CODEC_AAC, 48000U);

    CHECK(dongle_a2dp_source_connect(&ctx, false) == 0);
    CHECK((h.last_tx[0] >> 4U) == 15U);
    const uint8_t seps[] = { 0x04U, 0x08U, 0x08U, 0x08U };
    feed_accept(&ctx, &h, AVDTP_SIG_DISCOVER, seps, sizeof(seps));
    /* Label wrapped to 0 cleanly! */
    CHECK((h.last_tx[0] >> 4U) == 0U);

    const uint8_t sbc_caps[] = { 0x01U, 0x00U, 0x07U, 0x06U, 0x00U, 0x00U, 0x3FU, 0xFFU, 2U, 53U };
    feed_accept(&ctx, &h, AVDTP_SIG_GET_ALL_CAPABILITIES, sbc_caps, sizeof(sbc_caps));
    const uint8_t aac_caps[] = { 0x01U, 0x00U, 0x07U, 0x08U, 0x00U, 0x02U, 0x80U, 0x01U, 0x84U, 0x83U, 0xE8U, 0x00U };
    feed_accept(&ctx, &h, AVDTP_SIG_GET_ALL_CAPABILITIES, aac_caps, sizeof(aac_caps));

    CHECK(h.last_tx[1] == AVDTP_SIG_SET_CONFIGURATION);
    CHECK(ctx.active_codec == DONGLE_AUDIO_CODEC_AAC);
    CHECK(ctx.active_sample_rate_hz == 48000U);
}

int main(void)
{
    test_auto_negotiation_selects_ldac_96k();
    test_label_zero_wrap_and_preferred_aac();
    puts("A2DP source tests passed");
    return 0;
}
