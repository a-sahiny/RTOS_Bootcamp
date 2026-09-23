/**
 * app_hal_callbacks.c
 *
 * HAL "weak" callback'lerinin uygulama override'lari. HAL bunlari weak
 * sembol olarak tanimlar; hangi .c dosyasinda olduklarindan bagimsiz
 * link asamasinda devreye girerler. CubeMX'in urettigi stm32f7xx_it.c'ye
 * dokunmak gerekmez.
 */
#include "main.h"
#include "app_hw.h"
#include "app_config.h"
#include "app_rtos.h"
#include "app_button.h"
#include "app_cmd_rx.h"
#include "app_timestamp.h"

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != APP_UART_INSTANCE) {
        return;
    }
    /* t4: TC (son bit gonderildi) isleniyor. Burada alinir; UartTxTask en
     * dusuk oncelikli oldugu icin uyandiginda alinsaydi, araya giren ust
     * gorevlerin (CPU yuku dahil) suresi S4'e eklenirdi. */
    g_tx_done_cycles = Timestamp_Now();
    (void)osSemaphoreRelease(TxDoneSemHandle);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != APP_UART_INSTANCE) {
        return;
    }
    uint32_t err = huart->ErrorCode;

    /* TX DMA hatasi: UartTxTask'i tam timeout'u beklemeden uyandir. */
    if ((err & HAL_UART_ERROR_DMA) != 0u) {
        g_tx_dma_error = true;
        (void)osSemaphoreRelease(TxDoneSemHandle);
    }
    /* RX hatalari TX'i etkilemez; ama RX IT zincirini durdurabilir. */
    if ((err & (HAL_UART_ERROR_ORE | HAL_UART_ERROR_FE |
                HAL_UART_ERROR_NE  | HAL_UART_ERROR_PE)) != 0u) {
        CmdRx_OnError();
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == APP_UART_INSTANCE) {
        CmdRx_HandleByteReceived();
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == USER_BUTTON_PIN) {
        Button_HandleEXTI();
    }
}

