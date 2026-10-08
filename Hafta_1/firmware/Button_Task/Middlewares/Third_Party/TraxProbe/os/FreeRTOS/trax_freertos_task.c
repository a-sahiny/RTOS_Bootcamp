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
 * @file           : trax_freertos_task.c
 * @brief          : TraxProbe Ctrl Task (FreeRTOS)
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 *
 * Low-priority periodic task that drains the trace ring buffer and processes
 * host commands. Zero overhead on the ring buffer write path -- no callbacks,
 * no notifications added to TRAX_FRAME_ARGS_ATOMIC.
 *
 * FreeRTOS-specific: uses xTaskCreateStatic (falling back to xTaskCreate
 * when configSUPPORT_STATIC_ALLOCATION == 0) and vTaskDelayUntil
 * (requires INCLUDE_vTaskDelayUntil == 1).
 * Other RTOS ports provide their own task creation mechanism.
 *
 ******************************************************************************
 */

#include "internal/trax_task.h"
#include "trax.h"
#include "FreeRTOS.h"
#include "task.h"
#include "../common/trax_rtos_tables.h"

/*=============================================================================
 ====================LOCAL MACRO DEFINITIONS===================================
 ============================================================================*/

#ifndef TRAX_CFG_CTRL_TASK_PRIORITY
#define TRAX_CFG_CTRL_TASK_PRIORITY      1
#endif

#ifndef TRAX_CFG_CTRL_TASK_STACK_SIZE
#define TRAX_CFG_CTRL_TASK_STACK_SIZE    256
#endif

#ifndef TRAX_CFG_CTRL_TASK_PERIOD_MS
#define TRAX_CFG_CTRL_TASK_PERIOD_MS     10
#endif

#define TRAX_TASK_NAME              "TraxCtrl"

/*=============================================================================
 ====================LOCAL VARIABLES===========================================
 ============================================================================*/

/* Preferred path: statically-allocated TCB + stack, so the Ctrl Task never
 * touches (or fragments) the application heap and cannot fail at runtime.
 * Falls back to xTaskCreate() when configSUPPORT_STATIC_ALLOCATION == 0 —
 * the guard reads the value FreeRTOS.h installed (this TU includes
 * FreeRTOS.h above, so the default is already resolved here, unlike in the
 * trace-hook header which is parsed mid-FreeRTOSConfig.h). */
#if (configSUPPORT_STATIC_ALLOCATION == 1)
static StaticTask_t task_tcb;
static StackType_t  task_stack[TRAX_CFG_CTRL_TASK_STACK_SIZE];
#endif

/*=============================================================================
 ====================LOCAL FUNCTION DECLARATION================================
 ============================================================================*/

static void trax_task_func(void *pvParameters);

/*
 * Back-fill the task table with tasks that already existed before tracing
 * began.
 *
 * traceTASK_CREATE only fires for tasks created *after* trax_os_init() readies
 * the table free-list. Every kernel/system task (IDLE0/IDLE1, ipc0/ipc1,
 * esp_timer, Tmr Svc, the application main task, ...) is created during RTOS
 * startup — long before trax_init() runs — so their create hooks were dropped.
 * Without this back-fill the host receives no name/identity for them, renders
 * raw "Task 0x...." lanes, and (critically) cannot recognise the per-core idle
 * tasks, so CPU utilisation pins at 100%.
 *
 * uxTaskGetSystemState() (available only when configUSE_TRACE_FACILITY == 1)
 * returns the live roster {handle, name, priority} in a single snapshot; we
 * register each through the same path traceTASK_CREATE uses, so the next
 * SESSION_START frame ships the complete roster. Called once from
 * trax_task_init() while the table is still empty, so no de-dup is required;
 * TraxCtrl is created immediately afterwards and self-registers via its own
 * create hook.
 *
 * If configUSE_TRACE_FACILITY is disabled the enumeration API is unavailable;
 * the seed becomes a no-op and behaviour degrades gracefully to the previous
 * "only live-created tasks are named" state.
 */
#if (configUSE_TRACE_FACILITY == 1)
static void trax_seed_existing_tasks(void)
{
	UBaseType_t count = uxTaskGetNumberOfTasks();
	if (count == 0U) {
		return;
	}

	TaskStatus_t *status =
		(TaskStatus_t *)pvPortMalloc((size_t)count * sizeof(TaskStatus_t));
	if (status == NULL) {
		return;
	}

	UBaseType_t got = uxTaskGetSystemState(status, count, NULL);
	for (UBaseType_t i = 0U; i < got; i++) {
		(void)trax_rtos_task_create(
			(uint32_t)(uintptr_t)status[i].xHandle,
			(uint32_t)status[i].uxCurrentPriority,
			0U, /* total stack size is not exposed by TaskStatus_t */
			status[i].pcTaskName);
	}

	vPortFree(status);
}
#else
static inline void trax_seed_existing_tasks(void) {}
#endif

/*=============================================================================
 ====================GLOBAL FUNCTION IMPLEMENTATION============================
 ============================================================================*/

void trax_task_init(void)
{
	/* Register tasks that predate tracing (IDLE0/1, ipc, esp_timer, main, ...)
	 * before spawning TraxCtrl, so the SESSION_START snapshot carries the full
	 * roster: the host can then name every lane and classify the idle tasks. */
	trax_seed_existing_tasks();

#if (configSUPPORT_STATIC_ALLOCATION == 1)
	(void)xTaskCreateStatic(
		trax_task_func,
		TRAX_TASK_NAME,
		TRAX_CFG_CTRL_TASK_STACK_SIZE,
		NULL,
		TRAX_CFG_CTRL_TASK_PRIORITY,
		task_stack,
		&task_tcb);
#else
	(void)xTaskCreate(
		trax_task_func,
		TRAX_TASK_NAME,
		TRAX_CFG_CTRL_TASK_STACK_SIZE,
		NULL,
		TRAX_CFG_CTRL_TASK_PRIORITY,
		NULL);
#endif
}

/*=============================================================================
 ====================LOCAL FUNCTION IMPLEMENTATION=============================
 ============================================================================*/

static void trax_task_func(void *pvParameters)
{
	(void)pvParameters;

	TickType_t xLastWakeTime = xTaskGetTickCount();

	for (;;) {
		trax_process();

		vTaskDelayUntil(&xLastWakeTime,
			pdMS_TO_TICKS(TRAX_CFG_CTRL_TASK_PERIOD_MS));
	}
}


#endif /* TRAX_CFG_RTOS_TYPE == TRAX_RTOS_FREERTOS */
