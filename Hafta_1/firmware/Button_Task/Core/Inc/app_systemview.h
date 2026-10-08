#ifndef APP_SYSTEMVIEW_H
#define APP_SYSTEMVIEW_H

#include <stdint.h>

void App_SystemView_Init(void);
void App_SystemView_TxComplete(void);
void App_SystemView_RxEvent(uint16_t size);
void App_SystemView_Error(void);
void App_SystemView_RecordISRExit(void);

#endif
