/**
 * app_protocol.h
 *
 * UART tel protokolu: 64 baytlik sabit cerceve (62 bayt ASCII payload +
 * 1 bayt CRC-8 + LF) ve PC->MCU komut satirlari. Bkz. SPEC.md S8.
 */
#ifndef APP_PROTOCOL_H
#define APP_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "app_config.h"
#include "app_ring_buffer.h"

/** poly 0x07 (x^8+x^2+x+1), init 0x00, reflect yok, xorout yok. */
uint8_t Protocol_Crc8(const uint8_t *data, size_t len);

/* ----------------------- Cerceve olusturucular -----------------------
 * Her biri tam APP_FRAME_SIZE (64) bayt yazar. Icerik 62 bayta sigmazsa
 * sessizce kesilmez: yerine "ERR,ENCODE_OVERFLOW,<tip>" cercevesi yazilir
 * ve fonksiyon false doner. */

/** load_us: bu periyotta yapay yuk dongusunun DWT ile olculen suresi. */
bool Protocol_BuildTEL(uint8_t out[APP_FRAME_SIZE],
                       uint32_t seq, uint8_t scenario_id, uint32_t t_abs_ms,
                       uint32_t period_ms, uint32_t load_iter, uint32_t load_us,
                       const RingCounters_t *counters);

/** Canli "butona basildi" yaniti. t2 bu cerceve uretildikten SONRA
 *  olculdugu icin yanitta t2 yoktur (SPEC.md S6). */
bool Protocol_BuildBTN(uint8_t out[APP_FRAME_SIZE],
                       uint16_t event_id, uint8_t scenario_id, uint32_t t0_abs_ms,
                       uint32_t d1_us);

/** Acilis cercevesi: VCP'nin calistigini, gercek saat frekansini ve DWT
 *  sayacinin sayip saymadigini PC'ye bildirir (bring-up dogrulamasi). */
bool Protocol_BuildBOOT(uint8_t out[APP_FRAME_SIZE],
                        uint32_t sysclk_hz, bool dwt_ok, const char *fw_version);

/** Olculmemis zaman damgalari (ts_mask) '-' olarak yazilir. */
bool Protocol_BuildREC(uint8_t out[APP_FRAME_SIZE], const EventRecord_t *rec);

bool Protocol_BuildSTAT(uint8_t out[APP_FRAME_SIZE],
                        uint8_t scenario_id, const RingCounters_t *counters);

bool Protocol_BuildDumpEnd(uint8_t out[APP_FRAME_SIZE]);

/* ----------------------------- Komutlar ------------------------------ */

typedef enum {
    CMD_NONE = 0,
    CMD_CFG,
    CMD_START,
    CMD_STOP,
    CMD_DUMP,
    CMD_RESET_STATS,
} CommandType_t;

typedef struct {
    CommandType_t type;
    uint32_t      period_ms;
    uint32_t      load_iter;
    uint8_t       scenario_id;
} Command_t;

typedef enum {
    PARSE_OK = 0,
    PARSE_CRC_ERROR,   /* CRC alani dogru bicimde ama deger uyusmuyor */
    PARSE_MALFORMED,   /* bicim hatasi / bilinmeyen komut / gecersiz sayi */
} ParseResult_t;

/**
 * `line` (LF haric, `len` bayt) komut satirini ayristirir. Son alan,
 * oncesindeki icerigin 2 haneli buyuk harf hex CRC-8'idir (SPEC.md S8.4).
 * Sayisal alanlar yalnizca ondalik rakam kabul eder.
 */
ParseResult_t Protocol_ParseCommandLine(const char *line, size_t len, Command_t *out);

#endif /* APP_PROTOCOL_H */
