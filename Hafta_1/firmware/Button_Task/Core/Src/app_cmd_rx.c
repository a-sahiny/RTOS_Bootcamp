#include <stdbool.h>
#include "app_hw.h"
#include "app_cmd_rx.h"
#include "app_config.h"
#include "app_rtos.h"

static uint8_t   s_rx_byte;
static CmdLine_t s_line;
static bool      s_discarding;   /* asiri uzun / bozuk satir: LF'ye kadar at */

static void rearm(void)
{
    /* RX hala mesgulse HAL_BUSY doner; zararsiz. */
    (void)HAL_UART_Receive_IT(&APP_UART_HANDLE, &s_rx_byte, 1u);
}

void CmdRx_Init(void)
{
    s_line.len   = 0u;
    s_discarding = false;
    rearm();
}

void CmdRx_HandleByteReceived(void)
{
    char c = (char)s_rx_byte;

    if (c == '\n') {
        if (!s_discarding && s_line.len > 0u) {
            /* ISR context: timeout 0. CmdQueue doluysa satir duser; PC tekrar dener. */
#ifdef APP_TX_MODE_DUAL_QUEUE
            /* USART1 IRQ priority = 5; configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5.
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
    rearm();
}

void CmdRx_OnError(void)
{
    s_line.len   = 0u;
    s_discarding = true;   /* yarim kalan satir bozuk; bir sonraki LF'den devam */
    rearm();
}
