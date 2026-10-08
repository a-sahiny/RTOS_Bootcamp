#include <stdbool.h>
#include "app_hw.h"
#include "app_cmd_rx.h"
#include "app_config.h"
#include "app_rtos.h"

static uint8_t s_rx_buffer[256] __attribute__((aligned(32)));
static uint16_t s_read_pos;
static CmdLine_t s_line;
static bool      s_discarding;   /* asiri uzun / bozuk satir: LF'ye kadar at */

static void rearm(void)
{
    s_read_pos = 0u;
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0u) {
        SCB_CleanInvalidateDCache_by_Addr((uint32_t *)s_rx_buffer, sizeof(s_rx_buffer));
    }
    /* Circular DMA: HT/TC stay enabled, IDLE delivers short commands. */
    if (HAL_UARTEx_ReceiveToIdle_DMA(&APP_UART_HANDLE, s_rx_buffer,
                                    sizeof(s_rx_buffer)) != HAL_OK) {
        Error_Handler();
    }
}

void CmdRx_Init(void)
{
    s_line.len   = 0u;
    s_discarding = false;
    rearm();
}

static void handle_byte(char c)
{
    if (c == '\n') {
        if (!s_discarding && s_line.len > 0u) {
            /* ISR context: timeout 0. CmdQueue doluysa satir duser; PC tekrar dener. */
#ifdef APP_TX_MODE_DUAL_QUEUE
            /* USART1 and DMA2_Stream2 IRQ priority = 5; configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5.
             * Bu konfig'de CMSIS/FreeRTOS ISR API kullanimi uygundur. */
            if (osMessageQueuePut(CmdQueueHandle, &s_line, 0u, 0u) == osOK) {
                (void)osThreadFlagsSet(UartTxTaskHandle, APP_TX_WORK_READY_FLAG);
            }
#else
            (void)osMessageQueuePut(CmdQueueHandle, &s_line, 0u, 0u);
#endif
        }
        s_line.len   = 0u;
        s_discarding = false;
    } else if (c != '\r' && !s_discarding) {
        if (s_line.len < APP_CMD_LINE_MAX_LEN) {
            s_line.text[s_line.len++] = c;
        } else {
            s_discarding = true;
            s_line.len   = 0u;
        }
    }
}

void CmdRx_RxEvent(void)
{
    /* Read the live DMA position: delayed HT/TC and IDLE callbacks can arrive
       in either order. Callback Size may describe an older position. */
    uint16_t write_pos = (uint16_t)(sizeof(s_rx_buffer) -
                                   __HAL_DMA_GET_COUNTER(APP_UART_HANDLE.hdmarx));
    if (write_pos == sizeof(s_rx_buffer)) {
        write_pos = 0u;
    }
    if (s_read_pos == write_pos) {
        return;
    }
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0u) {
        /* Dedicated, aligned cache lines; the CPU never writes into active RX. */
        SCB_InvalidateDCache_by_Addr((uint32_t *)s_rx_buffer, sizeof(s_rx_buffer));
    }
    while (s_read_pos != write_pos) {
        char c = (char)s_rx_buffer[s_read_pos];
        s_read_pos = (uint16_t)((s_read_pos + 1u) % sizeof(s_rx_buffer));
        handle_byte(c);
    }
}

void CmdRx_OnError(void)
{
    s_line.len   = 0u;
    s_discarding = true;   /* yarim kalan satir bozuk; bir sonraki LF'den devam */
    rearm();
}
