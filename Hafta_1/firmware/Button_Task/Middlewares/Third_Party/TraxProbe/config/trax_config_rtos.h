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
 * @file           : trax_config_rtos.h
 * @brief          : TRAX RTOS Configuration
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 *
 * This file contains RTOS-related configuration:
 *   - RTOS type selection (BareMetal / FreeRTOS / ...)
 *   - RTOS version reporting
 *   - Task name length for table storage
 *   - Ctrl Task parameters (priority, stack, period)
 *
 * THESE VALUES MUST BE DEFINED BY USER in App/Config/trax_config.h!
 *
 * DO NOT MODIFY THIS FILE!
 * To configure, define values in App/Config/trax_config.h
 *
 ******************************************************************************
 */

#ifndef TRAX_CONFIG_RTOS_H_
#define TRAX_CONFIG_RTOS_H_

/*=============================================================================
====================RTOS TYPE CONFIGURATION=====================================
============================================================================*/

/**
 * @brief RTOS Type
 *
 * Specifies which RTOS is being used. Sent to Traxcope in the START_TRACE frame.
 * Currently supported values (defined in trax_tid.h):
 *   TRAX_RTOS_NONE      (0)   - Bare metal (default)
 *   TRAX_RTOS_FREERTOS  (1)   - FreeRTOS (enables task integration)
 *
 * Set explicitly in App/Config/trax_config.h:
 *   #define TRAX_CFG_RTOS_TYPE  TRAX_RTOS_FREERTOS
 */
#ifndef TRAX_CFG_RTOS_TYPE
    #define TRAX_CFG_RTOS_TYPE  TRAX_RTOS_NONE
#endif

/*=============================================================================
====================RTOS VERSION CONFIGURATION==================================
============================================================================*/

/**
 * @brief FreeRTOS kernel version encoder + "not set" sentinel.
 *
 * The FreeRTOS port (os/FreeRTOS/trax_rtos_port.h) needs to know the kernel
 * version at PREPROCESSING time: several trace-hook signatures and kernel
 * struct fields changed across releases, and the version CANNOT be
 * auto-detected — the port header is parsed while FreeRTOSConfig.h is being
 * processed, long before task.h defines tskKERNEL_VERSION_MAJOR/MINOR/BUILD.
 * (Percepio's TraceRecorder has the same constraint and the same solution:
 * a user-supplied TRC_CFG_FREERTOS_VERSION.)
 *
 * FreeRTOS users MUST therefore set, in App/Config/trax_config.h:
 *
 *   #define TRAX_CFG_FREERTOS_VERSION   TRAX_FREERTOS_VERSION(11, 2, 0)
 *
 * using the major/minor/patch of the kernel actually compiled into the
 * firmware (see tskKERNEL_VERSION_NUMBER in FreeRTOS's include/task.h).
 * The FreeRTOS port fails the build with a clear #error when the value is
 * missing or below the supported floor (V10.2.0).
 *
 * The value doubles as the source for the TRAX_CFG_OS_VER_* metadata below,
 * so the host receives the true kernel version in the START_TRACE frame.
 */
#define TRAX_FREERTOS_VERSION(major, minor, patch) \
    ((major) * 100000L + (minor) * 1000L + (patch))

#define TRAX_FREERTOS_VERSION_NOT_SET  0

#ifndef TRAX_CFG_FREERTOS_VERSION
    #define TRAX_CFG_FREERTOS_VERSION  TRAX_FREERTOS_VERSION_NOT_SET
#endif

/**
 * @brief OS/RTOS version reported in the START_TRACE metadata frame.
 *
 * For FreeRTOS these are derived from TRAX_CFG_FREERTOS_VERSION by the port
 * (os/FreeRTOS/trax_rtos_port.h) — do not set them separately.
 * For other OS types, override explicitly in App/Config/trax_config.h.
 *
 * Default: 0.0.0 (unknown / bare-metal)
 */
#ifndef TRAX_CFG_OS_VER_MAJOR
    #define TRAX_CFG_OS_VER_MAJOR  0
#endif

#ifndef TRAX_CFG_OS_VER_MINOR
    #define TRAX_CFG_OS_VER_MINOR  0
#endif

#ifndef TRAX_CFG_OS_VER_PATCH
    #define TRAX_CFG_OS_VER_PATCH  0
#endif

/*=============================================================================
====================RTOS TABLE CONFIGURATION====================================
============================================================================*/

/**
 * @brief Maximum RTOS task name length stored in the table (bytes, including null)
 *
 * Decoupled from the RTOS-specific value (e.g., FreeRTOS configMAX_TASK_NAME_LEN).
 * Set this to match or exceed the RTOS's name length if exact names are desired,
 * as a LITERAL:
 *   #define TRAX_CFG_RTOS_TASK_NAME_MAX  16
 * Do not alias the FreeRTOS macro itself — os/common/trax_rtos_tables.c sizes
 * its name field from this value and does not include FreeRTOS.h, so
 * configMAX_TASK_NAME_LEN is undeclared there and the build fails.
 *
 * Default: 16
 */
#ifndef TRAX_CFG_RTOS_TASK_NAME_MAX
#define TRAX_CFG_RTOS_TASK_NAME_MAX  16
#endif

/*=============================================================================
====================CTRL TASK CONFIGURATION (RTOS ONLY)=========================
============================================================================*/

/**
 * @brief Trax Ctrl Task priority (RTOS mode only)
 *
 * The Ctrl Task drains the ring buffer and processes host commands.
 *
 * TRAXPROBE OBSERVABILITY CONTRACT
 * --------------------------------
 *   Library promise : zero perturbation of application real-time behaviour.
 *                     The write path is lock-free; the drain runs as an
 *                     ordinary RTOS task, NOT from ISR context, NOT via
 *                     hidden priority escalation, NOT from the tick hook.
 *   User promise   : give the Ctrl Task enough CPU to drain. If the
 *                     application starves it (e.g. CPU-bound tasks at
 *                     higher priority that never block), frames will queue
 *                     in the ring and eventually be dropped. The host
 *                     surfaces the loss so the user knows it happened —
 *                     the library will never quietly perturb scheduling
 *                     to "save" the trace.
 *
 * Default of 1 (just above IDLE) reflects that contract: the library is
 * intentionally a low-priority, well-behaved citizen. Raise this in
 * App/Config/trax_config.h when (and only when) your application has no
 * idle slack and you accept that the drain task will now compete with
 * your real workload — that competition will be visible in the trace.
 *
 * Valid range: 1 .. configMAX_PRIORITIES - 1
 *
 * Default: 1 (just above idle)
 */
#ifndef TRAX_CFG_CTRL_TASK_PRIORITY
#define TRAX_CFG_CTRL_TASK_PRIORITY      1
#endif

/**
 * @brief Trax Ctrl Task stack size in StackType_t words
 *
 * Default: 256 words (1 KB on 32-bit platforms)
 */
#ifndef TRAX_CFG_CTRL_TASK_STACK_SIZE
#define TRAX_CFG_CTRL_TASK_STACK_SIZE    256
#endif

/**
 * @brief Trax Ctrl Task wake period in milliseconds
 *
 * How often the task wakes to drain the ring buffer and check for
 * host commands. Lower values reduce latency but increase scheduling
 * overhead. At 921600 baud UART, 10 ms allows ~921 bytes per period.
 *
 * Default: 10 ms
 */
#ifndef TRAX_CFG_CTRL_TASK_PERIOD_MS
#define TRAX_CFG_CTRL_TASK_PERIOD_MS     10
#endif

/*=============================================================================
====================ISR → SCHEDULER (YIELD FROM ISR)===========================
============================================================================*/

/**
 * @brief Collapse ISR exit into the pending context switch (default: 1)
 *
 * Cortex-M exception return always restores the interrupted thread context
 * before a same-or-lower-priority PendSV can run.  FreeRTOS therefore
 * calls `traceISR_EXIT_TO_SCHEDULER` (tick) / `portYIELD_FROM_ISR` (user
 * ISR) a few microseconds *before* `traceTASK_SWITCHED_OUT`.  If TraxProbe
 * still emits ISR_EXIT, Traxcope resumes the preempted task for that gap
 * (typically Idle, 2–5 µs) and then records SWITCH_OUT / SWITCH_IN — an
 * extra kernel frame plus a visually noisy “return to Idle” slice on
 * every tick that unblocks a task.
 *
 * Traxcope already has the matching path: SliceTracker flushes an ISR
 * left open on the context stack when SWITCH_OUT arrives, so the host
 * draws ISR → Scheduler → new task without activating the preempted
 * task.
 *
 *   1 (default) — omit the ISR_EXIT frame when a switch is pending.
 *                 SysTick (or the user ISR) is drawn until SWITCH_OUT;
 *                 the next slice is Scheduler → new task.  Saves one
 *                 16-byte kernel frame per yielding ISR.
 *   0           — emit ISR_EXIT and show the literal Cortex-M resume of
 *                 the preempted task before PendSV.
 *
 * Applies to `traceISR_EXIT_TO_SCHEDULER` (kernel tick / portYIELD_FROM_ISR
 * for the tick ISR), to `trax_freertos_on_tick()` on kernels without those
 * port.c hooks (CubeMX V10.3.x), and to `TRAX_ISR_END` /
 * `TRAX_ISR_EXIT_TO_SCHEDULER` for instrumented user ISRs.
 *
 * Override in App/Config/trax_config.h:
 *   #define TRAX_CFG_ISR_YIELD_TO_SCHEDULER  0
 */
#ifndef TRAX_CFG_ISR_YIELD_TO_SCHEDULER
#define TRAX_CFG_ISR_YIELD_TO_SCHEDULER  1
#endif

/**
 * @brief TraxProbe owns FreeRTOS vApplicationTickHook (default: 1)
 *
 * When 1 (default), TraxProbe:
 *   - forces `configUSE_TICK_HOOK` to 1 (CubeMX can leave it 0)
 *   - provides a strong `vApplicationTickHook` that calls
 *     `trax_freertos_on_tick()` then the weak `trax_app_tick_hook()`
 *
 * CubeMX: leave **USE_TICK_HOOK unchecked** and delete any generated
 * `vApplicationTickHook` in `app_freertos.c`. If the option stays
 * enabled, codegen restores an empty strong stub and the link fails
 * with multiple definition — that is the reminder to uncheck it.
 * CMSIS-RTOS also has a weak empty stub; TraxProbe's strong symbol
 * wins, so you do not need that file either.
 *
 * Put application tick work (BSP timebase, …) in `trax_app_tick_hook()`,
 * not in `vApplicationTickHook`.
 *
 * Set to 0 only if you must keep your own `vApplicationTickHook`; then
 * call `trax_freertos_on_tick()` from it when `configUSE_TICK_HOOK` is 1.
 *
 * Override in App/Config/trax_config.h:
 *   #define TRAX_CFG_OWN_FREERTOS_TICK_HOOK  0
 */
#ifndef TRAX_CFG_OWN_FREERTOS_TICK_HOOK
#define TRAX_CFG_OWN_FREERTOS_TICK_HOOK  1
#endif

#if (TRAX_CFG_OWN_FREERTOS_TICK_HOOK != 0) && (TRAX_CFG_OWN_FREERTOS_TICK_HOOK != 1)
#error "TRAX_CFG_OWN_FREERTOS_TICK_HOOK must be 0 (app owns vApplicationTickHook) or 1 (TraxProbe owns it)"
#endif

#if (TRAX_CFG_ISR_YIELD_TO_SCHEDULER != 0) && (TRAX_CFG_ISR_YIELD_TO_SCHEDULER != 1)
#error "TRAX_CFG_ISR_YIELD_TO_SCHEDULER must be 0 (literal ISR_EXIT) or 1 (collapse to scheduler)"
#endif

#endif /* TRAX_CONFIG_RTOS_H_ */
