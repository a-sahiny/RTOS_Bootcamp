/*
 * SPDX-License-Identifier: LicenseRef-TraxProbe-Commercial
 * Copyright (c) 2026 Embedya. All rights reserved.
 *
 * TraxProbe core engine. CONFIDENTIAL and proprietary to Embedya.
 * Licensed under the TraxProbe Commercial License (see LICENSE-COMMERCIAL.txt).
 * No use, copying, modification, redistribution, decompilation, or reverse
 * engineering is permitted except as expressly authorized by that agreement.
 */

/**
 ******************************************************************************
 * @file           : trax_memory.h
 * @brief          : TraxProbe Heap and Stack Memory Monitoring
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 *
 * Provides:
 *   1. Configuration defaults for heap/stack thresholds (session header)
 *   2. Periodic stack watermark polling via trax_memory_monitor_process()
 *
 * Heap events (traceMALLOC/traceFREE) are handled in trax_rtos_port.h as
 * synchronous trace hooks. This module handles the asynchronous polling of
 * stack high-water marks.
 *
 * USAGE:
 *   Heap total size is auto-detected at runtime via trax_os_get_total_heap_size()
 *   (e.g. configTOTAL_HEAP_SIZE on FreeRTOS). Optional overrides in trax_config.h:
 *
 *   #define TRAX_CFG_HEAP_WARN_BYTES        2048   // default: 2048
 *   #define TRAX_CFG_HEAP_CRITICAL_BYTES    512    // default: 512
 *   #define TRAX_CFG_STACK_WARN_PERCENT     20     // default: 20
 *   #define TRAX_CFG_STACK_CRITICAL_PERCENT 10     // default: 10
 *
 ******************************************************************************
 */

#ifndef TRAX_MEMORY_H_
#define TRAX_MEMORY_H_

#include <stdint.h>
#include "trax_config_default.h"
#include "trax_tid.h"

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 ====================CONFIGURATION DEFAULTS====================================
 ============================================================================*/

#ifndef TRAX_CFG_STACK_MONITOR_INTERVAL_MS
#define TRAX_CFG_STACK_MONITOR_INTERVAL_MS  500
#endif

/** Warning threshold: heap free bytes below this trigger warning zone */
#ifndef TRAX_CFG_HEAP_WARN_BYTES
#define TRAX_CFG_HEAP_WARN_BYTES            2048
#endif

/** Critical threshold: heap free bytes below this trigger danger zone */
#ifndef TRAX_CFG_HEAP_CRITICAL_BYTES
#define TRAX_CFG_HEAP_CRITICAL_BYTES        512
#endif

/** Warning at N% of each task's stack remaining (0 = disabled) */
#ifndef TRAX_CFG_STACK_WARN_PERCENT
#define TRAX_CFG_STACK_WARN_PERCENT         20
#endif

/** Critical at N% of each task's stack remaining (0 = disabled) */
#ifndef TRAX_CFG_STACK_CRITICAL_PERCENT
#define TRAX_CFG_STACK_CRITICAL_PERCENT     10
#endif

/*=============================================================================
 ====================GLOBAL FUNCTION DECLARATIONS==============================
 ============================================================================*/

/**
 * @brief Periodic memory monitor — call from trax_process()
 *
 * Implementation is **per-RTOS** and lives under
 * `os/<RTOS>/trax_<rtos>_memory.c`. The public contract:
 *
 *   - Self-throttles internally to TRAX_CFG_STACK_MONITOR_INTERVAL_MS,
 *     so callers can invoke it at any rate.
 *   - On RTOSes that can introspect per-task stacks, emits
 *     TRAX_TID_STACK_USAGE frames (one per tracked task) at each tick.
 *   - On RTOSes (or RTOS configurations) that cannot, the function is
 *     a no-op. Stack instrumentation simply doesn't appear in the
 *     trace — never a build or link error. Heap events are handled
 *     separately via per-RTOS traceMALLOC / traceFREE hooks.
 *
 * Per-port notes:
 *   FreeRTOS  — requires `INCLUDE_uxTaskGetStackHighWaterMark = 1` in
 *               FreeRTOSConfig.h (otherwise no-op). See
 *               os/FreeRTOS/trax_freertos_memory.c.
 *   BareMetal — no-op (single stack, no task table). See
 *               os/BareMetal/trax_baremetal_memory.c.
 */
void trax_memory_monitor_process(void);

/**
 * @brief Report a task stack overflow event to the host.
 *
 * Asynchronous companion to trax_memory_monitor_process(): the periodic
 * polling captures the high-water-mark trend leading up to a failure,
 * this function captures the failure itself.  Emits a single
 * TRAX_TID_TASK_STACK_OVERFLOW frame carrying the task handle so the host can
 * resolve the task in OsRegistry and surface a "stack overflow on
 * <TaskName>" event in the unified timeline + memory plot.
 *
 * USAGE (FreeRTOS, configCHECK_FOR_STACK_OVERFLOW = 1 or 2 in
 * FreeRTOSConfig.h):
 *
 *     void vApplicationStackOverflowHook(TaskHandle_t xTask,
 *                                        char *pcTaskName) {
 *         trax_report_stack_overflow((uint32_t)xTask);
 *         (void)pcTaskName;
 *         // application-specific halt / reset / log here
 *         configASSERT(0);
 *     }
 *
 * The pcTaskName argument is intentionally not part of this function's
 * signature — the host already has the name in OsRegistry from the
 * matching TRAX_TID_TASK_CREATE event, and falls back to the raw handle if
 * the task wasn't registered before the overflow (e.g. heap-exhausted
 * during xTaskCreate).  This keeps the wire frame fixed-size (no
 * variable-length string) and the function call ISR-safe.
 *
 * Per-port notes:
 *   FreeRTOS  — emits TRAX_TID_TASK_STACK_OVERFLOW. See
 *               os/FreeRTOS/trax_freertos_memory.c.
 *   BareMetal — no-op (no task concept, no overflow detection in the
 *               kernel).  Provided for source-portability so user code
 *               that calls this from a generic fault handler still
 *               links cleanly. See os/BareMetal/trax_baremetal_memory.c.
 *
 * @param task_handle  Task handle (TaskHandle_t cast to uint32_t).  May
 *                     be 0 if the kernel could not identify the task —
 *                     the host renders this as "<unknown task>".
 */
void trax_report_stack_overflow(uint32_t task_handle);

#ifdef __cplusplus
}
#endif

#endif /* TRAX_MEMORY_H_ */