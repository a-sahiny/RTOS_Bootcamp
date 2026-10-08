#ifndef APP_TRACE_H
#define APP_TRACE_H

#include "app_trace_config.h"

#if APP_TRACE_BACKEND == APP_TRACE_TRAXCOPE
#include "app_traxcope.h"
#include "trax.h"
#define App_Trace_Init       App_Traxcope_Init
#define App_Trace_TxComplete App_Traxcope_TxComplete
#define App_Trace_RxEvent    App_Traxcope_RxEvent
#define App_Trace_Error      App_Traxcope_Error
#else
#include "app_systemview.h"
#define App_Trace_Init       App_SystemView_Init
#define App_Trace_TxComplete App_SystemView_TxComplete
#define App_Trace_RxEvent    App_SystemView_RxEvent
#define App_Trace_Error      App_SystemView_Error
#endif

#endif
