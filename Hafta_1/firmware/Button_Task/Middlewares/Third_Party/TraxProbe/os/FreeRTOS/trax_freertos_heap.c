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
 * @file           : trax_freertos_heap.c
 * @brief          : FreeRTOS heap query implementations + boot-time counters
 * @version        : 1.1.0
 ******************************************************************************
 * @attention
 *
 * FreeRTOS-specific implementations of the platform heap query API
 * declared in os/common/trax_rtos_tables.h.
 *
 * Also owns the runtime heap-stat counters (g_trax_heap_*_count) that are
 * incremented from the FreeRTOS trace macros (traceMALLOC / traceFREE /
 * traceMALLOC_FAILED in os/FreeRTOS/trax_rtos_port.h) and harvested at
 * session start by trax_meta_tx_send_start() so the host can present
 * accurate "since boot" totals when connecting to an already-running
 * target.  Without this, a mid-flight connection would see all
 * historical counters as 0 (delta-only) and the Memory View table would
 * lie about the firmware's actual heap health.
 *
 * Other RTOS ports provide their own implementations in their respective
 * os/<RTOS>/ directories.
 *
 ******************************************************************************
 */

#include "../common/trax_rtos_tables.h"
#include "FreeRTOS.h"
#include "task.h"
#include "portable.h"
#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"   /* heap_caps_get_total_size() — ESP-IDF owns the heap */
#endif

/*=============================================================================
 ====================FEATURE DEFAULTS==========================================
 ============================================================================*/
/* vPortGetHeapStats() was added to heap_4/5 in FreeRTOS V10.3.0.  Default
 * the knob from the user-declared kernel version (TRAX_CFG_FREERTOS_VERSION,
 * see config/trax_config_rtos.h) so V10.2.x builds do not hit an undefined
 * reference; users on heap_1/2/3 with newer kernels still override to 0
 * manually (the heap variant is not knowable at preprocessing time). */
#ifndef TRAX_CFG_HEAP_HAS_STATS
	#if (TRAX_CFG_FREERTOS_VERSION >= TRAX_FREERTOS_VERSION(10, 3, 0))
		#define TRAX_CFG_HEAP_HAS_STATS  1
	#else
		#define TRAX_CFG_HEAP_HAS_STATS  0
	#endif
#endif

/*=============================================================================
 ====================RUNTIME COUNTERS==========================================
 ============================================================================*/
/* Free-running boot-time counters.  Incremented from traceMALLOC /
 * traceFREE / traceMALLOC_FAILED in os/FreeRTOS/trax_rtos_port.h.  Plain
 * ++ is used: a missed count under heavy SMP contention is acceptable
 * for a health-stat counter (trace-correctness counters use the
 * trax-buffer critical section).  Volatile so the increment is not
 * optimised away even when the trace macros are compiled with all
 * options inlined. */
volatile uint32_t g_trax_heap_alloc_count      = 0U;
volatile uint32_t g_trax_heap_free_count       = 0U;
volatile uint32_t g_trax_heap_alloc_fail_count = 0U;

/*=============================================================================
 ====================ACCESSOR IMPLEMENTATIONS==================================
 ============================================================================*/
uint32_t trax_os_get_free_heap_size(void)
{
	return (uint32_t)xPortGetFreeHeapSize();
}

uint32_t trax_os_get_total_heap_size(void)
{
#if defined(ESP_PLATFORM)
	/* ESP-IDF does not compile FreeRTOS's heap_*.c — the kernel heap IS the
	 * system heap, owned by the heap component. configTOTAL_HEAP_SIZE here
	 * expands to (&_heap_end - &_heap_start), linker symbols private to
	 * ESP-IDF's own port TUs and not linkable from this file, so query the
	 * heap component directly for a truthful total instead. */
	return (uint32_t)heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
#elif defined(configTOTAL_HEAP_SIZE)
	return (uint32_t)(configTOTAL_HEAP_SIZE);
#else
	return 0U;
#endif
}

uint32_t trax_os_get_min_ever_free_heap_size(void)
{
	/* xPortGetMinimumEverFreeHeapSize() is provided by heap_2/4/5.  For
	 * heap_1 (no free) and heap_3 (newlib wrapper) the symbol is absent;
	 * users on those variants should compile this TU with
	 * TRAX_CFG_HEAP_HAS_MIN_EVER=0 to suppress the call and return the
	 * convention-zero "unknown / not supported" sentinel. */
#if !defined(TRAX_CFG_HEAP_HAS_MIN_EVER) || (TRAX_CFG_HEAP_HAS_MIN_EVER == 1)
	return (uint32_t)xPortGetMinimumEverFreeHeapSize();
#else
	return 0U;
#endif
}

uint32_t trax_os_get_largest_free_heap_block(void)
{
#if defined(ESP_PLATFORM)
	/* ESP-IDF does not implement FreeRTOS's vPortGetHeapStats() (a heap_4/5
	 * function); the heap component exposes the largest free block directly.
	 * MALLOC_CAP_DEFAULT matches the capability set xPortGetFreeHeapSize()
	 * reports against, so "free" and "largest block" are consistent. */
	return (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
#elif (TRAX_CFG_HEAP_HAS_STATS == 1)
	/* vPortGetHeapStats() is provided by heap_4/5 on FreeRTOS >= V10.3.0
	 * (the version-derived default above handles the kernel side).  It
	 * calls vTaskSuspendAll() + taskENTER_CRITICAL() internally, so it is
	 * safe from task context (which is where trax_meta_tx_send_start()
	 * runs) but MUST NOT be called from an ISR.  Users on heap_1/2/3
	 * should compile this TU with TRAX_CFG_HEAP_HAS_STATS=0 — the host
	 * renders the convention-zero return as "fragmentation: n/a". */
	HeapStats_t stats;
	vPortGetHeapStats(&stats);
	return (uint32_t)stats.xSizeOfLargestFreeBlockInBytes;
#else
	return 0U;
#endif
}

uint32_t trax_os_get_heap_alloc_count(void)
{
	return g_trax_heap_alloc_count;
}

uint32_t trax_os_get_heap_free_count(void)
{
	return g_trax_heap_free_count;
}

uint32_t trax_os_get_heap_alloc_fail_count(void)
{
	return g_trax_heap_alloc_fail_count;
}

#endif /* TRAX_CFG_RTOS_TYPE == TRAX_RTOS_FREERTOS */
