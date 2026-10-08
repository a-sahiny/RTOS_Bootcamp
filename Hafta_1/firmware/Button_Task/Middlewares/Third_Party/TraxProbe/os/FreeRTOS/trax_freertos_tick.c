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
 * @file           : trax_freertos_tick.c
 * @brief          : Pre-V10.4 / no-port-hook SysTick ISR slice close path
 ******************************************************************************
 *
 * FreeRTOS < 10.4 (and some vendor trees) do not call traceISR_ENTER from
 * port.c. TraxProbe opens a synthetic SysTick ISR frame from
 * traceTASK_INCREMENT_TICK; this file closes it from the FreeRTOS tick hook
 * without patching Middleware port.c.
 *
 * Default (TRAX_CFG_OWN_FREERTOS_TICK_HOOK == 1): TraxProbe owns
 * vApplicationTickHook (strong) so CubeMX / CMSIS stubs are not required.
 * The kernel is forced to configUSE_TICK_HOOK=1 from trax_rtos_port.h.
 * Uncheck USE_TICK_HOOK in CubeMX and delete any generated
 * vApplicationTickHook — otherwise you get a multiple-definition error.
 * Application tick work goes in trax_app_tick_hook() (weak, overridable).
 *
 * On V10.4+ trax_freertos_on_tick() is a no-op because native port.c
 * traceISR_* already closed the tick ISR.
 *
 * Close policy matches TRAX_CFG_ISR_YIELD_TO_SCHEDULER: if this tick
 * readied a task that can preempt the interrupted one, omit ISR_EXIT
 * so Traxcope draws SysTick → Scheduler → new task. Idle ticks still
 * emit EXIT.
 *
 ******************************************************************************
 */

#include "trax_isr.h"
#include "trax_tid.h"
#include "trax_hw.h"   /* TRAX_PORT_GET_CORE_ID via hw port */

#include "FreeRTOS.h"
#include "task.h"

/* Declared in trax_rtos_port.h; defined in os/common/trax_rtos_tables.c */
extern volatile uint8_t trax_in_tick_isr[TRAX_CFG_CORE_COUNT];
extern volatile uint8_t trax_synthetic_tick_isr[TRAX_CFG_CORE_COUNT];
extern volatile uint8_t trax_tick_yield_pending[TRAX_CFG_CORE_COUNT];

void trax_freertos_on_tick(void)
{
	uint8_t core = (uint8_t)TRAX_PORT_GET_CORE_ID();

	if (trax_synthetic_tick_isr[core] != 0U) {
		TRAX_ISR_END(TRAX_TID_ISR_TICK, trax_tick_yield_pending[core]);
		trax_synthetic_tick_isr[core] = 0U;
		trax_in_tick_isr[core] = 0U;
		trax_tick_yield_pending[core] = 0U;
	}
}

#if defined(__GNUC__) || defined(__clang__) || defined(__CC_ARM) || defined(__ARMCC_VERSION)
#define TRAX_FREERTOS_WEAK  __attribute__((weak))
#elif defined(__ICCARM__)
#define TRAX_FREERTOS_WEAK  __weak
#else
#define TRAX_FREERTOS_WEAK
#endif

#if (TRAX_CFG_OWN_FREERTOS_TICK_HOOK == 1)

TRAX_FREERTOS_WEAK void trax_app_tick_hook(void)
{
}

void vApplicationTickHook(void)
{
	trax_freertos_on_tick();
	trax_app_tick_hook();
}

#elif defined(configUSE_TICK_HOOK) && (configUSE_TICK_HOOK == 1)

/*
 * Opt-out: application owns vApplicationTickHook. Weak fallback so a
 * forgotten stub still links; a strong application definition wins and
 * must call trax_freertos_on_tick().
 */
TRAX_FREERTOS_WEAK void vApplicationTickHook(void)
{
	trax_freertos_on_tick();
}

#endif /* TRAX_CFG_OWN_FREERTOS_TICK_HOOK */

#endif /* TRAX_CFG_RTOS_TYPE == TRAX_RTOS_FREERTOS && TRAX_ENABLE */
