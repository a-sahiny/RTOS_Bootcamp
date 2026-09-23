#include <string.h>
#include "main.h"
#include "app_hw.h"
#include "app_rtos.h"
#include "app_ring_buffer.h"
#include "app_protocol.h"
#include "app_timestamp.h"
#include "app_button.h"

#define FW_VERSION "0.3"

typedef enum {
    TX_OK = 0,
    TX_START_ERR,   /* HAL_UART_Transmit_DMA HAL_OK donmedi */
    TX_DMA_ERR,     /* HAL_UART_ErrorCallback: DMA hatasi */
    TX_TIMEOUT,     /* TC APP_TX_TC_TIMEOUT_MS icinde gelmedi */
} TxResult_t;

/* Tek sahipli TX arabellegi: gorev, TC (veya timeout + abort) olmadan bir
 * sonraki mesaja gecmedigi icin aktarim bitene kadar gecerli kalir.
 * 32 bayt hizali + 64 bayt = 2 D-Cache satiri (cache acilirsa temizlenebilsin). */
static uint8_t s_tx_buffer[APP_FRAME_SIZE] __attribute__((aligned(32)));

/* ========================================================================
 * UART gonderimi (yalnizca UartTxTask cagirir)
 * ==================================================================== */
static TxResult_t uart_tx_send(const uint8_t frame[APP_FRAME_SIZE], uint32_t *t3, uint32_t *t4)
{
    memcpy(s_tx_buffer, frame, APP_FRAME_SIZE);
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0u) {
        SCB_CleanDCache_by_Addr((uint32_t *)s_tx_buffer, (int32_t)sizeof(s_tx_buffer));
    }
#endif
    /* Onceki bir timeout'tan kalmis gec gelen TC jetonunu temizle. */
    while (osSemaphoreAcquire(TxDoneSemHandle, 0u) == osOK) {
    }
    g_tx_dma_error = false;

    *t3 = Timestamp_Now();   /* t3: UART baslatma cagrisindan hemen once */
    if (HAL_UART_Transmit_DMA(&APP_UART_HANDLE, s_tx_buffer, APP_FRAME_SIZE) != HAL_OK) {
        return TX_START_ERR;
    }

    if (osSemaphoreAcquire(TxDoneSemHandle, APP_TX_TC_TIMEOUT_MS) != osOK) {
        (void)HAL_UART_AbortTransmit(&APP_UART_HANDLE);
        return TX_TIMEOUT;
    }
    if (g_tx_dma_error) {
        return TX_DMA_ERR;
    }
    *t4 = g_tx_done_cycles;  /* t4: TC callback icinde (ISR) alindi, gorev uyanma gecikmesi dahil degil */
    return TX_OK;
}

static void send_item(const TxItem_t *item)
{
    uint32_t t3 = 0u, t4 = 0u;
    TxResult_t r = uart_tx_send(item->frame, &t3, &t4);

    if (item->ring_index == RING_INDEX_NONE) {
        if (r != TX_OK) {
            RingBuffer_IncTelTxFail();
        }
        return;
    }

    /* Kayit yazimi olcum zincirinin disinda (gonderimden sonra) yapilir. */
    RingBuffer_SetT3(item->ring_index, item->event_id, t3);
    switch (r) {
        case TX_OK:        RingBuffer_CloseSuccess(item->ring_index, item->event_id, t4); break;
        case TX_TIMEOUT:   RingBuffer_CloseTimeout(item->ring_index, item->event_id);     break;
        case TX_START_ERR:
        case TX_DMA_ERR:
        default:           RingBuffer_CloseTxErr(item->ring_index, item->event_id);       break;
    }
}

/* Dump/boot cercevesi: zamanlamaya duyarsiz, bir kez yeniden denenir. */
static void send_meta(const uint8_t frame[APP_FRAME_SIZE])
{
    uint32_t t3, t4;
    if (uart_tx_send(frame, &t3, &t4) != TX_OK) {
        (void)uart_tx_send(frame, &t3, &t4);
    }
}

/* ------------------------------ Kontrol ---------------------------------- */

static void dump_send_all(void)
{
    uint32_t      cursor = 0u;
    EventRecord_t rec;
    uint8_t       frame[APP_FRAME_SIZE];

    while (RingBuffer_DumpNext(&cursor, &rec)) {
        (void)Protocol_BuildREC(frame, &rec);
        send_meta(frame);
    }
    RingCounters_t counters = RingBuffer_GetCounters();
    (void)Protocol_BuildSTAT(frame, g_cfg.scenario_id, &counters);
    send_meta(frame);
    (void)Protocol_BuildDumpEnd(frame);
    send_meta(frame);
}

static void control_handle_command(const Command_t *cmd)
{
    switch (cmd->type) {
    case CMD_CFG:
        if (!g_running) {
            g_cfg.period_ms   = (cmd->period_ms < APP_MIN_TELEMETRY_PERIOD_MS)
                                    ? APP_MIN_TELEMETRY_PERIOD_MS : cmd->period_ms;
            g_cfg.load_iter   = cmd->load_iter;
            g_cfg.scenario_id = cmd->scenario_id;
        }
        break;

    case CMD_START:
        if (!g_running) {
            RingBuffer_ResetForScenario();
            Button_ResetForScenario();
            g_run_start_ms = HAL_GetTick();
            g_running = true;                           /* once bayrak degisken... */
            (void)osEventFlagsSet(RunFlagsHandle, RUN_BIT); /* ...sonra TelemetryTask uyanir */
        }
        break;

    case CMD_STOP:
        if (g_running) {
            /* Sira onemli: once event flag temizlenir. Tersi olursa yuksek
             * oncelikli TelemetryTask araya girip bayrak hala set iken
             * bekleme dongusunde donerek UartTxTask'i ac birakabilir. */
            (void)osEventFlagsClear(RunFlagsHandle, RUN_BIT);
            g_running = false;
        }
        break;

    case CMD_DUMP:
        /* Telemetri durmus ve TX kuyrugu bosalmis olmali (SPEC.md S7).
         * Kosul saglanmazsa komut yok sayilir; PC zaman asimiyla tekrar dener. */
#ifdef APP_TX_MODE_DUAL_QUEUE
        if (!g_running && osMessageQueueGetCount(BtnTxQueueHandle) == 0u
                       && osMessageQueueGetCount(TxQueueHandle) == 0u) {
#else
        if (!g_running && osMessageQueueGetCount(TxQueueHandle) == 0u) {
#endif
            dump_send_all();
        }
        break;

    case CMD_RESET_STATS:
        if (!g_running) {
            RingBuffer_ResetForScenario();
            Button_ResetForScenario();
        }
        break;

    default:
        break;
    }
}

static void handle_cmd_line(const CmdLine_t *line)
{
    Command_t cmd;
    ParseResult_t pr = Protocol_ParseCommandLine(line->text, line->len, &cmd);
    if (pr == PARSE_CRC_ERROR) {
        RingBuffer_IncCmdCrcErr();
    } else if (pr == PARSE_OK) {
        control_handle_command(&cmd);
    }
}

/* ========================================================================
 * UartTxTask (Dusuk - 1): FIFO TX kuyrugunu tuketir, UART'i yonetir,
 * PC komutlarini isler.
 * ==================================================================== */
void UartTxTask_Run(void *argument)
{
    (void)argument;

    uint8_t boot[APP_FRAME_SIZE];
    (void)Protocol_BuildBOOT(boot, SystemCoreClock, g_dwt_ok, FW_VERSION);
    send_meta(boot);

    for (;;) {
        CmdLine_t line;
        while (osMessageQueueGet(CmdQueueHandle, &line, NULL, 0u) == osOK) {
            handle_cmd_line(&line);
        }

        TxItem_t item;
#ifdef APP_TX_MODE_DUAL_QUEUE
        /* Task ONCELIKLERI AYNI. Gonderime baslamamis mesajlarda BTN > TEL.
         * Aktif DMA aktarimi yarida kesilmez. */
        if (osMessageQueueGet(BtnTxQueueHandle, &item, NULL, 0u) == osOK ||
            osMessageQueueGet(TxQueueHandle, &item, NULL, 0u) == osOK) {
            send_item(&item);
            continue;
        }
        /* UartTxTask iki FIFO da bosken uyur; yeni mesaj/komut ile uyanir. */
        (void)osThreadFlagsWait(APP_TX_WORK_READY_FLAG, osFlagsWaitAny, osWaitForever);
#else
        /* Sinirli bekleme: kuyruk bosken de komutlar en gec 20 ms'de islenir. */
        if (osMessageQueueGet(TxQueueHandle, &item, NULL, 20u) == osOK) {
            send_item(&item);
        }
#endif
    }
}

/* ========================================================================
 * ButtonTask (Orta - 2): olayi alir, yaniti uretir, TX kuyruguna birakir.
 * ==================================================================== */
void ButtonTask_Run(void *argument)
{
    (void)argument;

    for (;;) {
        ButtonEvent_t evt;
        if (osMessageQueueGet(ButtonQueueHandle, &evt, NULL, osWaitForever) != osOK) {
            continue;
        }
        uint32_t t1 = Timestamp_Now();   /* t1: olay alindiktan hemen sonra */

        /* Yanit uretimi (S2 = t2 - t1 bu blogu olcer). t2 henuz bilinmedigi
         * icin yanita konmaz; tam kayit dump ile gelir (SPEC.md S6). */
        TxItem_t item;
        item.type       = MSG_TYPE_BTN;
        item.event_id   = evt.event_id;
        item.ring_index = evt.ring_index;
        (void)Protocol_BuildBTN(item.frame, evt.event_id, evt.scenario_id, evt.t0_abs_ms,
                                Timestamp_DeltaToUs(evt.t0, t1));

        uint32_t t2 = Timestamp_Now();   /* t2: kuyruga birakma cagrisindan hemen once */
#ifdef APP_TX_MODE_DUAL_QUEUE
        /* Ayrilmis BTN FIFO'ya BLOKLANMADAN yaz: R deadline = 20 ms. */
        osStatus_t st = osMessageQueuePut(BtnTxQueueHandle, &item, 0u, 0u);
        RingBuffer_SetT1T2(evt.ring_index, evt.event_id, t1, t2);
        if (st == osOK) {
            (void)osThreadFlagsSet(UartTxTaskHandle, APP_TX_WORK_READY_FLAG);
        } else {
            RingBuffer_CloseDropTx(evt.ring_index, evt.event_id);
        }
#else
        osStatus_t st = osMessageQueuePut(TxQueueHandle, &item, 0u, APP_TX_QUEUE_SEND_TIMEOUT_MS);

        /* ButtonTask, UartTxTask'ten yuksek oncelikli oldugu icin bu yazma,
         * UartTxTask'in ayni kayda t3 yazmasindan once tamamlanir. */
        RingBuffer_SetT1T2(evt.ring_index, evt.event_id, t1, t2);
        if (st != osOK) {
            RingBuffer_CloseDropTx(evt.ring_index, evt.event_id);
        }
#endif
    }
}

/* ========================================================================
 * TelemetryTask (Yuksek - 3): periyodik TEL + sabit iterasyonlu CPU yuku.
 * ==================================================================== */
void TelemetryTask_Run(void *argument)
{
    (void)argument;

    for (;;) {
        (void)osEventFlagsWait(RunFlagsHandle, RUN_BIT, osFlagsWaitAny | osFlagsNoClear, osWaitForever);
        if (!g_running) {
            osDelay(1u); /* STOP gecisinin ortasi: donerek alt gorevleri ac birakma */
            continue;
        }

        uint32_t seq  = 0u;
        uint32_t wake = osKernelGetTickCount();  /* her START'ta tazele: eski deger ani TEL patlamasi uretir */

        while (g_running) {
            uint32_t period = g_cfg.period_ms;
            wake += period;
            uint32_t now = osKernelGetTickCount();
            if ((int32_t)(wake - now) <= 0) {
                /* Tasma (yuk >= periyot): kaybedilen periyotlari telafi etmeye
                 * calisma, yeniden hizala ve yine de bir periyot uyu. Aksi
                 * halde en yuksek oncelikli gorev hic bloklanmaz ve tum alt
                 * gorevleri (STOP'u isleyecek olan UartTxTask dahil) ac birakir. */
                wake = now + period;
            }
            (void)osDelayUntil(wake);
            if (!g_running) {
                break;
            }

            /* Sabit iterasyonlu yapay CPU yuku; suresi kalibrasyon icin TEL'de raporlanir. */
            uint32_t load_start = Timestamp_Now();
            volatile uint32_t acc = 0u;
            for (uint32_t i = 0u; i < g_cfg.load_iter; i++) {
                acc += (i * 2654435761u) ^ (acc << 1);
            }
            uint32_t load_us = Timestamp_DeltaToUs(load_start, Timestamp_Now());

            RingCounters_t counters = RingBuffer_GetCounters();
            TxItem_t item;
            item.type       = MSG_TYPE_TEL;
            item.event_id   = 0u;
            item.ring_index = RING_INDEX_NONE;
            (void)Protocol_BuildTEL(item.frame, ++seq, g_cfg.scenario_id, App_RunElapsedMs(),
                                    g_cfg.period_ms, g_cfg.load_iter, load_us, &counters);

#ifdef APP_TX_MODE_DUAL_QUEUE
            /* TEL dolu kuyrukta 200 ms bloklanmasin: aksi halde dusuk
             * oncelikli UartTxTask calisip backlog'u bosaltamaz. */
            if (osMessageQueuePut(TxQueueHandle, &item, 0u, 0u) == osOK) {
                (void)osThreadFlagsSet(UartTxTaskHandle, APP_TX_WORK_READY_FLAG);
            } else {
                RingBuffer_IncTelDrop();
            }
#else
            if (osMessageQueuePut(TxQueueHandle, &item, 0u, APP_TX_QUEUE_SEND_TIMEOUT_MS) != osOK) {
                RingBuffer_IncTelDrop();
            }
#endif
        }
    }
}
