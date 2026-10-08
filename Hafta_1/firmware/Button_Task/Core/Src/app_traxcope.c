#include "app_trace_config.h"

#if APP_TRACE_BACKEND == APP_TRACE_TRAXCOPE
#include "main.h"
#include "app_traxcope.h"
#include "trax.h"
#include "trax_transport.h"
#include <string.h>

extern UART_HandleTypeDef huart6;

static uint8_t s_tx_buffer[TRAX_CFG_TRANSPORT_BUF_SIZE] __attribute__((aligned(32)));
static uint8_t s_rx_dma[128] __attribute__((aligned(32)));
static uint8_t s_rx_ring[1024];
static uint16_t s_rx_read, s_rx_write, s_rx_count;
static volatile uint8_t s_tx_busy;
static volatile uint8_t s_rx_reset;

static void start_receive(void)
{
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0u) {
        SCB_CleanInvalidateDCache_by_Addr((uint32_t *)s_rx_dma, sizeof(s_rx_dma));
    }
    if (HAL_UARTEx_ReceiveToIdle_DMA(&huart6, s_rx_dma, sizeof(s_rx_dma)) != HAL_OK) {
        Error_Handler();
    }
    /* USART6 RX uses normal DMA: consume IDLE or TC, never HT twice. */
    __HAL_DMA_DISABLE_IT(huart6.hdmarx, DMA_IT_HT);
}

void App_Traxcope_Init(void)
{
    if (SystemCoreClock != TRAX_CFG_TIMER_FREQ_HZ || trax_init() != 0) {
        Error_Handler();
    }
}

int trax_transport_init(void)
{
    s_tx_busy = 0u;
    s_rx_read = s_rx_write = s_rx_count = 0u;
    s_rx_reset = 0u;
    start_receive();
    return 0;
}

size_t App_Traxcope_TxFree(void)
{
    return s_tx_busy ? 0u : sizeof(s_tx_buffer);
}

size_t trax_transport_write(const void *data, size_t size)
{
    if (size == 0u || size > sizeof(s_tx_buffer) || s_tx_busy) {
        return 0u;
    }
    /* Only TraxCtrl writes; copy before returning because the SDK reuses its ring. */
    memcpy(s_tx_buffer, data, size);
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0u) {
        SCB_CleanDCache_by_Addr((uint32_t *)s_tx_buffer, (int)((size + 31u) & ~31u));
    }
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    s_tx_busy = 1u;
    HAL_StatusTypeDef status = HAL_UART_Transmit_DMA(&huart6, s_tx_buffer, (uint16_t)size);
    if (status != HAL_OK) {
        s_tx_busy = 0u;
        if (HAL_UART_AbortTransmit(&huart6) != HAL_OK) {
            Error_Handler();
        }
    }
    __set_PRIMASK(mask);
    /* All-or-nothing: the SDK retries the same batch after HAL_BUSY/ERROR. */
    return status == HAL_OK ? size : 0u;
}

void App_Traxcope_TxComplete(void)
{
    s_tx_busy = 0u;
}

void trax_transport_clear_tx(void)
{
    /* Already accepted bytes finish on the wire before the new session.
       There is no queued software TX data to discard. Do not tear a DMA frame. */
}

void App_Traxcope_RxEvent(uint16_t size)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0u) {
        SCB_InvalidateDCache_by_Addr((uint32_t *)s_rx_dma, sizeof(s_rx_dma));
    }
    if (size > sizeof(s_rx_ring) - s_rx_count) {
        s_rx_reset = 1u;
    }
    if (!s_rx_reset) {
        for (uint16_t i = 0u; i < size; ++i) {
            s_rx_ring[s_rx_write] = s_rx_dma[i];
            s_rx_write = (uint16_t)((s_rx_write + 1u) % sizeof(s_rx_ring));
        }
        s_rx_count += size;
    }
    start_receive();
    __set_PRIMASK(mask);
}

size_t trax_transport_read(void *data, size_t size)
{
    uint8_t *dst = data;
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (s_rx_reset) {
        /* A lost command fragment must not be joined to the next command.
           Reset the SDK parser only here, in its owning TraxCtrl task. */
        s_rx_read = s_rx_write = s_rx_count = 0u;
        s_rx_reset = 0u;
        (void)trax_cmd_protocol_init();
    }
    size_t count = size < s_rx_count ? size : s_rx_count;
    for (size_t i = 0u; i < count; ++i) {
        dst[i] = s_rx_ring[s_rx_read];
        s_rx_read = (uint16_t)((s_rx_read + 1u) % sizeof(s_rx_ring));
    }
    s_rx_count -= (uint16_t)count;
    __set_PRIMASK(mask);
    return count;
}

void App_Traxcope_Error(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if ((huart6.ErrorCode & HAL_UART_ERROR_DMA) != 0u) {
        /* This HAL ends both UART states on a DMA fault. */
        if (HAL_UART_Abort(&huart6) != HAL_OK) {
            Error_Handler();
        }
        s_tx_busy = 0u;
    }
    s_rx_reset = 1u;
    if (huart6.RxState == HAL_UART_STATE_READY) {
        start_receive();
    }
    __set_PRIMASK(mask);
}
#endif
