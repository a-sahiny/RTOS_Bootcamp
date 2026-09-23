/**
 * app_rtos.h
 *
 * Paylasilan RTOS nesneleri (kuyruklar, semaphore, event flag), gorevler
 * arasi paylasilan durum ve gorev prototipleri. App_RTOS_Init(), CubeMX'in
 * urettigi freertos.c icindeki MX_FREERTOS_Init() -> USER CODE RTOS_THREADS
 * blogundan cagrilir (bu depodaki Core/Src/freertos.c bu blogu hazir icerir).
 */
#ifndef APP_RTOS_H
#define APP_RTOS_H

#include <stdint.h>
#include <stdbool.h>
#include "cmsis_os2.h"
#include "app_config.h"

#define MSG_TYPE_TEL  0u
#define MSG_TYPE_BTN  1u

/** ButtonQueue elemani: ISR -> ButtonTask */
typedef struct {
    uint32_t t0;          /* DWT cycle, ISR girisinde alinan */
    uint32_t t0_abs_ms;   /* START'tan itibaren ms (kronoloji icin) */
    uint16_t event_id;
    uint16_t ring_index;
    uint8_t  scenario_id;
} ButtonEvent_t;

/** TxQueue elemani: ButtonTask/TelemetryTask -> UartTxTask */
typedef struct {
    uint8_t  type;        /* MSG_TYPE_* */
    uint16_t event_id;    /* BTN icin: kaydi kapatirken dogrulanir */
    uint16_t ring_index;  /* TEL icin RING_INDEX_NONE */
    uint8_t  frame[APP_FRAME_SIZE];
} TxItem_t;

/** CmdQueue elemani: RX ISR -> UartTxTask (ayristirma gorevde yapilir) */
typedef struct {
    uint8_t len;
    char    text[APP_CMD_LINE_MAX_LEN];
} CmdLine_t;

/** Aktif senaryo (yalnizca RUNNING=0 iken CFG ile degisir). */
typedef struct {
    uint32_t period_ms;
    uint32_t load_iter;
    uint8_t  scenario_id;
} ScenarioConfig_t;

extern osMessageQueueId_t ButtonQueueHandle;
extern osMessageQueueId_t TxQueueHandle;
#ifdef APP_TX_MODE_DUAL_QUEUE
extern osMessageQueueId_t BtnTxQueueHandle;
extern osThreadId_t       UartTxTaskHandle;
#endif
extern osMessageQueueId_t CmdQueueHandle;
extern osSemaphoreId_t    TxDoneSemHandle;
extern osEventFlagsId_t   RunFlagsHandle;

#define RUN_BIT 0x00000001u
#ifdef APP_TX_MODE_DUAL_QUEUE
#define APP_TX_WORK_READY_FLAG 0x00000001u
#endif

extern volatile bool     g_running;        /* ISR dogrudan okur */
extern volatile uint32_t g_run_start_ms;   /* START anindaki HAL_GetTick() */
extern ScenarioConfig_t  g_cfg;
extern bool              g_dwt_ok;         /* DWT sayaci calisiyor mu */

/* UART TC / hata bilgisi: ISR yazar, UartTxTask okur. */
extern volatile uint32_t g_tx_done_cycles; /* t4: TC callback'inde alinan DWT */
extern volatile bool     g_tx_dma_error;

/** HAL_GetTick() tabanli, START'tan itibaren gecen ms. */
uint32_t App_RunElapsedMs(void);

/** Kuyruklar, semaphore, event flag + 3 gorev. Iki kez cagrilsa da tek kez calisir. */
void App_RTOS_Init(void);

void UartTxTask_Run(void *argument);
void ButtonTask_Run(void *argument);
void TelemetryTask_Run(void *argument);

#endif /* APP_RTOS_H */
