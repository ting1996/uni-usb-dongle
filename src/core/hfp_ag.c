#include "dongle/hfp_ag.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HFP_AG_FEATURES 0x0A00U /* Bit 9: Codec Negotiation | Bit 11: eSCO S4 T2 */

static void emit_str(dongle_hfp_ag_tx_fn tx_fn, void *context, const char *text)
{
    if (tx_fn != NULL && text != NULL) {
        tx_fn(context, (const uint8_t *)text, strlen(text));
    }
}

static bool starts_with_ci(const char *str, const char *prefix)
{
    while (*prefix != '\0') {
        if (toupper((unsigned char)*str) != toupper((unsigned char)*prefix)) {
            return false;
        }
        ++str;
        ++prefix;
    }
    return true;
}

static uint32_t parse_uint(const char *s, bool *ok)
{
    if (s == NULL || !isdigit((unsigned char)*s)) {
        if (ok != NULL) {
            *ok = false;
        }
        return 0U;
    }
    uint32_t val = 0U;
    while (isdigit((unsigned char)*s)) {
        val = val * 10U + (uint32_t)(*s - '0');
        ++s;
    }
    if (ok != NULL) {
        *ok = true;
    }
    return val;
}

static uint8_t parse_codec_list(const char *s)
{
    uint8_t mask = 0U;
    while (*s != '\0') {
        while (*s != '\0' && !isdigit((unsigned char)*s)) {
            ++s;
        }
        if (*s == '\0') {
            break;
        }
        bool ok = false;
        const uint32_t id = parse_uint(s, &ok);
        if (ok) {
            if (id == 1U) {
                mask |= 0x01U;
            } else if (id == 2U) {
                mask |= 0x02U;
            }
        }
        while (isdigit((unsigned char)*s)) {
            ++s;
        }
    }
    return mask != 0U ? mask : 0x01U;
}

static dongle_hfp_ag_action_t handle_line(
    dongle_hfp_ag_t *ag,
    const char *line,
    dongle_hfp_ag_tx_fn tx_fn,
    void *context)
{
    while (*line == ' ' || *line == '\t') {
        ++line;
    }
    if (*line == '\0') {
        return DONGLE_HFP_AG_ACTION_NONE;
    }

    if (starts_with_ci(line, "AT+BRSF=")) {
        bool ok = false;
        const uint32_t feats = parse_uint(line + 8, &ok);
        if (ok) {
            ag->hf_features = (uint16_t)feats;
        }
        char reply[32];
        (void)snprintf(reply, sizeof(reply), "\r\n+BRSF: %u\r\n\r\nOK\r\n",
                       (unsigned)HFP_AG_FEATURES);
        emit_str(tx_fn, context, reply);
        return DONGLE_HFP_AG_ACTION_NONE;
    }

    if (starts_with_ci(line, "AT+BAC=")) {
        ag->hf_codec_mask = parse_codec_list(line + 7);
        emit_str(tx_fn, context, "\r\nOK\r\n");
        return DONGLE_HFP_AG_ACTION_NONE;
    }

    if (starts_with_ci(line, "AT+CIND=?")) {
        emit_str(tx_fn, context,
                 "\r\n+CIND: (\"service\",(0,1)),(\"call\",(0,1)),"
                 "(\"callsetup\",(0-3)),(\"callheld\",(0-2)),"
                 "(\"signal\",(0-5)),(\"roam\",(0,1)),(\"battchg\",(0-5))\r\n"
                 "\r\nOK\r\n");
        return DONGLE_HFP_AG_ACTION_NONE;
    }

    if (starts_with_ci(line, "AT+CIND?")) {
        emit_str(tx_fn, context, "\r\n+CIND: 1,0,0,0,5,0,5\r\n\r\nOK\r\n");
        return DONGLE_HFP_AG_ACTION_NONE;
    }

    if (starts_with_ci(line, "AT+CMER=")) {
        const bool newly_ready = !ag->slc_ready;
        ag->slc_ready = true;
        emit_str(tx_fn, context, "\r\nOK\r\n");
        return newly_ready ? DONGLE_HFP_AG_ACTION_SLC_READY
                           : DONGLE_HFP_AG_ACTION_NONE;
    }

    if (starts_with_ci(line, "AT+CHLD=?")) {
        emit_str(tx_fn, context, "\r\n+CHLD: (0,1,2)\r\n\r\nOK\r\n");
        return DONGLE_HFP_AG_ACTION_NONE;
    }

    if (starts_with_ci(line, "AT+BCS=")) {
        bool ok = false;
        const uint32_t codec = parse_uint(line + 7, &ok);
        ag->bcs_pending = false;
        if (!ok || (codec != 1U && codec != 2U)) {
            emit_str(tx_fn, context, "\r\nERROR\r\n");
            return DONGLE_HFP_AG_ACTION_NONE;
        }
        ag->selected_codec = (uint8_t)codec;
        emit_str(tx_fn, context, "\r\nOK\r\n");
        return codec == 2U ? DONGLE_HFP_AG_ACTION_OPEN_SCO_MSBC
                           : DONGLE_HFP_AG_ACTION_OPEN_SCO_CVSD;
    }

    if (starts_with_ci(line, "AT+BCC")) {
        emit_str(tx_fn, context, "\r\nOK\r\n");
        return dongle_hfp_ag_request_codec(ag, tx_fn, context);
    }

    if (starts_with_ci(line, "AT+VGM=")) {
        bool ok = false;
        const uint32_t vol = parse_uint(line + 7, &ok);
        if (ok && vol <= 15U) {
            ag->hf_vgm = (uint8_t)vol;
        }
        emit_str(tx_fn, context, "\r\nOK\r\n");
        return DONGLE_HFP_AG_ACTION_NONE;
    }

    if (starts_with_ci(line, "AT+VGS=")) {
        bool ok = false;
        const uint32_t vol = parse_uint(line + 7, &ok);
        if (ok && vol <= 15U) {
            ag->hf_vgs = (uint8_t)vol;
        }
        emit_str(tx_fn, context, "\r\nOK\r\n");
        return DONGLE_HFP_AG_ACTION_NONE;
    }

    if (starts_with_ci(line, "AT")) {
        emit_str(tx_fn, context, "\r\nOK\r\n");
        return DONGLE_HFP_AG_ACTION_NONE;
    }

    emit_str(tx_fn, context, "\r\nERROR\r\n");
    return DONGLE_HFP_AG_ACTION_NONE;
}

void dongle_hfp_ag_init(dongle_hfp_ag_t *ag)
{
    if (ag == NULL) {
        return;
    }
    memset(ag, 0, sizeof(*ag));
    ag->hf_codec_mask = 0x01U; /* CVSD baseline */
    ag->hf_vgm = 15U;
    ag->hf_vgs = 15U;
}

dongle_hfp_ag_action_t dongle_hfp_ag_feed(
    dongle_hfp_ag_t *ag,
    const uint8_t *data,
    size_t size,
    dongle_hfp_ag_tx_fn tx_fn,
    void *context)
{
    if (ag == NULL || (size > 0U && data == NULL)) {
        return DONGLE_HFP_AG_ACTION_NONE;
    }

    dongle_hfp_ag_action_t result = DONGLE_HFP_AG_ACTION_NONE;
    for (size_t i = 0U; i < size; ++i) {
        const char ch = (char)data[i];
        if (ch == '\r' || ch == '\n') {
            if (!ag->line_overflow && ag->rx_len > 0U) {
                ag->rx_line[ag->rx_len] = '\0';
                const dongle_hfp_ag_action_t act =
                    handle_line(ag, ag->rx_line, tx_fn, context);
                if (act != DONGLE_HFP_AG_ACTION_NONE) {
                    result = act;
                }
            }
            ag->rx_len = 0U;
            ag->line_overflow = false;
            continue;
        }
        if (ag->line_overflow) {
            continue;
        }
        if (ag->rx_len + 1U >= sizeof(ag->rx_line)) {
            ag->line_overflow = true;
            ag->rx_len = 0U;
            emit_str(tx_fn, context, "\r\nERROR\r\n");
            continue;
        }
        ag->rx_line[ag->rx_len++] = ch;
    }
    return result;
}

dongle_hfp_ag_action_t dongle_hfp_ag_request_codec(
    dongle_hfp_ag_t *ag,
    dongle_hfp_ag_tx_fn tx_fn,
    void *context)
{
    if (ag == NULL || !ag->slc_ready) {
        return DONGLE_HFP_AG_ACTION_NONE;
    }
    if ((ag->hf_features & 0x0080U) != 0U) {
        const uint8_t codec = (ag->hf_codec_mask & 0x02U) != 0U ? 2U : 1U;
        char msg[24];
        ag->bcs_pending = true;
        (void)snprintf(msg, sizeof(msg), "\r\n+BCS: %u\r\n", (unsigned)codec);
        emit_str(tx_fn, context, msg);
        return DONGLE_HFP_AG_ACTION_NONE;
    }
    ag->selected_codec = 1U;
    return DONGLE_HFP_AG_ACTION_OPEN_SCO_CVSD;
}
