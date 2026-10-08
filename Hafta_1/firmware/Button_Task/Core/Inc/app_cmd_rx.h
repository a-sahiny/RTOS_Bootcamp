/**
 * app_cmd_rx.h
 *
 * PC->MCU komut kanali: USART1 RX circular DMA + IDLE/HT/TC
 * satir biriktirir, LF'de ham satiri CmdQueue'ya birakir. Ayristirma ve CRC
 * kontrolu UartTxTask'ta yapilir (ISR kisa kalsin, newlib ISR'dan cagrilmasin).
 *
 * TX (DMA) ile ayni UART uzerinde calisir; HAL TX (gState) ve RX (RxState)
 * durumlarini ayri tuttugu icin full-duplex kullanim desteklenir.
 */
#ifndef APP_CMD_RX_H
#define APP_CMD_RX_H

void CmdRx_Init(void);

/** HAL_UARTEx_RxEventCallback: DMA'nin yazdigi yeni baytlari bir kez isler. */
void CmdRx_RxEvent(void);

/**
 * HAL_UART_ErrorCallback'ten, RX/DMA durdurulduktan sonra cagrilir.
 * Yarim komutu atar ve RX DMA'yi yeniden baslatir; LF ile tekrar eszamanlanir.
 */
void CmdRx_OnError(void);

#endif /* APP_CMD_RX_H */
