/**
 * app_cmd_rx.h
 *
 * PC->MCU komut kanali: USART1 RX interrupt (byte bazli HAL_UART_Receive_IT)
 * satir biriktirir, LF'de ham satiri CmdQueue'ya birakir. Ayristirma ve CRC
 * kontrolu UartTxTask'ta yapilir (ISR kisa kalsin, newlib ISR'dan cagrilmasin).
 *
 * TX (DMA) ile ayni UART uzerinde calisir; HAL TX (gState) ve RX (RxState)
 * durumlarini ayri tuttugu icin full-duplex kullanim desteklenir.
 */
#ifndef APP_CMD_RX_H
#define APP_CMD_RX_H

void CmdRx_Init(void);

/** HAL_UART_RxCpltCallback'ten cagrilir: bayti isler, RX'i yeniden kollar. */
void CmdRx_HandleByteReceived(void);

/**
 * HAL_UART_ErrorCallback'ten, RX kaynakli hatalarda (ORE/FE/NE/PE) cagrilir.
 * HAL, overrun gibi hatalarda RX IT zincirini durdurur; yeniden kollanmazsa
 * komut kanali (STOP/DUMP dahil) kalici olarak susar.
 */
void CmdRx_OnError(void);

#endif /* APP_CMD_RX_H */
