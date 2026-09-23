/**
 * app_ring_buffer.h
 *
 * Buton olaylarinin (t0..t4 + durum) tutuldugu dairesel RAM tamponu.
 * Deney (RUNNING) sirasinda doldurulur, deney sonu `DUMP` komutuyla disari
 * aktarilir. Neden bu tasarim: SPEC.md S7.
 *
 * Her yazma, kaydi ayiran ISR'in verdigi (ring_index, event_id) ciftiyle
 * yapilir; slot o arada baska bir olaya devredilmisse yazma reddedilir ve
 * id_mismatch_count artar ("kayit dogru olay kimligiyle kapatilsin").
 */
#ifndef APP_RING_BUFFER_H
#define APP_RING_BUFFER_H

#include <stdint.h>
#include <stdbool.h>

#define RING_INDEX_NONE  0xFFFFu   /* TEL / dump mesajlari: kayit yok */

#define REC_TS_T1  (1u << 0)
#define REC_TS_T2  (1u << 1)
#define REC_TS_T3  (1u << 2)
#define REC_TS_T4  (1u << 3)

typedef enum {
    REC_STATUS_PENDING = 0,  /* henuz kapanmadi */
    REC_STATUS_OK      = 1,  /* R <= deadline */
    REC_STATUS_LATE    = 2,  /* R > deadline, ama tamamlandi */
    REC_STATUS_DROP    = 3,  /* ButtonQueue veya TxQueue dolu */
    REC_STATUS_TX_ERR  = 4,  /* UART/DMA baslatilamadi veya DMA hatasi */
    REC_STATUS_TIMEOUT = 5,  /* TC zaman asimi */
} RecordStatus_t;

typedef struct {
    uint32_t t0, t1, t2, t3, t4;   /* DWT cycle */
    uint32_t t0_abs_ms;            /* deney basindan (START) itibaren ms */
    uint16_t event_id;
    uint8_t  scenario_id;
    uint8_t  status;               /* RecordStatus_t */
    uint8_t  ts_mask;              /* REC_TS_* : hangi t'ler gercekten olculdu */
    bool     valid;
} EventRecord_t;

typedef struct {
    uint32_t success_count;        /* OK + LATE */
    uint32_t late_count;
    uint32_t btn_queue_drop_count; /* ISR: ButtonQueue dolu */
    uint32_t tx_queue_drop_count;  /* ButtonTask: TxQueue dolu */
    uint32_t tx_start_err_count;   /* BTN: DMA baslatma/DMA hatasi */
    uint32_t tx_timeout_count;     /* BTN: TC zaman asimi */
    uint32_t tel_drop_count;       /* TEL: TxQueue dolu */
    uint32_t tel_tx_fail_count;    /* TEL: DMA hatasi veya TC zaman asimi */
    uint32_t ring_overflow_count;  /* uzerine yazilan (kaybolan) kayit */
    uint32_t cmd_crc_err_count;    /* PC->MCU komut CRC hatasi */
    uint32_t id_mismatch_count;    /* yanlis olaya yazma girisimi (olmamali) */
} RingCounters_t;

void RingBuffer_Init(void);

/** START'ta cagrilir: kayitlari ve sayaclari sifirlar (RUNNING=0 iken). */
void RingBuffer_ResetForScenario(void);

/** ISR: yeni olay icin slot ayirir; dolu ise en eskinin uzerine yazar. */
uint16_t RingBuffer_Alloc(uint16_t event_id, uint8_t scenario_id,
                          uint32_t t0, uint32_t t0_abs_ms);

void RingBuffer_SetT1T2(uint16_t ring_index, uint16_t event_id, uint32_t t1, uint32_t t2);
void RingBuffer_SetT3(uint16_t ring_index, uint16_t event_id, uint32_t t3);

/** t4 kaydedilir, R = t4 - t0 mikrosaniye cinsinden deadline ile karsilastirilir. */
void RingBuffer_CloseSuccess(uint16_t ring_index, uint16_t event_id, uint32_t t4);
void RingBuffer_CloseDropIsr(uint16_t ring_index, uint16_t event_id);   /* ISR */
void RingBuffer_CloseDropTx(uint16_t ring_index, uint16_t event_id);
void RingBuffer_CloseTxErr(uint16_t ring_index, uint16_t event_id);
void RingBuffer_CloseTimeout(uint16_t ring_index, uint16_t event_id);

RingCounters_t RingBuffer_GetCounters(void);

void RingBuffer_IncCmdCrcErr(void);
void RingBuffer_IncTelDrop(void);
void RingBuffer_IncTelTxFail(void);

/**
 * Dump iteratoru: bu senaryodaki kayitlari eskiden yeniye dolasir.
 * `*cursor` 0 ile baslatilir; kayit varsa true doner ve `out`'a kopyalar.
 */
bool RingBuffer_DumpNext(uint32_t *cursor, EventRecord_t *out);

#endif /* APP_RING_BUFFER_H */
