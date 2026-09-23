/**
 * app_hw.h
 *
 * CubeMX'in urettigi UART handle'ina erisim. "Her periferik icin ayri .c/.h"
 * kapaliyken (CoupleFile=false, UART_RTOS_ODEV1 projesi) huart1 main.c'de
 * tanimlanir ve hicbir baslikta bildirilmez; aciksa usart.h de bildirir.
 * Buradaki extern bildirim iki duzende de gecerlidir.
 */
#ifndef APP_HW_H
#define APP_HW_H

#include "main.h"
#include "app_config.h"

extern UART_HandleTypeDef APP_UART_HANDLE;

#endif /* APP_HW_H */
