#include "dongle/dualsense_haptics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static void write_s16_le(uint8_t *p, int16_t v)
{
    p[0] = (uint8_t)((uint16_t)v & 0xFFU);
    p[1] = (uint8_t)(((uint16_t)v >> 8U) & 0xFFU);
}

static void fill_4ch_packet(uint8_t *buf, size_t frames,
                            int16_t fl, int16_t fr, int16_t hl, int16_t hr)
{
    for (size_t i = 0U; i < frames; ++i) {
        write_s16_le(&buf[i * 8U + 0U], fl);
        write_s16_le(&buf[i * 8U + 2U], fr);
        write_s16_le(&buf[i * 8U + 4U], hl);
        write_s16_le(&buf[i * 8U + 6U], hr);
    }
}

int main(void)
{
    dongle_ds_haptics_t st;
    dongle_ds_haptics_init(&st);

    uint8_t pcm4[48U * 8U];
    uint8_t stereo[48U * 4U];
    size_t stereo_len = 0U;
    uint16_t peak_l = 0U, peak_r = 0U;
    uint8_t motor_l = 0U, motor_r = 0U;
    bool changed = false;

    /* Invalid arguments and non-8-byte-aligned sizes are rejected cleanly. */
    CHECK(!dongle_ds_haptics_process_4ch(NULL, pcm4, sizeof(pcm4), stereo, &stereo_len,
                                         &peak_l, &peak_r, &motor_l, &motor_r, &changed));
    CHECK(!dongle_ds_haptics_process_4ch(&st, NULL, sizeof(pcm4), stereo, &stereo_len,
                                         &peak_l, &peak_r, &motor_l, &motor_r, &changed));
    CHECK(!dongle_ds_haptics_process_4ch(&st, pcm4, 0U, stereo, &stereo_len,
                                         &peak_l, &peak_r, &motor_l, &motor_r, &changed));
    CHECK(!dongle_ds_haptics_process_4ch(&st, pcm4, 12U, stereo, &stereo_len,
                                         &peak_l, &peak_r, &motor_l, &motor_r, &changed));

    /* 1. 4ch demux: Ch0/Ch1 copied intact to stereo_out, Ch2/Ch3 below noise floor -> (0,0). */
    fill_4ch_packet(pcm4, 48U, 1234, -5678, 100, -100);
    CHECK(dongle_ds_haptics_process_4ch(&st, pcm4, sizeof(pcm4), stereo, &stereo_len,
                                        &peak_l, &peak_r, &motor_l, &motor_r, &changed));
    CHECK(stereo_len == 48U * 4U);
    for (size_t i = 0U; i < 48U; ++i) {
        CHECK(memcmp(&stereo[i * 4U], &pcm4[i * 8U], 4U) == 0);
    }
    CHECK(peak_l == 1234U && peak_r == 5678U);
    CHECK(motor_l == 0U && motor_r == 0U && !changed);
    CHECK(!dongle_ds_haptics_reset(&st));

    /* 2. Left/right channel separation: Ch2 drives motor_l only, Ch3 stays 0. */
    fill_4ch_packet(pcm4, 48U, 0, 0, 12000, 0);
    CHECK(dongle_ds_haptics_process_4ch(&st, pcm4, sizeof(pcm4), stereo, &stereo_len,
                                        &peak_l, &peak_r, &motor_l, &motor_r, &changed));
    CHECK(motor_l >= DONGLE_DS_HAPTICS_MIN_MOTOR && motor_r == 0U && changed);
    CHECK(dongle_ds_haptics_reset(&st));

    fill_4ch_packet(pcm4, 48U, 0, 0, 0, -12000);
    CHECK(dongle_ds_haptics_process_4ch(&st, pcm4, sizeof(pcm4), stereo, &stereo_len,
                                        &peak_l, &peak_r, &motor_l, &motor_r, &changed));
    CHECK(motor_l == 0U && motor_r >= DONGLE_DS_HAPTICS_MIN_MOTOR && changed);
    CHECK(dongle_ds_haptics_reset(&st));

    /* 3. Strong haptic wave attacks to saturation, then silence holds off and stops cleanly. */
    fill_4ch_packet(pcm4, 48U, 100, -100, 30000, -30000);
    for (unsigned ms = 0U; ms < 10U; ++ms) {
        CHECK(dongle_ds_haptics_process_4ch(&st, pcm4, sizeof(pcm4), stereo, &stereo_len,
                                            &peak_l, &peak_r, &motor_l, &motor_r, &changed));
    }
    CHECK(motor_l == 255U && motor_r == 255U);

    fill_4ch_packet(pcm4, 48U, 0, 0, 0, 0);
    bool stopped = false;
    for (unsigned ms = 0U; ms < 120U; ++ms) {
        CHECK(dongle_ds_haptics_process_4ch(&st, pcm4, sizeof(pcm4), NULL, &stereo_len,
                                            NULL, NULL, &motor_l, &motor_r, &changed));
        if (changed && motor_l == 0U && motor_r == 0U) {
            stopped = true;
            break;
        }
    }
    CHECK(stopped);
    CHECK(!dongle_ds_haptics_reset(&st));

    /* 4. Rumble gain / loss: 0% off, 50% loss, 200%/400% gain, and reset persistence. */
    dongle_ds_haptics_set_gain(&st, 0U);
    CHECK(dongle_ds_haptics_get_gain(&st) == 0U);
    fill_4ch_packet(pcm4, 48U, 0, 0, 4000, 4000);
    CHECK(dongle_ds_haptics_process_4ch(&st, pcm4, sizeof(pcm4), NULL, NULL,
                                        NULL, NULL, &motor_l, &motor_r, &changed));
    CHECK(motor_l == 0U && motor_r == 0U && !changed);
    CHECK(!dongle_ds_haptics_reset(&st));
    CHECK(dongle_ds_haptics_get_gain(&st) == 0U);

    dongle_ds_haptics_set_gain(&st, 50U);
    fill_4ch_packet(pcm4, 48U, 0, 0, 4000, 4000);
    CHECK(dongle_ds_haptics_process_4ch(&st, pcm4, sizeof(pcm4), NULL, NULL,
                                        NULL, NULL, &motor_l, &motor_r, &changed));
    const uint8_t loss_motor = motor_l;
    CHECK(loss_motor >= DONGLE_DS_HAPTICS_MIN_MOTOR && changed);
    CHECK(dongle_ds_haptics_reset(&st));

    dongle_ds_haptics_set_gain(&st, 200U);
    fill_4ch_packet(pcm4, 48U, 0, 0, 4000, 4000);
    CHECK(dongle_ds_haptics_process_4ch(&st, pcm4, sizeof(pcm4), NULL, NULL,
                                        NULL, NULL, &motor_l, &motor_r, &changed));
    CHECK(motor_l > loss_motor && changed);
    CHECK(dongle_ds_haptics_reset(&st));

    dongle_ds_haptics_set_gain(&st, 999U);
    CHECK(dongle_ds_haptics_get_gain(&st) == DONGLE_DS_HAPTICS_GAIN_MAX);
    CHECK(!dongle_ds_haptics_reset(&st));

    puts("DualSense 4ch audio haptics demux and envelope validated");
    return 0;
}
