#include <stdio.h>
#include <string.h>
#include "app_protocol.h"
#include "app_timestamp.h"

/* ------------------------------- CRC-8 -------------------------------- */

uint8_t Protocol_Crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = APP_CRC8_INIT;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ APP_CRC8_POLY)
                                : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

/* ----------------------------- Frame pack ------------------------------ */

static void pack_payload(uint8_t out[APP_FRAME_SIZE], const char *content, size_t len)
{
    memcpy(out, content, len);
    memset(out + len, ' ', APP_FRAME_PAYLOAD_SIZE - len);
    out[APP_FRAME_CRC_INDEX] = Protocol_Crc8(out, APP_FRAME_PAYLOAD_SIZE);
    out[APP_FRAME_LF_INDEX]  = APP_FRAME_LF_BYTE;
}

/*
 * `n`: icerigi uretan snprintf'in dondurdugu (kesilmemis) uzunluk. snprintf
 * tampona sigmayani sessizce keser; bu yuzden karar n uzerinden verilir.
 */
static bool finish_frame(uint8_t out[APP_FRAME_SIZE], const char *content, int n,
                         const char *type_tag)
{
    if (n >= 0 && (size_t)n <= APP_FRAME_PAYLOAD_SIZE) {
        pack_payload(out, content, (size_t)n);
        return true;
    }
    char err[APP_FRAME_PAYLOAD_SIZE + 1];
    int m = snprintf(err, sizeof(err), "ERR,ENCODE_OVERFLOW,%s,%d", type_tag, n);
    if (m < 0 || (size_t)m > APP_FRAME_PAYLOAD_SIZE) {
        m = 0;
    }
    pack_payload(out, err, (size_t)m);
    return false;
}

static const char *status_to_str(uint8_t s)
{
    switch ((RecordStatus_t)s) {
        case REC_STATUS_OK:      return "OK";
        case REC_STATUS_LATE:    return "LATE";
        case REC_STATUS_DROP:    return "DROP";
        case REC_STATUS_TX_ERR:  return "TX_ERR";
        case REC_STATUS_TIMEOUT: return "TIMEOUT";
        default:                 return "PENDING";
    }
}

/* Olculmediyse "-" yazar; tampon en az 11 bayt olmali. */
static const char *fmt_delta(char buf[12], const EventRecord_t *rec, uint8_t mask, uint32_t t)
{
    if ((rec->ts_mask & mask) == 0u) {
        return "-";
    }
    snprintf(buf, 12, "%lu", (unsigned long)Timestamp_DeltaToUs(rec->t0, t));
    return buf;
}

/* --------------------------- Mesaj olusturucular ------------------------ */

bool Protocol_BuildTEL(uint8_t out[APP_FRAME_SIZE],
                       uint32_t seq, uint8_t scenario_id, uint32_t t_abs_ms,
                       uint32_t period_ms, uint32_t load_iter, uint32_t load_us,
                       const RingCounters_t *c)
{
    char content[APP_FRAME_SIZE];
    int n = snprintf(content, sizeof(content),
                     "TEL,%lu,%u,%lu,%lu,%lu,%lu,%lu,%lu,%lu",
                     (unsigned long)seq, (unsigned)scenario_id, (unsigned long)t_abs_ms,
                     (unsigned long)period_ms, (unsigned long)load_iter, (unsigned long)load_us,
                     (unsigned long)(c->btn_queue_drop_count + c->tx_queue_drop_count),
                     (unsigned long)(c->tx_start_err_count + c->tx_timeout_count),
                     (unsigned long)c->ring_overflow_count);
    return finish_frame(out, content, n, "TEL");
}

bool Protocol_BuildBTN(uint8_t out[APP_FRAME_SIZE],
                       uint16_t event_id, uint8_t scenario_id, uint32_t t0_abs_ms,
                       uint32_t d1_us)
{
    char content[APP_FRAME_SIZE];
    int n = snprintf(content, sizeof(content), "BTN,%u,%u,%lu,%lu,PRESSED",
                     (unsigned)event_id, (unsigned)scenario_id, (unsigned long)t0_abs_ms,
                     (unsigned long)d1_us);
    return finish_frame(out, content, n, "BTN");
}

bool Protocol_BuildBOOT(uint8_t out[APP_FRAME_SIZE],
                        uint32_t sysclk_hz, bool dwt_ok, const char *fw_version)
{
    char content[APP_FRAME_SIZE];
    int n = snprintf(content, sizeof(content), "BOOT,%lu,%u,%s",
                     (unsigned long)sysclk_hz, dwt_ok ? 1u : 0u, fw_version);
    return finish_frame(out, content, n, "BOOT");
}

bool Protocol_BuildREC(uint8_t out[APP_FRAME_SIZE], const EventRecord_t *rec)
{
    char b1[12], b2[12], b3[12], b4[12];
    char content[APP_FRAME_SIZE];
    int n = snprintf(content, sizeof(content), "REC,%u,%u,%lu,%s,%s,%s,%s,%s",
                     (unsigned)rec->event_id, (unsigned)rec->scenario_id,
                     (unsigned long)rec->t0_abs_ms,
                     fmt_delta(b1, rec, REC_TS_T1, rec->t1),
                     fmt_delta(b2, rec, REC_TS_T2, rec->t2),
                     fmt_delta(b3, rec, REC_TS_T3, rec->t3),
                     fmt_delta(b4, rec, REC_TS_T4, rec->t4),
                     status_to_str(rec->status));
    return finish_frame(out, content, n, "REC");
}

bool Protocol_BuildSTAT(uint8_t out[APP_FRAME_SIZE],
                        uint8_t scenario_id, const RingCounters_t *c)
{
    char content[APP_FRAME_SIZE];
    int n = snprintf(content, sizeof(content),
                     "STAT,%u,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu",
                     (unsigned)scenario_id,
                     (unsigned long)c->success_count,
                     (unsigned long)(c->btn_queue_drop_count + c->tx_queue_drop_count),
                     (unsigned long)c->tx_start_err_count,
                     (unsigned long)c->tx_timeout_count,
                     (unsigned long)c->ring_overflow_count,
                     (unsigned long)c->cmd_crc_err_count,
                     (unsigned long)c->tel_drop_count,
                     (unsigned long)c->tel_tx_fail_count,
                     (unsigned long)c->id_mismatch_count);
    return finish_frame(out, content, n, "STAT");
}

bool Protocol_BuildDumpEnd(uint8_t out[APP_FRAME_SIZE])
{
    static const char content[] = "DUMP_END";
    return finish_frame(out, content, (int)(sizeof(content) - 1u), "DUMP_END");
}

/* ------------------------------- Komutlar -------------------------------- */

static bool hex_nibble(char c, uint8_t *out)
{
    if (c >= '0' && c <= '9') { *out = (uint8_t)(c - '0'); return true; }
    if (c >= 'A' && c <= 'F') { *out = (uint8_t)(c - 'A' + 10); return true; }
    return false;
}

/* Yalnizca rakam; bos, isaretli veya tasan degerleri reddeder. */
static bool parse_u32(const char *s, uint32_t *out)
{
    if (s == NULL || *s == '\0') return false;
    uint32_t v = 0u;
    for (; *s != '\0'; s++) {
        if (*s < '0' || *s > '9') return false;
        uint32_t digit = (uint32_t)(*s - '0');
        if (v > (UINT32_MAX - digit) / 10u) return false;
        v = v * 10u + digit;
    }
    *out = v;
    return true;
}

static char *next_token(char **cursor)
{
    char *start = *cursor;
    if (start == NULL) return NULL;
    char *comma = strchr(start, ',');
    if (comma != NULL) {
        *comma = '\0';
        *cursor = comma + 1;
    } else {
        *cursor = NULL;
    }
    return start;
}

ParseResult_t Protocol_ParseCommandLine(const char *line, size_t len, Command_t *out)
{
    out->type = CMD_NONE;
    if (len == 0u || len > APP_CMD_LINE_MAX_LEN) {
        return PARSE_MALFORMED;
    }

    char buf[APP_CMD_LINE_MAX_LEN + 1];
    memcpy(buf, line, len);
    buf[len] = '\0';
    if (strlen(buf) != len) {
        return PARSE_MALFORMED; /* gomulu NUL */
    }

    char *last_comma = strrchr(buf, ',');
    if (last_comma == NULL || strlen(last_comma + 1) != 2u) {
        return PARSE_MALFORMED;
    }
    uint8_t hi, lo;
    if (!hex_nibble(last_comma[1], &hi) || !hex_nibble(last_comma[2], &lo)) {
        return PARSE_MALFORMED;
    }
    uint8_t expected_crc = (uint8_t)((hi << 4) | lo);
    size_t content_len = (size_t)(last_comma - buf);
    if (Protocol_Crc8((const uint8_t *)buf, content_len) != expected_crc) {
        return PARSE_CRC_ERROR;
    }
    *last_comma = '\0';

    char *cursor = buf;
    char *name = next_token(&cursor);

    if (strcmp(name, "CFG") == 0) {
        uint32_t period, load, scenario;
        char *p = next_token(&cursor);
        char *l = next_token(&cursor);
        char *s = next_token(&cursor);
        if (cursor != NULL || !parse_u32(p, &period) || !parse_u32(l, &load) ||
            !parse_u32(s, &scenario) || scenario > 255u) {
            return PARSE_MALFORMED;
        }
        out->type        = CMD_CFG;
        out->period_ms   = period;
        out->load_iter   = load;
        out->scenario_id = (uint8_t)scenario;
        return PARSE_OK;
    }

    if (cursor != NULL) {
        return PARSE_MALFORMED; /* argumansiz komutlarda fazla alan */
    }
    if (strcmp(name, "START") == 0)       { out->type = CMD_START;       return PARSE_OK; }
    if (strcmp(name, "STOP") == 0)        { out->type = CMD_STOP;        return PARSE_OK; }
    if (strcmp(name, "DUMP") == 0)        { out->type = CMD_DUMP;        return PARSE_OK; }
    if (strcmp(name, "RESET_STATS") == 0) { out->type = CMD_RESET_STATS; return PARSE_OK; }
    return PARSE_MALFORMED;
}
