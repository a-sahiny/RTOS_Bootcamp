#include <string.h>
#include "app_ring_buffer.h"
#include "app_config.h"
#include "app_timestamp.h"
#include "FreeRTOS.h"
#include "task.h"

static EventRecord_t  s_records[APP_RING_BUFFER_CAPACITY];
static uint32_t       s_total_allocated;   /* bu senaryoda ayrilan toplam kayit */
static RingCounters_t s_counters;

void RingBuffer_Init(void)
{
    memset(s_records, 0, sizeof(s_records));
    s_total_allocated = 0u;
    memset(&s_counters, 0, sizeof(s_counters));
}

void RingBuffer_ResetForScenario(void)
{
    taskENTER_CRITICAL();
    memset(s_records, 0, sizeof(s_records));
    s_total_allocated = 0u;
    memset(&s_counters, 0, sizeof(s_counters));
    taskEXIT_CRITICAL();
}

uint16_t RingBuffer_Alloc(uint16_t event_id, uint8_t scenario_id,
                          uint32_t t0, uint32_t t0_abs_ms)
{
    UBaseType_t saved = taskENTER_CRITICAL_FROM_ISR();

    uint32_t idx = s_total_allocated % APP_RING_BUFFER_CAPACITY;
    if (s_total_allocated >= APP_RING_BUFFER_CAPACITY) {
        s_counters.ring_overflow_count++;
    }
    s_total_allocated++;

    EventRecord_t *r = &s_records[idx];
    memset(r, 0, sizeof(*r));
    r->event_id    = event_id;
    r->scenario_id = scenario_id;
    r->t0          = t0;
    r->t0_abs_ms   = t0_abs_ms;
    r->status      = (uint8_t)REC_STATUS_PENDING;
    r->valid       = true;

    taskEXIT_CRITICAL_FROM_ISR(saved);
    return (uint16_t)idx;
}

/* Kritik bolge ICINDEN cagrilir. Slot artik bu olaya ait degilse NULL. */
static EventRecord_t *slot_for(uint16_t ring_index, uint16_t event_id)
{
    if (ring_index >= APP_RING_BUFFER_CAPACITY) {
        s_counters.id_mismatch_count++;
        return NULL;
    }
    EventRecord_t *r = &s_records[ring_index];
    if (!r->valid || r->event_id != event_id) {
        s_counters.id_mismatch_count++;
        return NULL;
    }
    return r;
}

void RingBuffer_SetT1T2(uint16_t ring_index, uint16_t event_id, uint32_t t1, uint32_t t2)
{
    taskENTER_CRITICAL();
    EventRecord_t *r = slot_for(ring_index, event_id);
    if (r != NULL) {
        r->t1 = t1;
        r->t2 = t2;
        r->ts_mask |= (uint8_t)(REC_TS_T1 | REC_TS_T2);
    }
    taskEXIT_CRITICAL();
}

void RingBuffer_SetT3(uint16_t ring_index, uint16_t event_id, uint32_t t3)
{
    taskENTER_CRITICAL();
    EventRecord_t *r = slot_for(ring_index, event_id);
    if (r != NULL) {
        r->t3 = t3;
        r->ts_mask |= (uint8_t)REC_TS_T3;
    }
    taskEXIT_CRITICAL();
}

void RingBuffer_CloseSuccess(uint16_t ring_index, uint16_t event_id, uint32_t t4)
{
    taskENTER_CRITICAL();
    EventRecord_t *r = slot_for(ring_index, event_id);
    if (r != NULL) {
        r->t4 = t4;
        r->ts_mask |= (uint8_t)REC_TS_T4;
        /* Mikrosaniye cinsinden karsilastirma: ms'ye kesme 20.9 ms'yi OK sayardi. */
        if (Timestamp_DeltaToUs(r->t0, t4) <= APP_DEADLINE_US) {
            r->status = (uint8_t)REC_STATUS_OK;
        } else {
            r->status = (uint8_t)REC_STATUS_LATE;
            s_counters.late_count++;
        }
        s_counters.success_count++;
    }
    taskEXIT_CRITICAL();
}

void RingBuffer_CloseDropIsr(uint16_t ring_index, uint16_t event_id)
{
    UBaseType_t saved = taskENTER_CRITICAL_FROM_ISR();
    EventRecord_t *r = slot_for(ring_index, event_id);
    if (r != NULL) {
        r->status = (uint8_t)REC_STATUS_DROP;
        s_counters.btn_queue_drop_count++;
    }
    taskEXIT_CRITICAL_FROM_ISR(saved);
}

static void close_with(uint16_t ring_index, uint16_t event_id,
                       RecordStatus_t status, uint32_t *counter)
{
    taskENTER_CRITICAL();
    EventRecord_t *r = slot_for(ring_index, event_id);
    if (r != NULL) {
        r->status = (uint8_t)status;
        (*counter)++;
    }
    taskEXIT_CRITICAL();
}

void RingBuffer_CloseDropTx(uint16_t ring_index, uint16_t event_id)
{
    close_with(ring_index, event_id, REC_STATUS_DROP, &s_counters.tx_queue_drop_count);
}

void RingBuffer_CloseTxErr(uint16_t ring_index, uint16_t event_id)
{
    close_with(ring_index, event_id, REC_STATUS_TX_ERR, &s_counters.tx_start_err_count);
}

void RingBuffer_CloseTimeout(uint16_t ring_index, uint16_t event_id)
{
    close_with(ring_index, event_id, REC_STATUS_TIMEOUT, &s_counters.tx_timeout_count);
}

RingCounters_t RingBuffer_GetCounters(void)
{
    RingCounters_t copy;
    taskENTER_CRITICAL();
    copy = s_counters;
    taskEXIT_CRITICAL();
    return copy;
}

static void inc_counter(uint32_t *counter)
{
    taskENTER_CRITICAL();
    (*counter)++;
    taskEXIT_CRITICAL();
}

void RingBuffer_IncCmdCrcErr(void) { inc_counter(&s_counters.cmd_crc_err_count); }
void RingBuffer_IncTelDrop(void)   { inc_counter(&s_counters.tel_drop_count); }
void RingBuffer_IncTelTxFail(void) { inc_counter(&s_counters.tel_tx_fail_count); }

bool RingBuffer_DumpNext(uint32_t *cursor, EventRecord_t *out)
{
    bool found = false;
    taskENTER_CRITICAL();
    bool wrapped = s_total_allocated > APP_RING_BUFFER_CAPACITY;
    uint32_t count = wrapped ? APP_RING_BUFFER_CAPACITY : s_total_allocated;
    if (*cursor < count) {
        uint32_t start = wrapped ? (s_total_allocated % APP_RING_BUFFER_CAPACITY) : 0u;
        *out = s_records[(start + *cursor) % APP_RING_BUFFER_CAPACITY];
        (*cursor)++;
        found = out->valid;
    }
    taskEXIT_CRITICAL();
    return found;
}
