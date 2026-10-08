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
#include "app_systemview.h"

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART6) {
        App_SystemView_TxComplete();
        return;
    }
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
    if (huart->Instance == USART6) {
        App_SystemView_Error();
        return;
    }
    if (huart->Instance != APP_UART_INSTANCE) {
        return;
    }
    uint32_t err = huart->ErrorCode;

    /* Bu HAL, RX veya TX DMA hatasinda iki UART durumunu da sonlandirir.
       Tamponlar yeniden kullanilmadan once iki DMA akisini da durdur. */
    if ((err & HAL_UART_ERROR_DMA) != 0u) {
        if (HAL_UART_Abort(huart) != HAL_OK) {
            Error_Handler();
        }
        g_tx_dma_error = true;
        (void)osSemaphoreRelease(TxDoneSemHandle);
    }
    /* UART RX hatalari yalnizca RX'i durdurur; TX aktarimi devam eder. */
    if ((err & (HAL_UART_ERROR_ORE | HAL_UART_ERROR_FE | HAL_UART_ERROR_DMA |
                HAL_UART_ERROR_NE  | HAL_UART_ERROR_PE)) != 0u) {
        CmdRx_OnError();
    }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART6) {
        App_SystemView_RxEvent(Size);
    } else if (huart->Instance == APP_UART_INSTANCE) {
        CmdRx_RxEvent();
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == USER_BUTTON_PIN) {
        Button_HandleEXTI();
    }
}

