/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Embedya.
 *
 * TraxProbe porting layer (reference port). Licensed under the Apache License,
 * Version 2.0 (see LICENSE-Apache-2.0.txt). You may copy and modify this file
 * to support additional hardware or RTOS targets.
 *
 * This file depends on TraxProbe core headers, which remain licensed under the
 * TraxProbe Commercial License and are NOT relicensed by this notice.
 */

#include "trax_config_default.h"
#if (TRAX_CFG_RTOS_TYPE == TRAX_RTOS_FREERTOS) && (TRAX_ENABLE)

/**
 ******************************************************************************
 * @file           : trax_freertos_meta.c
 * @brief          : Port-supplied ISR metadata for FreeRTOS-managed ISRs
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 *
 * The FreeRTOS port owns SysTick — it is configured by the kernel via
 * vPortSetupTimerInterrupt() inside xPortStartScheduler(), the handler
 * lives in the kernel's port.c, and our traceISR_ENTER macro is invoked
 * from there (FreeRTOS >= 10.4). The user never touches it.
 *
 * Since the user owns *none* of the SysTick stack, this file owns the
 * metadata too. The TID itself (TRAX_TID_ISR_TICK) is library-reserved in
 * trax_tid.h, so the application doesn't need to declare anything in
 * its trax_config.h either — the contract is fully port-managed.
 *
 * Without this file the lane would render as "ISR_0x4000" in Traxcope.
 *
 * Name is always "SysTick". Priority is the lowest implemented NVIC
 * priority (FreeRTOS programs SysTick/PendSV to that level). Prefer
 * configLIBRARY_LOWEST_INTERRUPT_PRIORITY when the port defines it;
 * otherwise derive from __NVIC_PRIO_BITS (Cortex-M0+/G0 → 3, classic
 * 4-bit NVIC → 15). Override with TRAX_CFG_FREERTOS_TICK_ISR_PRIORITY
 * if your board programs a different value.
 *
 ******************************************************************************
 */

#include "trax_isr.h"

#ifndef TRAX_CFG_FREERTOS_TICK_ISR_PRIORITY
  #if defined(configLIBRARY_LOWEST_INTERRUPT_PRIORITY)
    #define TRAX_CFG_FREERTOS_TICK_ISR_PRIORITY  configLIBRARY_LOWEST_INTERRUPT_PRIORITY
  #elif defined(__NVIC_PRIO_BITS)
    #define TRAX_CFG_FREERTOS_TICK_ISR_PRIORITY  ((1U << (__NVIC_PRIO_BITS)) - 1U)
  #else
    #define TRAX_CFG_FREERTOS_TICK_ISR_PRIORITY  15U
  #endif
#endif

TRAX_ISR_DEFINE(TRAX_TID_ISR_TICK, "SysTick", TRAX_CFG_FREERTOS_TICK_ISR_PRIORITY);

#endif /* TRAX_CFG_RTOS_TYPE == TRAX_RTOS_FREERTOS */
