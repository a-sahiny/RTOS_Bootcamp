#ifndef APP_TRAXCOPE_H
#define APP_TRAXCOPE_H

#include <stddef.h>
#include <stdint.h>

void App_Traxcope_Init(void);
void App_Traxcope_TxComplete(void);
void App_Traxcope_RxEvent(uint16_t size);
void App_Traxcope_Error(void);
size_t App_Traxcope_TxFree(void);

#endif
