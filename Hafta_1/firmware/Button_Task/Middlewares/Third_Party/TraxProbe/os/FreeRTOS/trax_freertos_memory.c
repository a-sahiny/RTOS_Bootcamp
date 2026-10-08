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
 * @file           : trax_freertos_memory.c
 * @brief          : FreeRTOS implementation of trax_memory_monitor_process()
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 *
 * Periodically polls each tracked task's stack high-water mark via
 * uxTaskGetStackHighWaterMark() and emits TRAX_TID_STACK_USAGE frames.
 *
 * REQUIRES (in App/Config/FreeRTOSConfig.h):
 *     #define INCLUDE_uxTaskGetStackHighWaterMark   1
 *
 * If that include macro is unset or 0, the body collapses to nothing
 * — no frames, no warnings, no link errors. Stack instrumentation
 * simply doesn't appear in the trace. This avoids forcing every
 * TraxProbe consumer to enable a kernel optional feature they may
 * otherwise not need (uxTaskGetStackHighWaterMark adds a per-TCB cost
 * because the kernel has to fill the stack with a known pattern at
 * task creation time).
 *
 ******************************************************************************
 */

#include "internal/trax_memory.h"
#include "trax_session.h"
#include "trax_frame.h"
#include "trax_timestamp.h"   /* TRAX_FRAME_TIMEPACKED_GET, trax_timebase */
#include "../common/trax_rtos_tables.h"

#include "FreeRTOS.h"
#include "task.h"

#ifndef TRAX_CFG_MAX_RTOS_TASKS
#define TRAX_CFG_MAX_RTOS_TASKS 16
#endif

#if (defined(INCLUDE_uxTaskGetStackHighWaterMark) && (INCLUDE_uxTaskGetStackHighWaterMark == 1))
static TickType_t s_last_poll_tick = 0;
#endif

void trax_memory_monitor_process(void)
{
#if (defined(INCLUDE_uxTaskGetStackHighWaterMark) && (INCLUDE_uxTaskGetStackHighWaterMark == 1))
    if (!TRAX_IS_SESSION_ACTIVE()) {
        return;
    }

    TickType_t now = xTaskGetTickCount();
    if ((now - s_last_poll_tick) < pdMS_TO_TICKS(TRAX_CFG_STACK_MONITOR_INTERVAL_MS)) {
        return;
    }
    s_last_poll_tick = now;

    uint32_t handles[TRAX_CFG_MAX_RTOS_TASKS];
    uint8_t count = trax_os_get_task_handles(handles, TRAX_CFG_MAX_RTOS_TASKS);

    for (uint8_t i = 0; i < count; i++) {
        TaskHandle_t task = (TaskHandle_t)handles[i];
        UBaseType_t unused_words = uxTaskGetStackHighWaterMark(task);
        TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_STACK_USAGE,
            handles[i], (uint32_t)unused_words);
    }
#endif
}

/*
 * trax_report_stack_overflow() — async failure event for vApplicationStackOverflowHook.
 *
 * Three side effects, all ISR/critical-section safe:
 *
 *   1. Latch TRAX_TASK_FLAG_OVERFLOWED into the task's slot in the
 *      common task table.  This is the breadcrumb that lets the host
 *      ship "this task already overflowed before you connected" in the
 *      SESSION_START memory snapshot on a future reconnect, even though
 *      the original wire frame is long gone.  Bounded O(N) scan over
 *      TRAX_CFG_MAX_RTOS_TASKS — fine from a fault path that's about to
 *      configASSERT(0) anyway.
 *
 *   2. Latch the (tick_overflow_cntr, timepacked) pair captured *now*
 *      so the host can decode the exact source-time of the overflow on
 *      a future reconnect, not just "before you connected".  Same
 *      first-write-wins rule as set_flag — both are sticky.  The reads
 *      from trax_timebase.tick_overflow_cntr and TRAX_FRAME_TIMEPACKED_GET()
 *      mirror the pair the per-frame TRAX_FRAME_ARGS_ATOMIC writes for
 *      the live event in step 3, so the host reconstructs both through
 *      the same TimerConfig path.
 *
 *   3. Emit a TRAX_TID_TASK_STACK_OVERFLOW frame for any currently-connected
 *      host (streaming-gated by TRAX_FRAME_ARGS_ATOMIC's internal
 *      TRAX_IS_SESSION_ACTIVE() check, so an inactive session silently
 *      drops the event with no extra cost).
 *
 * Independent of INCLUDE_uxTaskGetStackHighWaterMark — the kernel's
 * configCHECK_FOR_STACK_OVERFLOW path runs from the context-switch
 * regardless of HWM polling, and we want to surface the overflow even
 * on builds that didn't enable polling.
 *
 * Cost: one O(N) scan + one TIMEPACKED read + one TRAX_FRAME_ARGS_ATOMIC,
 * ~2us total, runs from vApplicationStackOverflowHook (which is itself
 * called with interrupts disabled inside the kernel's context-switch
 * critical section) — the atomic variant is mandatory because we cannot
 * afford a nested context switch while reporting that a context switch
 * detected corruption.
 */
void trax_report_stack_overflow(uint32_t task_handle)
{
    /* Capture the timestamp BEFORE emitting the live frame so the latched
     * snapshot pair matches what the host sees on the wire to within
     * read-back jitter (a few hundred ns). */
    uint32_t ovf  = trax_timebase.tick_overflow_cntr;
    uint32_t tpkd = TRAX_FRAME_TIMEPACKED_GET();

    trax_rtos_task_set_flag(task_handle, TRAX_TASK_FLAG_OVERFLOWED);
    trax_rtos_task_latch_overflow(task_handle, ovf, tpkd);
    TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_TASK_STACK_OVERFLOW, task_handle);
}

/*
 * vApplicationStackOverflowHook — weak default so that enabling
 * configCHECK_FOR_STACK_OVERFLOW > 0 in FreeRTOSConfig.h "just works"
 * with the trax library, with no extra application code required.
 *
 * Without this default, FreeRTOS's stack-check macros (in
 * include/stack_macros.h) would emit a call to vApplicationStackOverflowHook,
 * the linker would fail to find a definition, and the build would die
 * with the very confusing "undefined reference" + "dangerous relocation"
 * error pair.  That used to push every trax-using project to either
 * (a) leave configCHECK_FOR_STACK_OVERFLOW = 0 and lose overflow
 *     detection, or
 * (b) hand-write a one-line stub that called trax_report_stack_overflow().
 * Doing it once here removes that boilerplate.
 *
 * Behaviour:
 *
 *   1. Latch the per-task overflow flag + emit TRAX_TID_TASK_STACK_OVERFLOW.
 *   2. configASSERT(0) — same default a panic-on-overflow application
 *      would do.  The kernel calls this hook with interrupts disabled
 *      from the context-switch critical section; returning from it would
 *      let the corrupted task resume, which is virtually never what you
 *      want.  The configASSERT(...) macro in your FreeRTOSConfig.h
 *      decides what panic actually means (typical: disable IRQs + spin).
 *
 * To customise (e.g. log to a UART, light a fault LED, reboot via NVIC):
 * define your own non-weak vApplicationStackOverflowHook in your app
 * code and the linker will pick yours over this one.  Be sure to call
 * trax_report_stack_overflow((uint32_t)xTask) yourself so the host still
 * gets the timeline event + snapshot latch.
 */
#if (defined(configCHECK_FOR_STACK_OVERFLOW) && (configCHECK_FOR_STACK_OVERFLOW > 0))

#if defined(__GNUC__) || defined(__clang__) || defined(__CC_ARM) || defined(__ARMCC_VERSION)
__attribute__((weak))
#elif defined(__ICCARM__)
__weak
#endif
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)pcTaskName;
    trax_report_stack_overflow((uint32_t)xTask);
    configASSERT(0);
}

#endif /* configCHECK_FOR_STACK_OVERFLOW > 0 */

/*
 * vApplicationGetIdleTaskMemory — weak default for static allocation.
 *
 * FreeRTOS requires this callback when configSUPPORT_STATIC_ALLOCATION == 1.
 * V11+ can satisfy it via configKERNEL_PROVIDED_STATIC_MEMORY (kernel-owned
 * strong symbol). V10.x has no such option, so without a definition the
 * link fails with "undefined reference to vApplicationGetIdleTaskMemory"
 * (and the confusing ARM/Thumb "dangerous relocation" follow-on).
 *
 * Provide a weak default here so TraxProbe consumers keep a single
 * FreeRTOSConfig across V10/V11: KERNEL_PROVIDED=1 remains valid for V11,
 * and on V10 this weak symbol fills the gap. An application that supplies
 * its own (strong) implementation wins at link time, as does the V11
 * kernel-provided strong symbol when KERNEL_PROVIDED=1.
 *
 * Signature: V10 uses uint32_t * for the stack-size out-param; V11 uses
 * configSTACK_DEPTH_TYPE *. Match TRAX_CFG_FREERTOS_VERSION (must track
 * the linked kernel — same contract as the rest of this port).
 */
#if (defined(configSUPPORT_STATIC_ALLOCATION) && (configSUPPORT_STATIC_ALLOCATION == 1))

#if (TRAX_CFG_FREERTOS_VERSION >= TRAX_FREERTOS_VERSION(11, 0, 0))
typedef configSTACK_DEPTH_TYPE trax_idle_stack_size_t;
#else
typedef uint32_t trax_idle_stack_size_t;
#endif

#if defined(__GNUC__) || defined(__clang__) || defined(__CC_ARM) || defined(__ARMCC_VERSION)
__attribute__((weak))
#elif defined(__ICCARM__)
__weak
#endif
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   trax_idle_stack_size_t *pulIdleTaskStackSize)
{
    static StaticTask_t xIdleTaskTCB;
    static StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE];

    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = &uxIdleTaskStack[0];
    *pulIdleTaskStackSize = (trax_idle_stack_size_t)configMINIMAL_STACK_SIZE;
}

#if (defined(configUSE_TIMERS) && (configUSE_TIMERS == 1))
#if defined(__GNUC__) || defined(__clang__) || defined(__CC_ARM) || defined(__ARMCC_VERSION)
__attribute__((weak))
#elif defined(__ICCARM__)
__weak
#endif
void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t **ppxTimerTaskStackBuffer,
                                    trax_idle_stack_size_t *pulTimerTaskStackSize)
{
    static StaticTask_t xTimerTaskTCB;
    static StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];

    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = &uxTimerTaskStack[0];
    *pulTimerTaskStackSize = (trax_idle_stack_size_t)configTIMER_TASK_STACK_DEPTH;
}
#endif /* configUSE_TIMERS == 1 */

#endif /* configSUPPORT_STATIC_ALLOCATION == 1 */

/*
 * trax_os_get_task_stack_hwm_words() — port-supplied implementation of
 * the platform HWM accessor declared in os/common/trax_rtos_tables.h.
 *
 * Used by trax_os_write_task_meta() at session start to stamp each
 * per-task wire record with the current high-water-mark, so the host's
 * Memory View can show real headroom on connect instead of waiting for
 * the next 500-ms stack-monitor poll.
 *
 * Returns 0 when INCLUDE_uxTaskGetStackHighWaterMark is disabled in
 * FreeRTOSConfig.h — the host's table renders 0 as "n/a (kernel HWM
 * polling disabled)".
 *
 * NOT ISR-safe: caller must run from task context (matches the contract
 * declared in trax_rtos_tables.h).
 */
uint16_t trax_os_get_task_stack_hwm_words(uint32_t handle)
{
#if (defined(INCLUDE_uxTaskGetStackHighWaterMark) && (INCLUDE_uxTaskGetStackHighWaterMark == 1))
    if (handle == 0U) {
        return 0U;
    }
    UBaseType_t unused = uxTaskGetStackHighWaterMark((TaskHandle_t)handle);
    return (unused > UINT16_MAX) ? UINT16_MAX : (uint16_t)unused;
#else
    (void)handle;
    return 0U;
#endif
}

#endif /* TRAX_CFG_RTOS_TYPE == TRAX_RTOS_FREERTOS */
