#include "main.h"
#include "app_rtos.h"
#include "app_ring_buffer.h"
#include "app_button.h"
#include "app_cmd_rx.h"
#include "app_timestamp.h"

osMessageQueueId_t ButtonQueueHandle;
osMessageQueueId_t TxQueueHandle;
#ifdef APP_TX_MODE_DUAL_QUEUE
osMessageQueueId_t BtnTxQueueHandle;
osThreadId_t       UartTxTaskHandle;
#endif
osMessageQueueId_t CmdQueueHandle;
osSemaphoreId_t    TxDoneSemHandle;
osEventFlagsId_t   RunFlagsHandle;

volatile bool     g_running = false;
volatile uint32_t g_run_start_ms = 0u;
bool              g_dwt_ok = false;
volatile uint32_t g_tx_done_cycles = 0u;
volatile bool     g_tx_dma_error = false;

ScenarioConfig_t g_cfg = {
    .period_ms   = APP_DEFAULT_TELEMETRY_PERIOD_MS,
    .load_iter   = APP_DEFAULT_LOAD_ITERATIONS,
    .scenario_id = APP_DEFAULT_SCENARIO_ID,
};

uint32_t App_RunElapsedMs(void)
{
    return HAL_GetTick() - g_run_start_ms;
}

static void *require(void *handle)
{
    if (handle == NULL) {
        Error_Handler(); /* heap yetersiz: configTOTAL_HEAP_SIZE'i kontrol edin */
    }
    return handle;
}

void App_RTOS_Init(void)
{
    static bool initialized = false;
    if (initialized) {
        return;
    }
    initialized = true;

    g_dwt_ok = Timestamp_Init();
    RingBuffer_Init();
    Button_Init();

    const osMessageQueueAttr_t btn_q_attr = { .name = "ButtonQueue" };
    ButtonQueueHandle = require(osMessageQueueNew(APP_BUTTON_QUEUE_LEN, sizeof(ButtonEvent_t), &btn_q_attr));

    const osMessageQueueAttr_t tx_q_attr = { .name = "TxQueue" };
    TxQueueHandle = require(osMessageQueueNew(APP_TX_QUEUE_LEN, sizeof(TxItem_t), &tx_q_attr));

#ifdef APP_TX_MODE_DUAL_QUEUE
    /* Yalnizca BTN-oncelikli mimaride ek TX FIFO ayirilir. */
    const osMessageQueueAttr_t btn_tx_attr = { .name = "BtnTxQueue" };
    BtnTxQueueHandle = require(osMessageQueueNew(APP_BTN_TX_QUEUE_LEN, sizeof(TxItem_t), &btn_tx_attr));
#endif

    const osMessageQueueAttr_t cmd_q_attr = { .name = "CmdQueue" };
    CmdQueueHandle = require(osMessageQueueNew(APP_CMD_QUEUE_LEN, sizeof(CmdLine_t), &cmd_q_attr));

    const osSemaphoreAttr_t sem_attr = { .name = "TxDoneSem" };
    TxDoneSemHandle = require(osSemaphoreNew(1, 0, &sem_attr));

    const osEventFlagsAttr_t flags_attr = { .name = "RunFlags" };
    RunFlagsHandle = require(osEventFlagsNew(&flags_attr));

    const osThreadAttr_t uart_tx_attr = {
        .name       = "UartTxTask",
        .priority   = APP_PRIO_UART_TX_TASK,
        .stack_size = APP_STACK_UART_TX_TASK,
    };
#ifdef APP_TX_MODE_DUAL_QUEUE
    UartTxTaskHandle = require(osThreadNew(UartTxTask_Run, NULL, &uart_tx_attr));
#else
    (void)require(osThreadNew(UartTxTask_Run, NULL, &uart_tx_attr));
#endif

    const osThreadAttr_t button_attr = {
        .name       = "ButtonTask",
        .priority   = APP_PRIO_BUTTON_TASK,
        .stack_size = APP_STACK_BUTTON_TASK,
    };
    (void)require(osThreadNew(ButtonTask_Run, NULL, &button_attr));

    const osThreadAttr_t telemetry_attr = {
        .name       = "TelemetryTask",
        .priority   = APP_PRIO_TELEMETRY_TASK,
        .stack_size = APP_STACK_TELEMETRY_TASK,
    };
    (void)require(osThreadNew(TelemetryTask_Run, NULL, &telemetry_attr));

    /* RX zinciri en son: CmdQueue hazir olmadan ISR ona yazmasin. */
    CmdRx_Init();
}
