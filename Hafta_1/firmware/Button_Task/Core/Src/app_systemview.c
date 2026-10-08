#include "app_trace_config.h"
#if APP_TRACE_BACKEND == APP_TRACE_SYSTEMVIEW
#include "main.h"
#include "app_systemview.h"
#include "SEGGER_SYSVIEW.h"
#include "SEGGER_SYSVIEW_FreeRTOS.h"
#include "SEGGER_SYSVIEW_REC.h"
#include "SEGGER_RTT.h"
#include <string.h>

extern UART_HandleTypeDef huart6;
extern const SEGGER_SYSVIEW_OS_API SYSVIEW_X_OS_TraceAPI;

static uint8_t s_tx_buffer[256] __attribute__((aligned(32)));
static uint8_t s_rx_buffer[32] __attribute__((aligned(32)));
/* RTT leaves one byte unused; hold a complete RX DMA block of commands. */
static uint8_t s_command_buffer[sizeof(s_rx_buffer) + 1u];
static uint16_t s_tx_len;
static volatile uint8_t s_tx_busy;
static uint8_t s_initialized;

static void send_system_description(void)
{
    SEGGER_SYSVIEW_SendSysDesc("N=RTOS Bootcamp,D=STM32F746NG,O=FreeRTOS 10.2.0");
    SEGGER_SYSVIEW_SendSysDesc("I#56=Button_EXTI15_10");
}

void SEGGER_SYSVIEW_Conf(void)
{
    SEGGER_SYSVIEW_Init_Ex(SystemCoreClock, SystemCoreClock,
                          &SYSVIEW_X_OS_TraceAPI, send_system_description,
                          SYSVIEW_SendTaskStates, NULL);
    SEGGER_SYSVIEW_SetRAMBase(0x20000000u);
}

/* Called from tasks and interrupts. Never waits for the UART or a semaphore. */
static void try_send(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (s_initialized && !s_tx_busy) {
        if (s_tx_len == 0u) {
            int count = SYSVIEW_REC_GetOutgoing(s_tx_buffer, sizeof(s_tx_buffer));
            if (count > 0) {
                s_tx_len = (uint16_t)count;
            }
        }
        if (s_tx_len != 0u) {
            /* The buffer occupies whole cache lines; safe if D-cache is enabled later. */
            if ((SCB->CCR & SCB_CCR_DC_Msk) != 0u) {
                SCB_CleanDCache_by_Addr((uint32_t *)s_tx_buffer, sizeof(s_tx_buffer));
            }
            s_tx_busy = 1u;
            if (HAL_UART_Transmit_DMA(&huart6, s_tx_buffer, s_tx_len) != HAL_OK) {
                /* USART6 has one owner. Repair stale TX/DMA state once, without
                   a periodic retry task or consuming another recorder chunk. */
                if (HAL_UART_AbortTransmit(&huart6) != HAL_OK ||
                    HAL_UART_Transmit_DMA(&huart6, s_tx_buffer, s_tx_len) != HAL_OK) {
                    Error_Handler();
                }
            }
        }
    }
    __set_PRIMASK(mask);
}

static HAL_StatusTypeDef start_receive(void)
{
    if (huart6.RxState != HAL_UART_STATE_READY) {
        return HAL_BUSY;
    }
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0u) {
        SCB_CleanInvalidateDCache_by_Addr((uint32_t *)s_rx_buffer, sizeof(s_rx_buffer));
    }
    HAL_StatusTypeDef status = HAL_UARTEx_ReceiveToIdle_DMA(&huart6, s_rx_buffer,
                                                         sizeof(s_rx_buffer));
    if (status != HAL_OK) {
        /* Recovery stays in the receive/error callback; no timer or polling. */
        if (HAL_UART_AbortReceive(&huart6) != HAL_OK) {
            Error_Handler();
        }
        status = HAL_UARTEx_ReceiveToIdle_DMA(&huart6, s_rx_buffer, sizeof(s_rx_buffer));
        if (status != HAL_OK) {
            Error_Handler();
        }
    }
    /* Normal DMA: process only IDLE or a full buffer, never the same bytes twice. */
    __HAL_DMA_DISABLE_IT(huart6.hdmarx, DMA_IT_HT);
    return status;
}

void App_SystemView_Init(void)
{
    /* Timestamp_Init must already have enabled DWT, before any task is created. */
    SEGGER_SYSVIEW_Conf();
    (void)SEGGER_RTT_ConfigDownBuffer(SEGGER_SYSVIEW_GetChannelID(), "SysView",
                                    s_command_buffer, sizeof(s_command_buffer),
                                    SEGGER_RTT_MODE_NO_BLOCK_SKIP);
    s_initialized = 1u;
    if (start_receive() != HAL_OK) {
        Error_Handler();
    }
}

void SEGGER_SYSVIEW_X_OnEventRecorded(unsigned NumBytes)
{
    (void)NumBytes;
    try_send();
}

void App_SystemView_TxComplete(void)
{
    s_tx_len = 0u;
    s_tx_busy = 0u;
    try_send();
}

void App_SystemView_RxEvent(uint16_t size)
{
    uint8_t received[sizeof(s_rx_buffer)];
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0u) {
        SCB_InvalidateDCache_by_Addr((uint32_t *)s_rx_buffer, sizeof(s_rx_buffer));
    }
    /* Normal DMA has stopped. Copy before restarting into the same buffer. */
    memcpy(received, s_rx_buffer, size);
    (void)start_receive();
    for (uint16_t i = 0; i < size; i++) {
        (void)SYSVIEW_REC_ProcessIncoming(&received[i], 1u);
        try_send();
    }
    /* Drain a command burst only after all its bytes (including parameters) arrived. */
    while (SEGGER_RTT_HASDATA(SEGGER_SYSVIEW_GetChannelID())) {
        (void)SEGGER_SYSVIEW_IsStarted();
    }
    __set_PRIMASK(mask);
}

void App_SystemView_Error(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if ((huart6.ErrorCode & HAL_UART_ERROR_DMA) != 0u) {
        /* This HAL ends both UART states on a DMA error, even for RX faults.
           Stop both DMA streams before reusing either buffer. */
        if (HAL_UART_Abort(&huart6) != HAL_OK) {
            Error_Handler();
        }
        s_tx_len = 0u;
        s_tx_busy = 0u;
    }
    /* HAL ends RX on overrun; restore reception without disturbing USART1. */
    if (huart6.RxState == HAL_UART_STATE_READY) {
        (void)start_receive();
    }
    try_send();
    __set_PRIMASK(mask);
}

void App_SystemView_RecordISRExit(void)
{
    /* CMSIS-RTOS2 may call portYIELD_FROM_ISR several times in one IRQ.
       Close the trace once, at the actual end of the peripheral handler. */
    if ((SCB->ICSR & SCB_ICSR_PENDSVSET_Msk) != 0u) {
        SEGGER_SYSVIEW_RecordExitISRToScheduler();
    } else {
        SEGGER_SYSVIEW_RecordExitISR();
    }
}

#endif /* APP_TRACE_BACKEND == APP_TRACE_SYSTEMVIEW */
