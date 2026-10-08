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

/**
 ******************************************************************************
 * @file           : trax_rtos_port.h
 * @brief          : TraxProbe FreeRTOS trace hook definitions
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 *
 * FreeRTOS port for TraxProbe. Maps FreeRTOS trace hook macros to TraxProbe
 * frame macros via the RTOS entity table API (trax_rtos_tables.h).
 *
 * Pulled in by trax.h (via the dispatcher) when FreeRTOSConfig.h includes
 * trax.h AFTER the config* macros and configUSE_TRACE_FACILITY == 1.
 * Do not include this header from application code.
 *
 * FreeRTOS calls these macros from within kernel code (tasks.c, queue.c)
 * where the TCB structure and pxCurrentTCB are visible.
 *
 * KERNEL VERSION SUPPORT
 * ----------------------
 * Supported kernels: FreeRTOS V10.2.0 .. V11.2.x (and the ESP-IDF fork via
 * sdk/esp/esp_trace_freertos_impl.h).  The user MUST declare the kernel
 * version in App/Config/trax_config.h when swapping FreeRTOS trees:
 *
 *   #define TRAX_CFG_FREERTOS_VERSION   TRAX_FREERTOS_VERSION(10, 6, 2)
 *
 * Auto-detection is impossible: this header is parsed while FreeRTOSConfig.h
 * is still being processed (FreeRTOS.h includes it long before task.h defines
 * tskKERNEL_VERSION_MAJOR), so the version must come from the user — the same
 * contract Percepio's TraceRecorder uses (TRC_CFG_FREERTOS_VERSION).
 *
 * Keep application FreeRTOSConfig / main unchanged across supported versions;
 * TraxProbe adapts (hooks + weak FreeRTOS callbacks in trax_freertos_memory.c
 * and trax_freertos_tick.c).
 *
 * Version-sensitive behaviour handled by this port:
 *   >= 10.2.0  floor — Queue_t carries the u.xSemaphore (SemaphoreData_t)
 *              union and Timer_t carries ucStatus/tmrSTATUS_IS_AUTORELOAD;
 *              both are accessed by hooks below.  V10.0/V10.1 used different
 *              layouts and are NOT supported.
 *   <  10.4.0  task-notification hooks take NO index argument (single
 *              notification slot); Cortex-M port.c has no tick-ISR trace
 *              hooks (traceISR_ENTER family) — traceTASK_INCREMENT_TICK
 *              advances the timestamp and opens a synthetic SysTick ISR
 *              frame (closed by trax_freertos_on_tick / tick hook);
 *              queue-set notifications arrive through traceQUEUE_SEND on
 *              the container (traceQUEUE_SET_SEND does not exist yet), and
 *              may fire in ISR context — which is why the queue hooks read
 *              pxQueue->uxMessagesWaiting directly instead of calling
 *              uxQueueMessagesWaiting().
 *   >= 10.4.0  notification-array index argument on notify hooks; dedicated
 *              traceQUEUE_SET_SEND; tick-ISR trace hooks in Cortex-M ports.
 *   <  11.0.0  weak vApplicationGetIdleTaskMemory (and timer variant) in
 *              trax_freertos_memory.c — V10 ignores KERNEL_PROVIDED_STATIC_MEMORY.
 *   >= 11.0.0  SMP kernel possible (configNUMBER_OF_CORES > 1): pxCurrentTCB
 *              is tasks.c-private, so this port maps it to
 *              xTaskGetCurrentTaskHandle() for the other kernel TUs.
 *   >= 11.1.0  stream BATCHING buffers (third sbTYPE_* value → obj_type 9);
 *              xStreamBufferResetFromISR (traceSTREAM_BUFFER_RESET_FROM_ISR
 *              first fires here); configUSE_EVENT_GROUPS /
 *              configUSE_STREAM_BUFFERS opt-outs appear (defaulted to 1 by
 *              FreeRTOS.h AFTER this header is parsed — see the permissive
 *              guards below).
 *
 * Hooks whose signature is identical across the whole supported range are
 * deliberately written version-agnostically (no gate) so that a mistyped
 * TRAX_CFG_FREERTOS_VERSION degrades loudly at compile time rather than
 * silently corrupting the trace.
 *
 * O(1) LOOKUP STRATEGY:
 *   - traceTASK_CREATE allocates a table index from a free-list stack (O(1))
 *     and stores it in pxNewTCB->uxTCBNumber (a field reserved for trace tools)
 *   - All subsequent hooks read uxTCBNumber for direct table access (O(1))
 *   - traceTASK_DELETE frees the index back to the stack (O(1))
 *
 * PERFORMANCE:
 *   SWITCH_IN/OUT:   ~1.5us on 64MHz (single TRAX_FRAME_ARGS_ATOMIC, 1 param)
 *   TASK_CREATE:     ~5us (function call with name copy, infrequent)
 *   PRIORITY_SET:    ~1us (O(1) table update + conditional frame)
 *   Object ops:      ~1.5us (single TRAX_FRAME_ARGS_ATOMIC, 2 params)
 *
 * QUEUE / SEMAPHORE / MUTEX COVERAGE:
 *   Task context : traceQUEUE_SEND, traceQUEUE_RECEIVE, traceQUEUE_PEEK,
 *                  traceQUEUE_SEND_FAILED, traceQUEUE_RECEIVE_FAILED,
 *                  traceQUEUE_PEEK_FAILED
 *   ISR context  : traceQUEUE_SEND_FROM_ISR, traceQUEUE_RECEIVE_FROM_ISR,
 *                  traceQUEUE_PEEK_FROM_ISR,
 *                  traceQUEUE_SEND_FROM_ISR_FAILED,
 *                  traceQUEUE_RECEIVE_FROM_ISR_FAILED,
 *                  traceQUEUE_PEEK_FROM_ISR_FAILED
 *   Block events : traceBLOCKING_ON_QUEUE_SEND,
 *                  traceBLOCKING_ON_QUEUE_RECEIVE,
 *                  traceBLOCKING_ON_QUEUE_PEEK
 *   Lifecycle    : traceQUEUE_CREATE, traceQUEUE_DELETE,
 *                  traceQUEUE_REGISTRY_ADD
 *   Queue sets   : traceQUEUE_SET_SEND               (TRAX_TID_QUEUE_SET_SEND)
 *   The ISR variants reuse the task-context TIDs (TRAX_TID_QUEUE_SEND /
 *   TRAX_TID_QUEUE_RECEIVE / TRAX_TID_QUEUE_PEEK / *_FAILED). Caller context is
 *   reconstructed on the host from the surrounding TRAX_TID_ISR_ENTER /
 *   TRAX_TID_ISR_EXIT. The block events have their own TIDs
 *   (TRAX_TID_QUEUE_SEND_BLOCK / TRAX_TID_QUEUE_RECV_BLOCK / TRAX_TID_QUEUE_PEEK_BLOCK)
 *   and carry the calling task's TCB so the host can correlate "I'm
 *   about to BLOCK on this queue" with the SWITCH_OUT that immediately
 *   follows.  Queue sets reuse traceQUEUE_CREATE / DELETE / REGISTRY
 *   for lifecycle (xQueueCreateSet flows through xQueueGenericCreate
 *   with queueQUEUE_TYPE_SET) and TRAX_TID_QUEUE_RECEIVE for the receive
 *   side (xQueueSelectFromSet is just an xQueueReceive on the set's
 *   internal queue) — only the set-notification side-effect needs its
 *   own hook.  This closes the FreeRTOS queue trace-hook surface —
 *   every xQueue / xSemaphore / xQueueSet path now produces a visible
 *   trace event.
 *
 * TASK NOTIFICATION COVERAGE:
 *   Send (any)   : traceTASK_NOTIFY                  (TRAX_TID_TASK_NOTIFY)
 *   Send (ISR)   : traceTASK_NOTIFY_FROM_ISR         (TRAX_TID_TASK_NOTIFY)
 *   Send (Give)  : traceTASK_NOTIFY_GIVE_FROM_ISR    (TRAX_TID_TASK_NOTIFY_GIVE)
 *   Receive Take : traceTASK_NOTIFY_TAKE             (TRAX_TID_TASK_NOTIFY_TAKE)
 *   Block Take   : traceTASK_NOTIFY_TAKE_BLOCK       (TRAX_TID_TASK_NOTIFY_TAKE_BLOCK)
 *   Receive Wait : traceTASK_NOTIFY_WAIT             (TRAX_TID_TASK_NOTIFY_WAIT)
 *   Block Wait   : traceTASK_NOTIFY_WAIT_BLOCK       (TRAX_TID_TASK_NOTIFY_WAIT_BLOCK)
 *   Send-side payload [target_tcb, sender_tcb_or_0] so events land on the
 *   target's task lane (matches the "this task got woken" mental model);
 *   receive-side payload [self_tcb, index] so multi-index setups are
 *   distinguishable.  GIVE_FROM_ISR has its own TID because its
 *   semaphore-replacement role is the marquee artefact for tut21.
 *
 * STREAM / MESSAGE BUFFER COVERAGE:
 *   Create       : traceSTREAM_BUFFER_CREATE         (TRAX_TID_OBJ_CREATE, type=7/8/9)
 *   Send (task)  : traceSTREAM_BUFFER_SEND           (TRAX_TID_STREAM_BUFFER_SEND)
 *   Send-fail    : traceSTREAM_BUFFER_SEND_FAILED    (TRAX_TID_STREAM_BUFFER_SEND_FAILED)
 *   Send (ISR)   : traceSTREAM_BUFFER_SEND_FROM_ISR  (TRAX_TID_STREAM_BUFFER_SEND/FAILED)
 *   Recv (task)  : traceSTREAM_BUFFER_RECEIVE        (TRAX_TID_STREAM_BUFFER_RECV)
 *   Recv-fail    : traceSTREAM_BUFFER_RECEIVE_FAILED (TRAX_TID_STREAM_BUFFER_RECV_FAILED)
 *   Recv (ISR)   : traceSTREAM_BUFFER_RECEIVE_FROM_ISR (TRAX_TID_STREAM_BUFFER_RECV/FAILED)
 *   Block events : traceBLOCKING_ON_STREAM_BUFFER_SEND/RECEIVE
 *                                                     (TRAX_TID_STREAM_BUFFER_*_BLOCK)
 *   Reset        : traceSTREAM_BUFFER_RESET / RESET_FROM_ISR
 *                                                     (TRAX_TID_STREAM_BUFFER_RESET)
 *   Delete       : traceSTREAM_BUFFER_DELETE         (TRAX_TID_OBJ_DELETE)
 *   Create reuses TRAX_TID_OBJ_CREATE (obj_type byte = 7 + xStreamBufferType so
 *   stream / message / batching buffers slot into osStore alongside
 *   queues / sems / mutexes / timers — name resolution / filtering / lane
 *   come for free).  No name is set at create time because StreamBuffer_t
 *   has no pcName-equivalent field; users wanting named SBs build a thin
 *   wrapper that calls trax_rtos_object_set_name() after xStreamBufferCreate
 *   returns.  ISR send/recv have no separate FROM_ISR_FAILED hook in
 *   stream_buffer.c — xReturn == 0 discriminates, and the macro splits
 *   firmware-side so success/failure reach distinct TIDs (cleaner than
 *   a host-side numeric-zero check, and the per-frame cost is the same).
 *   traceSTREAM_BUFFER_CREATE_FAILED / CREATE_STATIC_FAILED are
 *   intentionally NOT implemented — see trax_tid.h header comment.
 *   traceSTREAM_BUFFER_DELETE IS now implemented via the
 *   trax_rtos_object_delete_by_handle() scan helper (StreamBuffer_t
 *   has no uxQueueNumber-equivalent for an O(1) lookup, so we pay an
 *   O(n) scan at delete time — acceptable because deletes are rare).
 *
 * SOFTWARE TIMER COVERAGE:
 *   Create       : traceTIMER_CREATE                 (TRAX_TID_OBJ_CREATE+OBJ_NAME, type=6)
 *   Cmd send     : traceTIMER_COMMAND_SEND           (TRAX_TID_TIMER_COMMAND_SEND)
 *   Cmd received : traceTIMER_COMMAND_RECEIVED       (TRAX_TID_TIMER_COMMAND_RECEIVED)
 *   Delete       : (piggy-backed on RECEIVED)         (TRAX_TID_OBJ_DELETE)
 *   Expired      : traceTIMER_EXPIRED                (TRAX_TID_TIMER_EXPIRED)
 *   Create reuses the existing object-lifecycle infrastructure
 *   (TRAX_TID_OBJ_CREATE with obj_type = 6 = timer, length = period_ticks,
 *   item_size = autoreload_flag) so timers appear in osStore as a
 *   normal RTOS object — name resolution / filtering / object lane all
 *   come for free.  COMMAND_SEND and COMMAND_RECEIVED carry the same
 *   [handle, command_id] payload so the host can pair them; the SEND→
 *   RECEIVED gap is tut16.2's headline measurement (latency through
 *   the timer-service-task's internal queue).  Delete is piggy-backed
 *   on COMMAND_RECEIVED with command_id == tmrCOMMAND_DELETE — V11
 *   has no dedicated traceTIMER_DELETE hook, but COMMAND_RECEIVED is
 *   the last trace point where pxTimer is still valid before
 *   prvProcessReceivedCommands does the actual cleanup, so we emit
 *   TRAX_TID_OBJ_DELETE (via trax_rtos_object_delete_by_handle()) at that
 *   moment.  traceTIMER_CREATE_FAILED is intentionally not implemented
 *   (FreeRTOS V11 never fires it from timers.c — adding a no-op TID
 *   would just inflate code size).
 *
 * TASK PRIORITY COVERAGE:
 *   App-driven   : traceTASK_PRIORITY_SET            (TRAX_TID_TASK_PRIORITY_SET)
 *   Mutex boost  : traceTASK_PRIORITY_INHERIT        (TRAX_TID_TASK_PRIORITY_INHERIT)
 *   Mutex restore: traceTASK_PRIORITY_DISINHERIT     (TRAX_TID_TASK_PRIORITY_DISINHERIT)
 *   The three TIDs are deliberately distinct so the host can render an
 *   explicit vTaskPrioritySet() call separately from a kernel-driven
 *   mutex-inheritance boost — the distinction is the marquee artefact for
 *   tut14.3 (mutex priority inversion). All three update the per-task
 *   priority field in trax_rtos_tables so subsequent SWITCH_IN events
 *   surface the boosted/restored value.
 *
 ******************************************************************************
 */

#ifndef TRAX_FREERTOS_PORT_H_
#define TRAX_FREERTOS_PORT_H_

#include "../common/trax_rtos_tables.h"

/*=============================================================================
 ====================TASK PRIORITY ORDERING CONVENTION=========================
 ============================================================================*/

/* FreeRTOS convention: **higher priority number = higher priority**.
 * `tskIDLE_PRIORITY` is hardcoded to 0 in the kernel source;
 * `configMAX_PRIORITIES - 1` is the highest. This is a kernel API fact
 * — every `xTaskCreate`, `vTaskPrioritySet`, etc. interprets the
 * argument the same way. There is no legitimate scenario in which a
 * FreeRTOS application would want to override this byte: doing so
 * would not change the kernel, it would only mislead the host into
 * rendering task lanes upside-down. The macro is therefore fixed (no
 * `#ifndef` guard) so the build fails loudly if someone tries to
 * redefine it elsewhere. */
#define TRAX_CFG_TASK_PRIO_ASCENDING  1   /* FreeRTOS: higher number wins */

/*=============================================================================
 ====================KERNEL VERSION CONTRACT===================================
 ============================================================================*/

/*
 * TRAX_CFG_FREERTOS_VERSION is MANDATORY for the FreeRTOS port.
 *
 * It cannot be auto-detected: FreeRTOS.h includes FreeRTOSConfig.h (and
 * thereby this header) near its top, hundreds of lines before task.h defines
 * tskKERNEL_VERSION_MAJOR/MINOR/BUILD — so at parse time here the kernel's
 * own version macros do not exist yet, in ANY translation unit.  The
 * previous auto-detection block in this file was dead code for exactly that
 * reason (the wire metadata always reported 0.0.0).
 *
 * The encoder macro and the NOT_SET sentinel live in
 * config/trax_config_rtos.h, which is always parsed before this header.
 */
#if (TRAX_CFG_FREERTOS_VERSION == TRAX_FREERTOS_VERSION_NOT_SET)
#error "TraxProbe FreeRTOS port: define TRAX_CFG_FREERTOS_VERSION in App/Config/trax_config.h, e.g. #define TRAX_CFG_FREERTOS_VERSION TRAX_FREERTOS_VERSION(11, 2, 0). See tskKERNEL_VERSION_NUMBER in FreeRTOS include/task.h for the kernel version in your build."
#endif

/*
 * Supported floor: V10.2.0.  Older kernels use different kernel-private
 * struct layouts consumed by hooks below (V10.1 and earlier keep the
 * recursive-mutex count in Queue_t.u.uxRecursiveCallCount instead of
 * u.xSemaphore.uxRecursiveCallCount, and Timer_t.ucStatus /
 * tmrSTATUS_IS_AUTORELOAD do not exist before V10.2.0) — supporting them
 * would mean shipping struct-access variants we cannot test.  All actively
 * maintained vendor SDKs bundle >= 10.2 (ST CubeMX: 10.3.1, ESP-IDF v5:
 * 10.5-based fork, FreeRTOS LTS: 10.5 / 11.1).
 */
#if (TRAX_CFG_FREERTOS_VERSION < TRAX_FREERTOS_VERSION(10, 2, 0))
#error "TraxProbe FreeRTOS port supports FreeRTOS V10.2.0 and newer. Check TRAX_CFG_FREERTOS_VERSION in App/Config/trax_config.h."
#endif

/*
 * OS version reported in the START_TRACE metadata frame — decomposed from
 * the (authoritative) user-declared kernel version.
 */
#undef TRAX_CFG_OS_VER_MAJOR
#define TRAX_CFG_OS_VER_MAJOR ((TRAX_CFG_FREERTOS_VERSION) / 100000L)
#undef TRAX_CFG_OS_VER_MINOR
#define TRAX_CFG_OS_VER_MINOR (((TRAX_CFG_FREERTOS_VERSION) / 1000L) % 100L)
#undef TRAX_CFG_OS_VER_PATCH
#define TRAX_CFG_OS_VER_PATCH ((TRAX_CFG_FREERTOS_VERSION) % 1000L)

/*
 * configUSE_TRACE_FACILITY == 1 is required: the O(1) lookup strategy stores
 * table indices in TCB_t.uxTCBNumber / Queue_t.uxQueueNumber, and
 * traceQUEUE_CREATE reads Queue_t.ucQueueType — all three fields only exist
 * in the kernel structs when the trace facility is enabled.  Without this
 * check the user would get a wall of confusing field-access errors from
 * deep inside tasks.c/queue.c instead of one actionable message.
 *
 * Guarded by INC_FREERTOS_H so it only fires when this header is really
 * being parsed from FreeRTOSConfig.h (i.e. by kernel/application TUs);
 * TraxProbe library TUs include trax.h without FreeRTOS in scope and must
 * not trip it.  This also enforces the documented include order: define
 * all FreeRTOS config macros BEFORE the trailing #include "trax.h".
 */
#if defined(INC_FREERTOS_H) && (!defined(configUSE_TRACE_FACILITY) || (configUSE_TRACE_FACILITY != 1))
#error "TraxProbe FreeRTOS port requires configUSE_TRACE_FACILITY == 1, defined in FreeRTOSConfig.h BEFORE #include \"trax.h\"."
#endif

/* When TRAX_ENABLE==0 the entire FreeRTOS trace-hook layer below compiles
 * out. FreeRTOS then falls back to its own built-in (empty) defaults for
 * every undefined traceXXX() macro, so the kernel emits no TraxProbe frames
 * and references no TraxProbe symbols. */
#if TRAX_ENABLE

/*=============================================================================
 ====================ISR TRACE HOOKS (SysTick via port.c)=====================
 ============================================================================*/

/*
 * Guard flag: set by traceISR_ENTER(), cleared by traceISR_EXIT() and
 * traceISR_EXIT_TO_SCHEDULER().  Prevents spurious TRAX_TID_ISR_TICK exit events
 * when portYIELD_FROM_ISR() is called from an untraced ISR (e.g. a DMA or
 * timer ISR that lacks TRAX_ISR_ENTER/EXIT instrumentation).  Without this
 * guard, portEND_SWITCHING_ISR → traceISR_EXIT would emit an ISR_EXIT for
 * the tick ISR even though traceISR_ENTER was never called, desynchronising
 * the scope's SliceTracker context stack.
 *
 * Per-core array, sized by TRAX_CFG_CORE_COUNT (default 1, see
 * config/trax_config_hw_port.h).  On SMP each core's tick ISR
 * runs concurrently with the other cores' tick ISRs and each one
 * needs its own guard or one core's clear would race with another
 * core's set.  Single-core builds get a one-element array — same
 * memory and code as the previous scalar form after the compiler
 * folds TRAX_PORT_GET_CORE_ID() to the constant 0.
 *
 * Defined in trax_rtos_tables.c (zero-initialised).
 */
extern volatile uint8_t trax_in_tick_isr[TRAX_CFG_CORE_COUNT];
extern volatile uint8_t trax_synthetic_tick_isr[TRAX_CFG_CORE_COUNT];
extern volatile uint8_t trax_tick_yield_pending[TRAX_CFG_CORE_COUNT];
extern volatile uint8_t trax_tick_hook_missing[TRAX_CFG_CORE_COUNT];

/**
 * @brief Close a synthetic SysTick ISR frame opened from
 *        traceTASK_INCREMENT_TICK (kernels without port.c traceISR_*).
 *
 * Called from TraxProbe's vApplicationTickHook. No-op when the native
 * V10.4+ path already owns the tick ISR. If TRAX_CFG_OWN_FREERTOS_TICK_HOOK
 * is 0, call this from your own vApplicationTickHook.
 */
void trax_freertos_on_tick(void);

/**
 * @brief Application tick work (BSP timebase, …). Weak empty default.
 *
 * TraxProbe's vApplicationTickHook calls this after trax_freertos_on_tick().
 * Override in application code; do not define vApplicationTickHook yourself
 * unless TRAX_CFG_OWN_FREERTOS_TICK_HOOK is 0.
 */
void trax_app_tick_hook(void);

#if (TRAX_CFG_OWN_FREERTOS_TICK_HOOK == 1)
#ifdef configUSE_TICK_HOOK
#undef configUSE_TICK_HOOK
#endif
#define configUSE_TICK_HOOK  1
#endif

/*
 * These macros are called from port.c's SysTick_Handler to trace the tick
 * ISR.  traceISR_EXIT_TO_SCHEDULER fires when xTaskIncrementTick returns
 * pdTRUE (context switch needed); traceISR_EXIT fires otherwise.
 *
 * Ordering note
 * -------------
 * traceISR_ENTER calls trax_timestamp_tick() before TRAX_ISR_ENTER. The
 * order is no longer required for timestamp correctness — the host
 * (Traxcope MessageDecoder) detects and compensates for the SysTick
 * tick-boundary race per core — but we keep it because:
 *   - it is the cheaper of the two operations and lets the rest of the
 *     handler run with the freshly-incremented tick base,
 *   - it keeps the ENTER timestamp aligned with the same tick that
 *     vApplicationTickHook will see.
 *
 * The trax_in_tick_isr guard is unrelated to the timestamp race: it
 * prevents portEND_SWITCHING_ISR / traceISR_EXIT from emitting a
 * spurious TRAX_TID_ISR_TICK exit if a non-tick ISR (e.g. a DMA or peripheral
 * IRQ that lacks TRAX_ISR_ENTER/EXIT instrumentation) calls
 * portYIELD_FROM_ISR. Without the guard, the resulting traceISR_EXIT
 * would close an ENTER that was never opened and desync the host's
 * SliceTracker context stack. The guard is still required.
 */
#define traceISR_ENTER()                                                       \
  do {                                                                         \
    trax_timestamp_tick();                                                     \
    trax_in_tick_isr[TRAX_PORT_GET_CORE_ID()] = 1;                             \
    TRAX_ISR_ENTER(TRAX_TID_ISR_TICK);                                              \
  } while (0)

#define traceISR_EXIT()                                                        \
  do {                                                                         \
    if (trax_in_tick_isr[TRAX_PORT_GET_CORE_ID()]) {                           \
      TRAX_ISR_EXIT(TRAX_TID_ISR_TICK);                                             \
      trax_in_tick_isr[TRAX_PORT_GET_CORE_ID()] = 0;                           \
    }                                                                          \
  } while (0)

/*
 * FreeRTOS calls this when xTaskIncrementTick() returns pdTRUE (PendSV
 * will run).  Cortex-M still returns to the interrupted thread for a
 * few microseconds before PendSV; emitting ISR_EXIT makes Traxcope
 * draw that hop (Idle “ran for 3 µs”, then TASK_SWITCH).  Default
 * TRAX_CFG_ISR_YIELD_TO_SCHEDULER=1 omits the frame: SliceTracker
 * closes the still-open SysTick slice on SWITCH_OUT.  Set the
 * config to 0 for the literal return-to-preempted-task recording.
 * The in-tick guard is always cleared so a later portYIELD_FROM_ISR
 * from an untraced ISR cannot emit a spurious tick EXIT.
 */
#if TRAX_CFG_ISR_YIELD_TO_SCHEDULER
#define traceISR_EXIT_TO_SCHEDULER()                                           \
  do {                                                                         \
    trax_in_tick_isr[TRAX_PORT_GET_CORE_ID()] = 0;                             \
  } while (0)
#else
#define traceISR_EXIT_TO_SCHEDULER()  traceISR_EXIT()
#endif

/*
 * Pre-V10.4 / no-port-hook fallback (timestamp + SysTick ISR slices)
 * ------------------------------------------------------------------
 * traceISR_ENTER / traceISR_EXIT above are invoked from port.c's SysTick
 * handler only in FreeRTOS V10.4.0 and newer — older kernels (e.g. ST
 * CubeMX's bundled V10.3.1) have no tick-ISR trace hooks in their ports.
 *
 * traceTASK_INCREMENT_TICK fires at the top of xTaskIncrementTick() on
 * every kernel version. When the native ENTER has not run, we:
 *   1. Advance the trace timestamp (same as before).
 *   2. Open a synthetic SysTick ISR frame so Traxcope gets a swimlane
 *      slice without patching Middleware port.c.
 *
 * Close path:
 *   - Preferred: trax_freertos_on_tick() from the tick hook
 *     (configUSE_TICK_HOOK == 1) near the end of xTaskIncrementTick.
 *     Default: TraxProbe owns vApplicationTickHook and forces the
 *     config. CubeMX users uncheck USE_TICK_HOOK / delete the stub.
 *   - Degraded: if the tick hook is off (OWN=0 and config=0),
 *     ENTER+EXIT immediately here (near-zero-width markers).
 *
 * Runtime guards (not a compile-time version gate) keep one definition
 * correct on ALL kernels and immune to a mistyped TRAX_CFG_FREERTOS_VERSION:
 *
 *   1. trax_in_tick_isr[core] — set by native traceISR_ENTER() before
 *      xTaskIncrementTick() on V10.4+. A set flag means "skip" (no
 *      double timestamp / double ENTER).
 *   2. TRAX_PORT_IN_ISR() — filters xTaskResumeAll() pended-tick replay
 *      in task context.
 *   3. trax_synthetic_tick_isr[core] — marks ENTER we opened so
 *      trax_freertos_on_tick() closes only the synthetic path.
 *   4. trax_tick_hook_missing[core] — self-heal latch.  Finding guard 3
 *      still set at the top of a tick means the close path never ran, so
 *      the application owns vApplicationTickHook (OWN=0) and forgot to
 *      call trax_freertos_on_tick().  Left alone, guard 1 would stay set
 *      and skip trax_timestamp_tick() forever, freezing the whole trace
 *      timebase rather than merely losing the SysTick lane.  We close the
 *      stale frame, latch the fault for the host, and fall back to the
 *      zero-width ENTER+EXIT pattern so time keeps advancing.  Costs the
 *      healthy path one byte compare per tick and never triggers on the
 *      native V10.4+ path, which leaves guard 3 clear.
 *
 * Defined only when the hardware port provides TRAX_PORT_IN_ISR().
 */
#if defined(TRAX_PORT_IN_ISR)
#if defined(configUSE_TICK_HOOK) && (configUSE_TICK_HOOK == 1)
/* Tick hook will call trax_freertos_on_tick() to close the synthetic ISR. */
#define traceTASK_INCREMENT_TICK(xTickCount)                                   \
  do {                                                                         \
    uint8_t _trax_tick_core = (uint8_t)TRAX_PORT_GET_CORE_ID();                \
    if (trax_synthetic_tick_isr[_trax_tick_core] != 0U) {                      \
      TRAX_ISR_END(TRAX_TID_ISR_TICK,                                          \
                   trax_tick_yield_pending[_trax_tick_core]);                  \
      trax_tick_yield_pending[_trax_tick_core] = 0U;                           \
      trax_synthetic_tick_isr[_trax_tick_core] = 0U;                           \
      trax_in_tick_isr[_trax_tick_core] = 0U;                                  \
      trax_tick_hook_missing[_trax_tick_core] = 1U;                            \
    }                                                                          \
    if ((trax_in_tick_isr[_trax_tick_core] == 0U) && TRAX_PORT_IN_ISR()) {     \
      trax_timestamp_tick();                                                   \
      if (trax_tick_hook_missing[_trax_tick_core] != 0U) {                     \
        TRAX_ISR_ENTER(TRAX_TID_ISR_TICK);                                     \
        TRAX_ISR_EXIT(TRAX_TID_ISR_TICK);                                      \
      } else {                                                                 \
        trax_in_tick_isr[_trax_tick_core] = 1U;                                \
        trax_synthetic_tick_isr[_trax_tick_core] = 1U;                         \
        TRAX_ISR_ENTER(TRAX_TID_ISR_TICK);                                     \
      }                                                                        \
    }                                                                          \
  } while (0)
#else
/* No tick hook: near-zero-width ENTER+EXIT so the SysTick lane is not empty. */
#define traceTASK_INCREMENT_TICK(xTickCount)                                   \
  do {                                                                         \
    uint8_t _trax_tick_core = (uint8_t)TRAX_PORT_GET_CORE_ID();                \
    if ((trax_in_tick_isr[_trax_tick_core] == 0U) && TRAX_PORT_IN_ISR()) {     \
      trax_timestamp_tick();                                                   \
      TRAX_ISR_ENTER(TRAX_TID_ISR_TICK);                                       \
      TRAX_ISR_EXIT(TRAX_TID_ISR_TICK);                                        \
    }                                                                          \
  } while (0)
#endif /* configUSE_TICK_HOOK */
#endif /* TRAX_PORT_IN_ISR */

/*=============================================================================
 ====================pxCurrentTCB EXTERN DECLARATION===========================
 ============================================================================*/

/*
 * pxCurrentTCB is defined in tasks.c without `static`, so it has external
 * linkage — but FreeRTOS does NOT extern-declare it in any public header
 * (task.h, FreeRTOS.h, etc.).  Translation units inside tasks.c see it via
 * the file-scope definition, and stack_macros.h pulls it in transitively
 * (only included from tasks.c), but every OTHER kernel translation unit
 * (queue.c, stream_buffer.c, timers.c, event_groups.c, ...) cannot see the
 * symbol without an explicit forward declaration.
 *
 * Several of our trace hooks reference `pxCurrentTCB` directly to record
 * "the task that was running when this hook fired" — task notify wait,
 * traceBLOCKING_ON_QUEUE_*, traceBLOCKING_ON_STREAM_BUFFER_*, etc.  Those
 * hooks expand at the FreeRTOS source-file call site (queue.c for the
 * queue blocks, stream_buffer.c for the SB blocks), so without this extern
 * declaration the affected TUs fail to compile with "'pxCurrentTCB'
 * undeclared".  The hooks that ONLY ever fire from tasks.c (SWITCH_IN /
 * SWITCH_OUT, TASK_DELAY, the notify family) compiled fine before, but
 * keeping the extern declaration in this header is harmless for them and
 * future-proofs us against new hooks landing in other kernel TUs.
 *
 * The extern uses a forward-declared opaque struct (`tskTaskControlBlock`,
 * the same tag tasks.c uses internally) so we never see TCB_t's layout
 * (which is private to tasks.c).  All trace macros consume the value via
 * `(uint32_t)(uintptr_t)pxCurrentTCB` — only the pointer's bit pattern
 * matters; we never dereference it on this side.
 *
 * Multi-core builds (configNUMBER_OF_CORES > 1, upstream SMP kernel V11+)
 * keep the running task per core in pxCurrentTCBs[] and define
 * `pxCurrentTCB` as a tasks.c-PRIVATE macro for xTaskGetCurrentTaskHandle()
 * — see tasks.c around the matching `#if ( configNUMBER_OF_CORES == 1 )`
 * block.  Hooks expanding in OTHER kernel TUs (queue.c, stream_buffer.c,
 * event_groups.c) would therefore not compile on SMP without help, so for
 * SMP we install the same mapping globally, exactly as the ESP-IDF shim
 * (sdk/esp/esp_trace_freertos_impl.h) already does for the IDF fork:
 *
 *   - The replacement list is token-identical to tasks.c's own macro, so
 *     when tasks.c re-defines it the redefinition is benign (ISO C 6.10.3).
 *   - xTaskGetCurrentTaskHandle() is safe inside frame-macro arguments per
 *     docs/PITFALL_RTOS_CALLS_IN_FRAME_ARGUMENTS.md: on SMP it is a plain
 *     per-core read under a save/restore interrupt mask (the composable
 *     pattern), never a task-level critical section.
 *   - The function is declared in task.h, which every kernel TU includes
 *     before any hook expands; INCLUDE_xTaskGetCurrentTaskHandle is forced
 *     to 1 below (Percepio's TraceRecorder forces the same), which is safe
 *     because this header is documented to be included at the END of
 *     FreeRTOSConfig.h.
 *
 * Note on guard semantics: this header is included from FreeRTOSConfig.h,
 * which is processed BEFORE FreeRTOS.h installs the configNUMBER_OF_CORES
 * default (V11 sets it to 1 if not user-overridden), so the symbol may
 * legitimately be undefined here.  We treat "undefined" as single-core to
 * match FreeRTOS's own fallback — the alternative (`#if (X == 1)` alone)
 * silently elides the extern in the common single-core case where the
 * user hasn't bothered to set configNUMBER_OF_CORES, which would
 * reintroduce the original "'pxCurrentTCB' undeclared" failure mode.
 */
#if !defined(configNUMBER_OF_CORES) || (configNUMBER_OF_CORES == 1)
struct tskTaskControlBlock;
extern struct tskTaskControlBlock * volatile pxCurrentTCB;
#else
#ifndef pxCurrentTCB
#define pxCurrentTCB xTaskGetCurrentTaskHandle()
#endif
#undef INCLUDE_xTaskGetCurrentTaskHandle
#define INCLUDE_xTaskGetCurrentTaskHandle 1
#endif

/*=============================================================================
 ====================CONTEXT SWITCH HOOKS (critical path)=====================
 ============================================================================*/

/*
 * traceTASK_SWITCHED_IN / traceTASK_SWITCHED_OUT
 *
 * Called from vTaskSwitchContext() in tasks.c on every context switch.
 * pxCurrentTCB is visible in that scope. No table access needed.
 *
 * Frame: 16 bytes = Header(4) + Timestamp(4) + TID(4) + Handle(4)
 */
#define traceTASK_SWITCHED_IN()                                                \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_TASK_SWITCH_IN, (uint32_t)pxCurrentTCB)

#define traceTASK_SWITCHED_OUT()                                               \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_TASK_SWITCH_OUT, (uint32_t)pxCurrentTCB)

/*=============================================================================
 ====================TASK LIFECYCLE HOOKS======================================
 ============================================================================*/

/*
 * Stack size helper: calculate from TCB pointers when available.
 * Requires configRECORD_STACK_HIGH_ADDRESS == 1 (adds pxEndOfStack to TCB).
 * The macro expands inside tasks.c where TCB_t fields are visible.
 *
 * Include-order sensitive: unlike the permissive feature guards elsewhere
 * in this file, this one CANNOT treat "undefined" as enabled — the chosen
 * variant is baked in at parse time and the 1-variant dereferences a TCB
 * field that only exists when the option is really on.  Define
 * configRECORD_STACK_HIGH_ADDRESS before the trax include in
 * FreeRTOSConfig.h (as documented) or task stack sizes are reported as 0.
 */
#if (configRECORD_STACK_HIGH_ADDRESS == 1)
#define TRAX_RTOS_STACK_SIZE(pxTCB)                                            \
  (uint32_t)((pxTCB)->pxEndOfStack - (pxTCB)->pxStack + 1)
#else
#define TRAX_RTOS_STACK_SIZE(pxTCB) 0
#endif

/*
 * traceTASK_CREATE(pxNewTCB)
 *
 * Called from prvAddNewTaskToReadyList() in tasks.c after TCB is fully
 * initialized. pxNewTCB is TCB_t* with all fields visible in that scope.
 *
 * Allocates a table slot and stores the index in uxTCBNumber for O(1)
 * lookups by all subsequent hooks (delete, priority set, etc.).
 */
#define traceTASK_CREATE(pxNewTCB)                                             \
  do {                                                                         \
    int _trax_idx = trax_rtos_task_create(                                     \
        (uint32_t)(pxNewTCB), (uint32_t)(pxNewTCB)->uxPriority,                \
        TRAX_RTOS_STACK_SIZE(pxNewTCB), (pxNewTCB)->pcTaskName);               \
    if (_trax_idx >= 0) {                                                      \
      (pxNewTCB)->uxTCBNumber = (UBaseType_t)_trax_idx;                        \
    }                                                                          \
  } while (0)

/*
 * traceTASK_DELETE(pxTaskToDelete)
 *
 * Uses uxTCBNumber for O(1) table lookup and frees the slot.
 */
#define traceTASK_DELETE(pxTaskToDelete)                                       \
  trax_rtos_task_delete((uint8_t)(pxTaskToDelete)->uxTCBNumber,                \
                        (uint32_t)(pxTaskToDelete))

/*
 * traceTASK_CREATE_FAILED(...)
 *
 * DEFENSIVE ONLY on the supported kernel range: V10.2.1, V10.3.1 and
 * V11.2.0 tasks.c never invoke this hook (verified by grep — xTaskCreate's
 * malloc-fail branch just returns errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY).
 * FreeRTOS.h still default-defines it, and kernel forks (e.g. ESP-IDF) may
 * call it, so we keep a definition that is safe in ANY expansion scope:
 *
 *   - Variadic, absorbing whatever arguments a fork passes (upstream
 *     removed the parameter long ago; History.txt, "Removed the (pointless)
 *     parameter from the traceTASK_CREATE_FAILED() macro").
 *   - NO payload from enclosing-scope locals.  The previous revision read
 *     `uxPriority`/`uxStackDepth` from the call-site scope, but the stack
 *     parameter is `usStackDepth` on pre-11.1 kernels (renamed by the V11.1
 *     configSTACK_DEPTH_TYPE sweep) — an out-of-scope reference that only
 *     stayed buildable because the hook is never expanded.  A bare
 *     timestamp+TID frame carries the diagnosis ("task create failed → look
 *     at the preceding HEAP_ALLOC_FAILED for the size") without any
 *     version-fragile scope assumptions.
 */
#define traceTASK_CREATE_FAILED(...)                                           \
  TRAX_FRAME_NOARGS_ATOMIC(TRAX_TID_TASK_CREATE_FAILED)

/*
 * traceTASK_PRIORITY_SET(pxTask, uxNewPriority)
 *
 * Called from vTaskPrioritySet() in tasks.c. Uses uxTCBNumber for O(1)
 * table access to update the stored priority.
 */
#define traceTASK_PRIORITY_SET(pxTask, uxNewPriority)                          \
  trax_rtos_task_update_priority((uint8_t)(pxTask)->uxTCBNumber,               \
                                 (uint32_t)(pxTask), (uint8_t)(uxNewPriority))

/*
 * traceTASK_PRIORITY_INHERIT(pxTCBOfMutexHolder, uxInheritedPriority)
 *
 * Called from xTaskPriorityInherit() in tasks.c when a higher-priority task
 * blocks on a mutex that a lower-priority task already holds. The kernel
 * boosts the holder's priority to the blocker's so the mid-priority crowd
 * cannot starve the chain (the bounded version of priority inversion).
 *
 * Distinct TID (TRAX_TID_TASK_PRIORITY_INHERIT, NOT TRAX_TID_TASK_PRIORITY_SET) so the
 * host can render the kernel-driven boost differently from an explicit
 * vTaskPrioritySet() call — the distinction is the marquee artefact for
 * tut14.3 (mutex priority inversion). The table entry is updated in place
 * so downstream SWITCH_IN events report the boosted value.
 */
#define traceTASK_PRIORITY_INHERIT(pxTCBOfMutexHolder, uxInheritedPriority)    \
  trax_rtos_task_priority_inherit(                                             \
      (uint8_t)(pxTCBOfMutexHolder)->uxTCBNumber,                              \
      (uint32_t)(pxTCBOfMutexHolder), (uint8_t)(uxInheritedPriority))

/*
 * traceTASK_PRIORITY_DISINHERIT(pxTCBOfMutexHolder, uxOriginalPriority)
 *
 * Called from xTaskPriorityDisinherit() / xTaskPriorityDisinheritAfterTimeout()
 * in tasks.c when the holder gives back the boosted mutex (or the blocker
 * times out) and the kernel restores the holder's pre-boost priority.
 * Pairs with traceTASK_PRIORITY_INHERIT — every INHERIT eventually has a
 * matching DISINHERIT on the same task handle.
 */
#define traceTASK_PRIORITY_DISINHERIT(pxTCBOfMutexHolder, uxOriginalPriority)  \
  trax_rtos_task_priority_disinherit(                                          \
      (uint8_t)(pxTCBOfMutexHolder)->uxTCBNumber,                              \
      (uint32_t)(pxTCBOfMutexHolder), (uint8_t)(uxOriginalPriority))

/*=============================================================================
 ====================TASK STATE HOOKS=========================================
 ============================================================================*/

#define traceMOVED_TASK_TO_READY_STATE(pxTCB)                                  \
  do {                                                                         \
    uint8_t _trax_rdy_core = (uint8_t)TRAX_PORT_GET_CORE_ID();                 \
    TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_TASK_READY, (uint32_t)(pxTCB));            \
    if ((trax_in_tick_isr[_trax_rdy_core] != 0U)                               \
        && ((pxTCB)->uxPriority >= pxCurrentTCB->uxPriority)) {                \
      trax_tick_yield_pending[_trax_rdy_core] = 1U;                            \
    }                                                                          \
  } while (0)

#define traceTASK_SUSPEND(pxTaskToSuspend)                                     \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_TASK_SUSPEND, (uint32_t)(pxTaskToSuspend))

#define traceTASK_RESUME(pxTaskToResume)                                       \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_TASK_RESUME, (uint32_t)(pxTaskToResume))

#define traceTASK_RESUME_FROM_ISR(pxTaskToResume)                              \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_TASK_RESUME, (uint32_t)(pxTaskToResume))

#define traceTASK_DELAY()                                                      \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_TASK_DELAY, (uint32_t)pxCurrentTCB)

#define traceTASK_DELAY_UNTIL(x)                                               \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_TASK_DELAY, (uint32_t)pxCurrentTCB)

/*=============================================================================
 ====================TASK NOTIFICATION HOOKS===================================
 ============================================================================*/

/*
 * Task notifications — FreeRTOS's lightweight per-task signalling primitive,
 * the recommended modern replacement for binary/counting semaphores in
 * ISR-to-task wake-up patterns.  Without these hooks an ISR-driven wake
 * shows up only as a SWITCH_OUT/SWITCH_IN pair on the receiver's lane with
 * no event explaining why — these macros add the explicit "you got woken
 * because IRQ_X notified you" event that tut21 needs to teach the pattern.
 *
 * Send-side (NOTIFY / NOTIFY_FROM_ISR / NOTIFY_GIVE_FROM_ISR)
 * -----------------------------------------------------------
 * Hooks fire from xTaskGenericNotify() / xTaskGenericNotifyFromISR() /
 * vTaskNotifyGiveFromISR() in tasks.c with `pxTCB` (the target) in scope
 * and `pxCurrentTCB` pointing at the SENDER (in task ctx) or whatever
 * task got preempted by the IRQ (in ISR ctx — not semantically the
 * sender).  Payload: [target_tcb, sender_tcb_or_0].  Sender is
 * pxCurrentTCB in task ctx and explicitly 0 in ISR ctx — the surrounding
 * ISR_ENTER / ISR_EXIT brackets remain the authoritative caller-context
 * source, the explicit 0 just lets the renderer skip the "by task_X"
 * suffix without looking at the brackets.  Lands on the TARGET task's
 * lane (matches the "this task got woken" mental model).
 *
 * Index info is dropped on the send side — usually 0
 * (configTASK_NOTIFICATION_ARRAY_ENTRIES default is 1).  If a future use
 * case needs the index, add a 3rd uint32_t param to the frame.
 *
 * NOTIFY (any-action, task or ISR ctx) and GIVE_FROM_ISR have distinct
 * TIDs because GIVE's "atomic increment, like a binary/counting sem give"
 * semantic is the marquee artefact for tut21 — a reader watching a
 * notification trace needs to be able to see at a glance whether the
 * sender used the lightweight semaphore-replacement fast path or the
 * heavier general-purpose notify API.
 *
 * Receive-side (TAKE / WAIT and their _BLOCK peers)
 * --------------------------------------------------
 * Hooks fire from ulTaskNotifyTake() / ulTaskNotifyWait() with
 * `pxCurrentTCB` = the receiver and `uxIndexToWaitOn` in scope.
 * Payload: [self_tcb, index].  TAKE and WAIT have distinct TIDs because
 * the consume semantic differs (TAKE clears the 32-bit value; WAIT
 * clears only the bits the caller asked about) — tut21 will need to
 * render the two differently to teach when to pick which.  The _BLOCK
 * peers fire the instant the receiver discovers there's nothing to take
 * and is about to be parked, mirroring the QUEUE_RECEIVE / RECV_BLOCK
 * pair.
 *
 * Portability — FreeRTOS V10.4.0 introduced task-notification ARRAYS and
 * with them an index argument on every notify hook; older kernels (e.g.
 * STM32Cube's bundled V10.3.1) invoke the same hooks with NO argument.
 * The two shapes are version-gated explicitly (same approach as Percepio's
 * TraceRecorder kernel port): the pre-10.4 variants hard-code index 0,
 * which is exact — those kernels have a single (index-0) notification slot.
 */
#if (TRAX_CFG_FREERTOS_VERSION >= TRAX_FREERTOS_VERSION(10, 4, 0))

#define traceTASK_NOTIFY(uxIndexToNotify)                                      \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_TASK_NOTIFY, (uint32_t)pxTCB,                     \
                         (uint32_t)pxCurrentTCB)

#define traceTASK_NOTIFY_FROM_ISR(uxIndexToNotify)                             \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_TASK_NOTIFY, (uint32_t)pxTCB, (uint32_t)0)

#define traceTASK_NOTIFY_GIVE_FROM_ISR(uxIndexToNotify)                        \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_TASK_NOTIFY_GIVE, (uint32_t)pxTCB, (uint32_t)0)

#define traceTASK_NOTIFY_TAKE(uxIndexToWaitOn)                                 \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_TASK_NOTIFY_TAKE, (uint32_t)pxCurrentTCB,         \
                         (uint32_t)(uxIndexToWaitOn))

#define traceTASK_NOTIFY_TAKE_BLOCK(uxIndexToWaitOn)                           \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_TASK_NOTIFY_TAKE_BLOCK,                           \
                         (uint32_t)pxCurrentTCB, (uint32_t)(uxIndexToWaitOn))

#define traceTASK_NOTIFY_WAIT(uxIndexToWaitOn)                                 \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_TASK_NOTIFY_WAIT, (uint32_t)pxCurrentTCB,         \
                         (uint32_t)(uxIndexToWaitOn))

#define traceTASK_NOTIFY_WAIT_BLOCK(uxIndexToWaitOn)                           \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_TASK_NOTIFY_WAIT_BLOCK,                           \
                         (uint32_t)pxCurrentTCB, (uint32_t)(uxIndexToWaitOn))

#else /* pre-V10.4.0: single notification slot, hooks take no argument */

#define traceTASK_NOTIFY()                                                     \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_TASK_NOTIFY, (uint32_t)pxTCB,                     \
                         (uint32_t)pxCurrentTCB)

#define traceTASK_NOTIFY_FROM_ISR()                                            \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_TASK_NOTIFY, (uint32_t)pxTCB, (uint32_t)0)

#define traceTASK_NOTIFY_GIVE_FROM_ISR()                                       \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_TASK_NOTIFY_GIVE, (uint32_t)pxTCB, (uint32_t)0)

#define traceTASK_NOTIFY_TAKE()                                                \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_TASK_NOTIFY_TAKE, (uint32_t)pxCurrentTCB,         \
                         (uint32_t)0)

#define traceTASK_NOTIFY_TAKE_BLOCK()                                          \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_TASK_NOTIFY_TAKE_BLOCK,                           \
                         (uint32_t)pxCurrentTCB, (uint32_t)0)

#define traceTASK_NOTIFY_WAIT()                                                \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_TASK_NOTIFY_WAIT, (uint32_t)pxCurrentTCB,         \
                         (uint32_t)0)

#define traceTASK_NOTIFY_WAIT_BLOCK()                                          \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_TASK_NOTIFY_WAIT_BLOCK,                           \
                         (uint32_t)pxCurrentTCB, (uint32_t)0)

#endif /* TRAX_CFG_FREERTOS_VERSION >= 10.4.0 */

/*=============================================================================
 ====================QUEUE / SEMAPHORE / MUTEX HOOKS==========================
 ============================================================================*/

/*
 * Depth source — direct field read, NEVER uxQueueMessagesWaiting()
 * ----------------------------------------------------------------
 * All depth-carrying queue hooks read pxQueue->uxMessagesWaiting directly.
 * The field is visible because these macros expand inside queue.c, and the
 * value is consistent because every call site already holds the kernel's
 * critical section (task paths) or interrupt mask (ISR paths).
 *
 * Calling uxQueueMessagesWaiting() here instead would be a portability
 * bug, not just a style issue:
 *   - It takes a TASK-level critical section.  On kernels older than
 *     V10.4.0, prvNotifyQueueSetContainer invokes traceQUEUE_SEND on the
 *     queue-set container — including from xQueueSendFromISR paths — and
 *     taskENTER_CRITICAL from ISR context corrupts the interrupt-mask
 *     bookkeeping (hard fault / lost ticks on Cortex-M0).
 *   - Even in task context it violates the frame-argument contract in
 *     docs/PITFALL_RTOS_CALLS_IN_FRAME_ARGUMENTS.md (taskEXIT_CRITICAL
 *     unconditionally re-enables interrupts when its own nesting hits
 *     zero, breaking TraxProbe's masked section open on PRIMASK ports).
 *     Here the kernel's own nesting happens to protect us, but the direct
 *     read removes the dependency on that coincidence — and is cheaper.
 */
#define traceQUEUE_SEND(pxQueue)                                               \
  TRAX_FRAME_ARGS_ATOMIC(                                                      \
      TRAX_TID_QUEUE_SEND, (uint32_t)(pxQueue),                                     \
      (uint32_t)(pxQueue)->uxMessagesWaiting)

#define traceQUEUE_RECEIVE(pxQueue)                                            \
  TRAX_FRAME_ARGS_ATOMIC(                                                      \
      TRAX_TID_QUEUE_RECEIVE, (uint32_t)(pxQueue),                                  \
      (uint32_t)(pxQueue)->uxMessagesWaiting)

#define traceQUEUE_SEND_FAILED(pxQueue)                                        \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_QUEUE_SEND_FAILED, (uint32_t)(pxQueue))

#define traceQUEUE_RECEIVE_FAILED(pxQueue)                                     \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_QUEUE_RECV_FAILED, (uint32_t)(pxQueue))

/*
 * ISR variants — fired by xQueueGenericSendFromISR / xQueueReceiveFromISR
 * (i.e. xSemaphoreGiveFromISR, xSemaphoreTakeFromISR, xQueueSendFromISR,
 * xQueueOverwriteFromISR, ...). FreeRTOS defines a SEPARATE macro family
 * for the ISR path and falls back to a no-op when these are undefined,
 * so without the four hooks below every ISR-side queue/semaphore op
 * is silently invisible in the trace.
 *
 * Depth source: pxQueue->uxMessagesWaiting directly, same rationale as
 * the task-context hooks above (see the "Depth source" block) — here it
 * is additionally mandatory because these hooks always run under
 * portSET_INTERRUPT_MASK_FROM_ISR(), where a task-level critical section
 * is never allowed.
 *
 * TID reuse
 * ---------
 * We deliberately reuse TRAX_TID_QUEUE_SEND / TRAX_TID_QUEUE_RECEIVE rather than
 * allocate ISR-specific TIDs. The host distinguishes ISR vs task
 * context from the surrounding TRAX_TID_ISR_ENTER / TRAX_TID_ISR_EXIT brackets,
 * and the semantics ("a permit was added/removed") are identical
 * regardless of caller context.
 *
 * Count value semantics
 * ---------------------
 * Both the task-context and the ISR-context hooks fire BEFORE the
 * kernel applies the queue mutation, so the count value carried in
 * the trace frame is the PRE-op count: for a Give showing items=N
 * the depth was N before the give and is N+1 after; for a Take
 * showing items=N the depth was N before the take and is N-1 after.
 * Verified empirically against a hardware capture (4-give burst into
 * an empty counting semaphore reports items=0,1,2,3; 4-take drain
 * reports items=4,3,2,1).
 */
#define traceQUEUE_SEND_FROM_ISR(pxQueue)                                      \
  TRAX_FRAME_ARGS_ATOMIC(                                                      \
      TRAX_TID_QUEUE_SEND, (uint32_t)(pxQueue),                                     \
      (uint32_t)(pxQueue)->uxMessagesWaiting)

#define traceQUEUE_RECEIVE_FROM_ISR(pxQueue)                                   \
  TRAX_FRAME_ARGS_ATOMIC(                                                      \
      TRAX_TID_QUEUE_RECEIVE, (uint32_t)(pxQueue),                                  \
      (uint32_t)(pxQueue)->uxMessagesWaiting)

#define traceQUEUE_SEND_FROM_ISR_FAILED(pxQueue)                               \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_QUEUE_SEND_FAILED, (uint32_t)(pxQueue))

#define traceQUEUE_RECEIVE_FROM_ISR_FAILED(pxQueue)                            \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_QUEUE_RECV_FAILED, (uint32_t)(pxQueue))

/*
 * traceBLOCKING_ON_QUEUE_SEND(pxQueue)
 * traceBLOCKING_ON_QUEUE_RECEIVE(pxQueue)
 * traceBLOCKING_ON_QUEUE_PEEK(pxQueue)
 *
 * Fired from xQueueGenericSend() / xQueueReceive() / xQueuePeek() in
 * queue.c the instant the kernel decides to park the calling task on
 * the queue's wait-list (full on send / empty on receive or peek, with
 * a non-zero timeout).  The matching `vTaskPlaceOnEventList(...)`
 * immediately follows the macro call site; pxCurrentTCB still points
 * at the about-to-block task at hook time, so we capture both the
 * queue handle and the task handle.  Distinct from the *_FAILED hooks
 * above (those fire when the operation gives up — task is still
 * RUNNING; here the task is about to leave RUNNING for BLOCKED).
 *
 * For sem/mutex objects the same RECEIVE hook fires (FreeRTOS
 * implements xSemaphoreTake / xSemaphoreTakeMutex through the queue
 * internals), so the host renders the event polymorphically — as
 * Q_RECV_BLOCK / SEM_TAKE_BLOCK / MTX_TAKE_BLOCK depending on the
 * object type byte recorded at create time.  The PEEK block hook does
 * NOT fire on sem/mutex paths in practice (those primitives consume
 * the permit on the take side and have no peek API), so it is queue-
 * specific in the trace.
 *
 * The wait is resolved by either a SUCCESS event (Q_SEND/Q_RECV/Q_PEEK
 * with the same handle some time later) or a *_FAILED event (the wait
 * timed out).  The two events bracket the BLOCKED slice on the
 * task's lane in tut15.4-style starvation diagnoses.
 */
#define traceBLOCKING_ON_QUEUE_SEND(pxQueue)                                   \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_QUEUE_SEND_BLOCK, (uint32_t)(pxQueue),            \
                         (uint32_t)pxCurrentTCB)

#define traceBLOCKING_ON_QUEUE_RECEIVE(pxQueue)                                \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_QUEUE_RECV_BLOCK, (uint32_t)(pxQueue),            \
                         (uint32_t)pxCurrentTCB)

#define traceBLOCKING_ON_QUEUE_PEEK(pxQueue)                                   \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_QUEUE_PEEK_BLOCK, (uint32_t)(pxQueue),            \
                         (uint32_t)pxCurrentTCB)

/*
 * Queue PEEK family — non-destructive read counterpart of the RECEIVE
 * family above.  Five FreeRTOS hooks (task ctx success/failed, ISR
 * ctx success/failed, plus the BLOCKING variant) collapse to three
 * TIDs using the same caller-context-via-ISR-bracket trick.
 *
 * Pre/post-op note: peek does NOT modify uxMessagesWaiting (that is
 * literally the point of peek — read without consuming), so the
 * items_waiting value carried in TRAX_TID_QUEUE_PEEK is simultaneously the
 * pre-op AND post-op depth.  Compare with TRAX_TID_QUEUE_SEND/RECEIVE
 * which carry strictly pre-op counts and need a +1 / -1 respectively
 * to read as post-op depth.
 */
#define traceQUEUE_PEEK(pxQueue)                                               \
  TRAX_FRAME_ARGS_ATOMIC(                                                      \
      TRAX_TID_QUEUE_PEEK, (uint32_t)(pxQueue),                                     \
      (uint32_t)(pxQueue)->uxMessagesWaiting)

#define traceQUEUE_PEEK_FAILED(pxQueue)                                        \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_QUEUE_PEEK_FAILED, (uint32_t)(pxQueue))

#define traceQUEUE_PEEK_FROM_ISR(pxQueue)                                      \
  TRAX_FRAME_ARGS_ATOMIC(                                                      \
      TRAX_TID_QUEUE_PEEK, (uint32_t)(pxQueue),                                     \
      (uint32_t)(pxQueue)->uxMessagesWaiting)

#define traceQUEUE_PEEK_FROM_ISR_FAILED(pxQueue)                               \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_QUEUE_PEEK_FAILED, (uint32_t)(pxQueue))

/*=============================================================================
 ====================OBJECT LIFECYCLE HOOKS====================================
 ============================================================================*/

/*
 * traceQUEUE_CREATE(pxNewQueue)
 *
 * Called from prvInitialiseNewQueue() in queue.c AFTER ucQueueType is set.
 * Fires for ALL types: queues, binary/counting semaphores, mutexes,
 * recursive mutexes. Queue_t fields are visible in that scope.
 */
#define traceQUEUE_CREATE(pxNewQueue)                                          \
  do {                                                                         \
    int _trax_idx = trax_rtos_object_create(                                   \
        (uint32_t)(pxNewQueue), (pxNewQueue)->ucQueueType,                     \
        (uint32_t)(pxNewQueue)->uxLength, (uint32_t)(pxNewQueue)->uxItemSize); \
    if (_trax_idx >= 0) {                                                      \
      (pxNewQueue)->uxQueueNumber = (UBaseType_t)_trax_idx;                    \
    }                                                                          \
  } while (0)

/*
 * traceQUEUE_DELETE(pxQueue)
 *
 * Called from vQueueDelete() in queue.c. Uses uxQueueNumber for O(1) lookup.
 */
#define traceQUEUE_DELETE(pxQueue)                                             \
  trax_rtos_object_delete((uint8_t)(pxQueue)->uxQueueNumber,                   \
                          (uint32_t)(pxQueue))

/*
 * traceQUEUE_REGISTRY_ADD(xQueue, pcQueueName)
 *
 * Called from vQueueAddToRegistry() in queue.c. The xQueue parameter
 * is QueueHandle_t (opaque), so we cast to Queue_t* to access uxQueueNumber.
 */
#define traceQUEUE_REGISTRY_ADD(xQueue, pcQueueName)                           \
  trax_rtos_object_set_name((uint8_t)((Queue_t *)(xQueue))->uxQueueNumber,     \
                            (uint32_t)(xQueue), (pcQueueName))

/*
 * traceQUEUE_SET_SEND(pxQueueSetContainer)
 *
 * Called from prvNotifyQueueSetContainer() in queue.c the instant a
 * send to a member queue/semaphore notifies the parent queue set —
 * specifically, after the "is there room in the set?" guard
 * (uxMessagesWaiting < uxLength) and just before prvCopyDataToQueue
 * pushes the member's handle into the set's internal queue.  Any
 * task blocked on xQueueSelectFromSet for this set will be unblocked
 * by the immediately-following enqueue, so a SWITCH_IN may follow
 * within the same critical section.
 *
 * Symbols in scope at hook expansion (V11 kernel):
 *   - pxQueueSetContainer : macro argument, the SET's Queue_t*
 *   - pxQueue             : enclosing function parameter
 *                           (prvNotifyQueueSetContainer( const Queue_t
 *                           * const pxQueue )), the MEMBER queue/sem
 *                           that received the data and triggered the
 *                           set notification.
 *
 * Wire payload [set_handle, member_handle] gives the host a complete
 * (set, member) pair — the renderer can show "set <S> notified by
 * send to <M>" without an osStore roundtrip.  Lands on the SET's
 * lane on the host (where blocked tasks unblock from), with the
 * member's name resolved into the card body for visual context.
 *
 * The hook fires only on the cold "member happens to be in a set"
 * path — standalone queues / semaphores never enter
 * prvNotifyQueueSetContainer — so hot-path overhead is zero for the
 * common case.  Single TRAX_FRAME_ARGS_ATOMIC, ~1.5us cost on the
 * cold path.
 *
 * Version gate: the dedicated traceQUEUE_SET_SEND hook exists from
 * V10.4.0.  On older kernels prvNotifyQueueSetContainer fires plain
 * traceQUEUE_SEND on the CONTAINER handle instead (verified against
 * V10.3.1 queue.c), so the set notification still reaches the host —
 * rendered as a queue send on the set object, without the member
 * handle.  The `pxQueue` reference below is the enclosing function's
 * parameter and only exists as such on >= 10.4 kernels, so the gate is
 * also a scope-correctness requirement.
 */
#if (TRAX_CFG_FREERTOS_VERSION >= TRAX_FREERTOS_VERSION(10, 4, 0))
#define traceQUEUE_SET_SEND(pxQueueSetContainer)                               \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_QUEUE_SET_SEND,                                   \
                         (uint32_t)(pxQueueSetContainer),                      \
                         (uint32_t)(pxQueue))
#endif

/*
 * traceTAKE_MUTEX_RECURSIVE(pxMutex)
 * traceTAKE_MUTEX_RECURSIVE_FAILED(pxMutex)
 * traceGIVE_MUTEX_RECURSIVE(pxMutex)
 * traceGIVE_MUTEX_RECURSIVE_FAILED(pxMutex)
 *
 * Guard semantics — why "undefined counts as enabled": this header is
 * parsed mid-FreeRTOSConfig.h, and configUSE_RECURSIVE_MUTEXES may
 * legitimately still be undefined here even though the FEATURE ends up
 * enabled (user defines it after the trax include, or a fork defaults
 * it).  A strict `== 1` guard would then silently compile the hooks
 * out and the recursive-mutex lane would just be missing — the worst
 * failure mode.  Defining the macros while the feature is OFF is
 * harmless: queue.c's recursive-mutex functions (the only expansion
 * sites, where u.xSemaphore.uxRecursiveCallCount is in scope) are
 * themselves compiled out, so the macro body is never instantiated.
 * The same permissive pattern is used for event groups and tickless
 * idle below.
 *
 * Hook fires from queue.c (xQueueGiveMutexRecursive /
 * xQueueTakeMutexRecursive — task context only; FreeRTOS does not
 * provide ISR variants for recursive mutexes, so no *_FROM_ISR
 * counterpart is needed).
 *
 *   TAKE_RECURSIVE         success (caller now owns it / count
 *                          incremented).  Hook fires BEFORE the
 *                          uxRecursiveCallCount++ — count_before==0
 *                          means this is the outermost take attempt
 *                          and we entered the xQueueSemaphoreTake
 *                          path; count_before>=1 means we already
 *                          owned it and the kernel will just bump
 *                          the counter without rescheduling.
 *
 *   TAKE_RECURSIVE_FAILED  outermost take attempt timed out (caller
 *                          did not previously own the mutex AND
 *                          xQueueSemaphoreTake returned pdFAIL).
 *                          uxRecursiveCallCount is whatever the
 *                          actual holder's nesting is — meaningless
 *                          to the caller — so we omit it.
 *
 *   GIVE_RECURSIVE         success (caller owns it / count
 *                          decremented).  Hook fires BEFORE the
 *                          uxRecursiveCallCount-- — count_before==1
 *                          means this is the OUTERMOST give and the
 *                          kernel will release the underlying
 *                          mutex; count_before>=2 means inner give
 *                          (just decrements counter).  Probe.cpp
 *                          uses count_before==1 as the trigger to
 *                          stamp hold-time, fixing the
 *                          long-documented "inner Take loses outer
 *                          time" limitation noted in Probe.cpp:998.
 *
 *   GIVE_RECURSIVE_FAILED  caller is not the mutex holder.
 *                          uxRecursiveCallCount is some other
 *                          task's nesting depth, so we omit it.
 *
 * Symbols in scope at hook expansion (V11, configUSE_RECURSIVE_MUTEXES==1):
 *   - pxMutex                                  : Queue_t* (macro arg)
 *   - pxMutex->u.xSemaphore.uxRecursiveCallCount : UBaseType_t,
 *                                                  visible because
 *                                                  these macros
 *                                                  expand inside
 *                                                  queue.c which has
 *                                                  full Queue_t
 *                                                  definition.
 *
 * Cost: single TRAX_FRAME_*_ATOMIC each, ~1.5us; only on the
 * (rare) recursive mutex code path.  Hot-path mutex / queue
 * traffic is unaffected.
 */
#if !defined(configUSE_RECURSIVE_MUTEXES) || (configUSE_RECURSIVE_MUTEXES == 1)

#define traceTAKE_MUTEX_RECURSIVE(pxMutex)                                     \
  TRAX_FRAME_ARGS_ATOMIC(                                                      \
      TRAX_TID_MUTEX_TAKE_RECURSIVE, (uint32_t)(pxMutex),                           \
      (uint32_t)(pxMutex)->u.xSemaphore.uxRecursiveCallCount)

#define traceTAKE_MUTEX_RECURSIVE_FAILED(pxMutex)                              \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_MUTEX_TAKE_RECURSIVE_FAILED, (uint32_t)(pxMutex))

#define traceGIVE_MUTEX_RECURSIVE(pxMutex)                                     \
  TRAX_FRAME_ARGS_ATOMIC(                                                      \
      TRAX_TID_MUTEX_GIVE_RECURSIVE, (uint32_t)(pxMutex),                           \
      (uint32_t)(pxMutex)->u.xSemaphore.uxRecursiveCallCount)

#define traceGIVE_MUTEX_RECURSIVE_FAILED(pxMutex)                              \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_MUTEX_GIVE_RECURSIVE_FAILED, (uint32_t)(pxMutex))

#endif /* configUSE_RECURSIVE_MUTEXES undefined or == 1 */

/*
 * traceQUEUE_CREATE_FAILED(ucQueueType)
 * traceCREATE_MUTEX_FAILED()
 * traceCREATE_COUNTING_SEMAPHORE_FAILED()
 *
 * All three fire from queue.c when the underlying pvPortMalloc returns
 * NULL (heap exhausted, fragmentation, or assertion-equivalent failure
 * on static builds).  Each dispatches to a class-specific TID
 * (TRAX_TID_QUEUE_CREATE_FAILED / TRAX_TID_BIN_SEM_CREATE_FAILED /
 * TRAX_TID_MUTEX_CREATE_FAILED / TRAX_TID_COUNTING_SEM_CREATE_FAILED), mirroring
 * the per-class create-success TIDs and matching upstream FreeRTOS's
 * deliberate split into three distinct trace-hook macros — so a raw
 * frame dump filterable by TID alone tells you exactly what failed,
 * without inspecting the payload.  No handle is emitted because the
 * object never existed and there is nothing to register in
 * trax_rtos_tables / osStore.
 *
 * traceQUEUE_CREATE_FAILED is invoked from xQueueGenericCreate for
 * BOTH queues (queueQUEUE_TYPE_BASE) and binary semaphores
 * (queueQUEUE_TYPE_BINARY_SEMAPHORE) — the same C code path serves
 * both create entry points.  The macro therefore branches on
 * ucQueueType to dispatch to the right TID; the branch lives only on
 * the cold malloc-fail path, so hot-path overhead is zero and the
 * wire format stays one-TID-per-class.  All other queueQUEUE_TYPE_*
 * values (RECURSIVE_MUTEX = 4 reaches us via traceCREATE_MUTEX_FAILED
 * from prvInitialiseMutex; MUTEX = 1 and COUNTING_SEMAPHORE = 2 have
 * their own dedicated entry points) collapse into the queue arm as a
 * defensive default — they shouldn't fire from xQueueGenericCreate in
 * V11, but if upstream changes the routing the event still surfaces
 * as a queue-class create-failed rather than being dropped.
 *
 * Symbols in scope at hook expansion (V11 kernel):
 *   - traceQUEUE_CREATE_FAILED:      ucQueueType (macro arg) and
 *                                    uxQueueLength (xQueueGenericCreate
 *                                    parameter) are both visible.  For
 *                                    binary semaphores uxQueueLength
 *                                    is always 1, so we drop the param
 *                                    entirely on the BIN_SEM TID.
 *   - traceCREATE_COUNTING_SEMAPHORE_FAILED: uxMaxCount
 *                                    (xQueueCreateCountingSemaphore
 *                                    parameter) is in scope.
 *   - traceCREATE_MUTEX_FAILED:      neither ucQueueType nor a length is
 *                                    in scope (hook fires inside
 *                                    prvInitialiseMutex with only the
 *                                    NULL pxNewQueue visible).  We have
 *                                    no useful payload — the TID alone
 *                                    carries the class identity.
 *                                    Recursive vs normal mutex isn't
 *                                    distinguishable here; both share
 *                                    TRAX_TID_MUTEX_CREATE_FAILED.
 *
 * Single TRAX_FRAME_*_ATOMIC each, ~1.5us cost, only on the rare
 * malloc-fail path so hot-path overhead is zero.
 */
#define traceQUEUE_CREATE_FAILED(ucQueueType)                                  \
  do {                                                                         \
    if ((ucQueueType) == queueQUEUE_TYPE_BINARY_SEMAPHORE) {                   \
      TRAX_FRAME_NOARGS_ATOMIC(TRAX_TID_BIN_SEM_CREATE_FAILED);                     \
    } else {                                                                   \
      TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_QUEUE_CREATE_FAILED,                          \
                             (uint32_t)(uxQueueLength));                       \
    }                                                                          \
  } while (0)

#define traceCREATE_MUTEX_FAILED()                                             \
  TRAX_FRAME_NOARGS_ATOMIC(TRAX_TID_MUTEX_CREATE_FAILED)

#define traceCREATE_COUNTING_SEMAPHORE_FAILED()                                \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_COUNTING_SEM_CREATE_FAILED,                       \
                         (uint32_t)(uxMaxCount))

/*=============================================================================
 ====================HEAP TRACE HOOKS==========================================
 ============================================================================*/

/*
 * traceMALLOC(pvAddress, uiSize) / traceFREE(pvAddress, uiSize)
 *
 * Called from pvPortMalloc() / vPortFree() in heap_*.c.
 * FreeRTOS provides xPortGetFreeHeapSize() to query the remaining heap.
 *
 * Frame: 24 bytes = Header(4) + Timestamp(4) + TID(4) + Address(4) + Size(4) +
 * Remaining(4)
 *
 * Each macro also bumps a free-running boot-time counter (declared in
 * trax_rtos_tables.h, defined in os/FreeRTOS/trax_freertos_heap.c).
 * These counters are harvested at session start and shipped in the
 * SESSION_START frame so the host can present truthful "since boot"
 * totals on a mid-flight connect.  Plain ++ is used: a missed count on
 * heavily-contended SMP is acceptable for a health-stat counter (the
 * trace frame itself uses the trax-buffer critical section for
 * correctness).
 */
#define traceMALLOC(pvAddress, uiSize)                                         \
  do {                                                                         \
    ++g_trax_heap_alloc_count;                                                 \
    TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_HEAP_ALLOC, (uint32_t)(pvAddress),              \
                           (uint32_t)(uiSize), (uint32_t)xPortGetFreeHeapSize());\
  } while (0)

#define traceFREE(pvAddress, uiSize)                                           \
  do {                                                                         \
    ++g_trax_heap_free_count;                                                  \
    TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_HEAP_FREE, (uint32_t)(pvAddress),               \
                           (uint32_t)(uiSize), (uint32_t)xPortGetFreeHeapSize());\
  } while (0)

#define traceMALLOC_FAILED()                                                   \
  do {                                                                         \
    ++g_trax_heap_alloc_fail_count;                                            \
    TRAX_FRAME_NOARGS_ATOMIC(TRAX_TID_HEAP_ALLOC_FAILED);                           \
  } while (0)

/*=============================================================================
====================SOFTWARE TIMER HOOKS======================================
============================================================================*/

/* Object-type byte for timers in the osStore (queue/sem/mutex use 0..5;
 * timer claims 6).  Keep in sync with osObjectTypeLabel() in the host
 * (UI/Traxcope/views/traceview/LocalStore.cpp). */
#ifndef TRAX_OBJ_TYPE_TIMER
#define TRAX_OBJ_TYPE_TIMER             6U
#endif

/* tmrSTATUS_IS_AUTORELOAD is private to timers.c (define is local to that
 * compilation unit), but the bit value is part of FreeRTOS's stable on-wire
 * Timer_t layout.  Re-declare locally so the macro is self-contained — if
 * FreeRTOS ever changes the bit, the build will silently emit the wrong
 * autoreload flag, which is why TIMER_CREATE comments below explicitly note
 * the dependency. */
#ifndef TRAX_TMR_STATUS_AUTORELOAD_BIT
#define TRAX_TMR_STATUS_AUTORELOAD_BIT  0x04U
#endif

/*
 * traceTIMER_CREATE(pxNewTimer)
 *
 * Called from prvInitialiseNewTimer() in timers.c with pxNewTimer typed as
 * Timer_t* (the struct definition is local to timers.c, so the field
 * accesses below only compile from within that translation unit — exactly
 * where the trace point fires).  Reuses TRAX_TID_OBJ_CREATE + TRAX_TID_OBJ_NAME so
 * timers slot into the existing osStore alongside queues/sems/mutexes; the
 * table_index returned by trax_rtos_object_create() is intentionally
 * discarded because Timer_t has no uxQueueNumber-equivalent field to stash
 * it in.  TIMER_DELETE / future name updates would need a scan-by-handle
 * helper — not in tut16's scope.
 *
 * length    = xTimerPeriodInTicks (host renders as period for tut16)
 * item_size = autoreload flag (0 = one-shot, 1 = auto-reload)
 */
#define traceTIMER_CREATE(pxNewTimer)                                          \
  do {                                                                         \
    int _trax_idx = trax_rtos_object_create(                                   \
        (uint32_t)(pxNewTimer), TRAX_OBJ_TYPE_TIMER,                           \
        (uint32_t)((pxNewTimer)->xTimerPeriodInTicks),                         \
        (uint32_t)(((pxNewTimer)->ucStatus &                                   \
                    TRAX_TMR_STATUS_AUTORELOAD_BIT) ? 1U : 0U));               \
    if (_trax_idx >= 0 && (pxNewTimer)->pcTimerName != NULL) {                 \
      trax_rtos_object_set_name((uint8_t)_trax_idx, (uint32_t)(pxNewTimer),    \
                                (pxNewTimer)->pcTimerName);                    \
    }                                                                          \
  } while (0)

/*
 * traceTIMER_COMMAND_SEND(xTimer, xMessageID, xMessageValueValue, xReturn)
 *
 * Called from xTimerGenericCommandFromTask() / xTimerGenericCommandFromISR()
 * right after xQueueSendToBack() pushes the DaemonTaskMessage_t onto
 * xTimerQueue.  Fires in the CALLER's context (task ctx for *FromTask, ISR
 * ctx for *FromISR — the surrounding ISR brackets distinguish them, same
 * trick the QUEUE family uses).  Lands on the timer's own lane so the
 * reader sees "this command was sent to timer X".  The xMessageValueValue
 * parameter is the new period for CHANGE_PERIOD or the tick count for
 * START / RESET — sacrificed in the trace because the host can derive it
 * from the matching TIMER_COMMAND_RECEIVED event when needed (and pairing
 * SEND/RECEIVED by command_id alone is the marquee tut16.2 measurement).
 */
#define traceTIMER_COMMAND_SEND(xTimer, xMessageID, xMessageValueValue,        \
                                xReturn)                                       \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_TIMER_COMMAND_SEND, (uint32_t)(xTimer),           \
                         (uint32_t)(xMessageID))

/*
 * traceTIMER_COMMAND_RECEIVED(pxTimer, xMessageID, xMessageValue)
 *
 * Called from prvProcessReceivedCommands() inside the timer service task
 * ("Tmr Svc") after it pops a command off xTimerQueue and before it acts
 * on it.  Fires in the timer-service-task context — pxCurrentTCB at this
 * point is always the Tmr Svc task, so no sender field is needed (the
 * SEND event already disclosed the original caller via its own ISR
 * bracketing).  Same [handle, command_id] payload as SEND so the host can
 * pair them and surface the SEND→RECEIVED gap.
 *
 * tmrCOMMAND_DELETE special case — upstream FreeRTOS V11 has no
 * dedicated traceTIMER_DELETE hook; the timer's storage cleanup
 * happens further down inside the same prvProcessReceivedCommands()
 * call (vPortFree for dynamic timers, ucStatus auto-reload bit
 * clear for static timers).  We piggy-back on this hook — the only
 * V11 trace point where pxTimer is still valid AND we know the
 * delete is about to happen — to fire TRAX_TID_OBJ_DELETE and remove the
 * timer from osStore in lock-step with the kernel's deletion.  Per
 * FreeRTOS docs ("xTimerDelete() makes a timer become invalid; it is
 * NOT safe to use the timer's handle after calling xTimerDelete()"),
 * the user's intent is "this timer is gone after this call" even
 * for statically-allocated timers where the kernel only clears the
 * auto-reload flag — our trace honours that intent.  The OBJ_DELETE
 * frame uses the same wire format as queue / stream-buffer delete
 * (TRAX_TID_OBJ_DELETE + handle), so the host pipeline needs no
 * timer-specific handling — EventType::ObjectDelete already does
 * the right thing.
 */
#define traceTIMER_COMMAND_RECEIVED(pxTimer, xMessageID, xMessageValue)        \
  do {                                                                         \
    TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_TIMER_COMMAND_RECEIVED, (uint32_t)(pxTimer),    \
                           (uint32_t)(xMessageID));                            \
    if ((xMessageID) == tmrCOMMAND_DELETE) {                                   \
      trax_rtos_object_delete_by_handle((uint32_t)(pxTimer));                  \
    }                                                                          \
  } while (0)

/*
 * traceTIMER_EXPIRED(pxTimer)
 *
 * Called from prvProcessExpiredTimer() / prvProcessReceivedCommands() the
 * instant before pxCallbackFunction(pxTimer) is invoked.  Fires in the
 * timer-service-task context (the callback runs there too — auto-reload
 * timers fire repeatedly in this task's lane, never preempting application
 * tasks unless they raise the Tmr Svc priority).  Headline event for
 * tut16: actual vs. requested fire interval (drift) for auto-reload
 * timers and one-shot completion confirmation.
 */
#define traceTIMER_EXPIRED(pxTimer)                                            \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_TIMER_EXPIRED, (uint32_t)(pxTimer))

/*=============================================================================
====================STREAM / MESSAGE BUFFER HOOKS=============================
============================================================================*/

/* Object-type bytes for stream/message/batching buffers in the osStore
 * (queue/sem/mutex use 0..5; timer claims 6).  Computed as 7 +
 * xStreamBufferType so the FreeRTOS-public sbTYPE_* enum (0/1/2 in
 * stream_buffer.h) maps directly onto our obj_type byte (7/8/9).  Keep
 * in sync with osObjectTypeLabel() in the host (LocalStore.cpp).
 *
 * Version note: sbTYPE_* and the third (batching, 2) value exist since
 * V11.1.0.  On older kernels traceSTREAM_BUFFER_CREATE's second argument
 * is `xIsMessageBuffer` (BaseType_t 0/1), which maps onto the same first
 * two obj_type values — so the single definition below is correct across
 * the whole supported range with no gate. */
#ifndef TRAX_OBJ_TYPE_STREAM_BUFFER
#define TRAX_OBJ_TYPE_STREAM_BUFFER     7U  /* sbTYPE_STREAM_BUFFER          (0) + 7 */
#endif
#ifndef TRAX_OBJ_TYPE_MESSAGE_BUFFER
#define TRAX_OBJ_TYPE_MESSAGE_BUFFER    8U  /* sbTYPE_MESSAGE_BUFFER         (1) + 7 */
#endif
#ifndef TRAX_OBJ_TYPE_BATCHING_BUFFER
#define TRAX_OBJ_TYPE_BATCHING_BUFFER   9U  /* sbTYPE_STREAM_BATCHING_BUFFER (2) + 7 */
#endif

/*
 * traceSTREAM_BUFFER_CREATE(pxStreamBuffer, xStreamBufferType)
 *
 * Called from xStreamBufferGenericCreate() / xStreamBufferGenericCreateStatic()
 * in stream_buffer.c after the buffer is fully initialized.  Reuses
 * TRAX_TID_OBJ_CREATE with obj_type = 7/8/9 so stream / message / batching
 * buffers appear in osStore as a normal RTOS object — name resolution,
 * filtering, lane routing all come for free via the existing OBJ_CREATE
 * decoder path.  The table_index returned by trax_rtos_object_create()
 * is intentionally discarded (StreamBuffer_t has no uxQueueNumber-
 * equivalent field to stash it in — same constraint as Timer_t).
 * traceSTREAM_BUFFER_DELETE works around this by scanning the table
 * for the matching handle via trax_rtos_object_delete_by_handle()
 * (O(n) but called only at delete time, which is rare).
 *
 * No name is set at create time because StreamBuffer_t has no pcName-
 * equivalent field (unlike Timer_t which carries pcTimerName).  The
 * host renderer falls back to "0x..." hex display.  Users wanting
 * named stream buffers can call trax_rtos_object_set_name_by_handle()
 * directly (also a scan-by-handle helper) immediately after
 * xStreamBufferCreate() returns — same scan-pay-once pattern.
 *
 * Field accesses below only compile inside stream_buffer.c (StreamBuffer_t
 * struct definition is local to that translation unit), which is exactly
 * where this trace point fires.
 *
 * length    = xLength             (total buffer size in bytes)
 * item_size = xTriggerLevelBytes  (receive trigger level — 1 by default;
 *                                  for message buffers this is the
 *                                  minimum-bytes threshold for unblocking
 *                                  a waiting receiver)
 */
#define traceSTREAM_BUFFER_CREATE(pxStreamBuffer, xStreamBufferType)            \
  (void)trax_rtos_object_create(                                                \
      (uint32_t)(pxStreamBuffer),                                               \
      (uint8_t)(TRAX_OBJ_TYPE_STREAM_BUFFER + (uint8_t)(xStreamBufferType)),    \
      (uint32_t)((pxStreamBuffer)->xLength),                                    \
      (uint32_t)((pxStreamBuffer)->xTriggerLevelBytes))

/*
 * traceSTREAM_BUFFER_DELETE(xStreamBuffer)
 *
 * Called from vStreamBufferDelete() in stream_buffer.c just before
 * the buffer's storage is released (heap path) or marked unused
 * (static path).  StreamBuffer_t has no uxQueueNumber-equivalent
 * field where traceSTREAM_BUFFER_CREATE could have stashed the
 * table_index returned by trax_rtos_object_create(), so we do an
 * O(n) scan over the object table for the matching handle via
 * trax_rtos_object_delete_by_handle().  This is the same scan-helper
 * pattern that traceTIMER_DELETE uses (Timer_t has the same
 * limitation).  No-op (silently) if the handle is not in the table —
 * happens when the buffer was created before TraxProbe started
 * streaming, or when TRAX_CFG_MAX_RTOS_OBJECTS is too small to track
 * all live objects.  Per-delete cost is bounded by the table size
 * (default 64); deletes are rare in well-designed RTOS apps so the
 * O(n) scan is acceptable.
 *
 * The host pipeline reuses the existing TRAX_TID_OBJ_DELETE path
 * (EventType::ObjectDelete) — no host-side changes required since
 * SB delete is semantically identical to queue delete from osStore's
 * perspective (object is gone from this point on, name resolution
 * for past references stays valid via the historical record).
 */
#define traceSTREAM_BUFFER_DELETE(xStreamBuffer)                               \
  trax_rtos_object_delete_by_handle((uint32_t)(xStreamBuffer))

/*
 * traceSTREAM_BUFFER_SEND(xStreamBuffer, xBytesSent)
 *
 * Called from xStreamBufferSend() in the success branch (xReturn > 0).
 * Lands on the buffer's own object lane.  bytes_sent may be < the
 * caller's requested length if the buffer didn't have enough free
 * space and xTicksToWait elapsed (partial send is normal for stream
 * buffers; for message buffers it's an all-or-nothing operation, so
 * bytes_sent == requested or the call hits the FAILED path).
 */
#define traceSTREAM_BUFFER_SEND(xStreamBuffer, xBytesSent)                      \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_STREAM_BUFFER_SEND, (uint32_t)(xStreamBuffer),     \
                         (uint32_t)(xBytesSent))

/*
 * traceSTREAM_BUFFER_SEND_FAILED(xStreamBuffer)
 *
 * Called from xStreamBufferSend() when no bytes were accepted (full
 * timeout elapsed without space, OR for message buffers when the
 * message length exceeded the buffer capacity).
 */
#define traceSTREAM_BUFFER_SEND_FAILED(xStreamBuffer)                           \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_STREAM_BUFFER_SEND_FAILED,                         \
                         (uint32_t)(xStreamBuffer))

/*
 * traceSTREAM_BUFFER_SEND_FROM_ISR(xStreamBuffer, xBytesSent)
 *
 * Called from xStreamBufferSendFromISR() unconditionally — xBytesSent
 * may be 0 (no space, fire-and-forget failure: ISRs can't block).
 * Firmware-side discriminates SUCCESS vs FAILURE by branching on
 * xBytesSent so the host always sees a distinct SEND vs SEND_FAILED
 * TID and doesn't have to special-case "ISR send with zero count =
 * failure" rendering.  The do/while wrapper makes the macro a single
 * statement (FreeRTOS expands trace macros at statement positions
 * inside ISR-context functions, so a bare if/else would be a syntax
 * error if the call site is followed by an else clause — defensive).
 */
#define traceSTREAM_BUFFER_SEND_FROM_ISR(xStreamBuffer, xBytesSent)             \
  do {                                                                          \
    if ((size_t)(xBytesSent) > (size_t)0) {                                     \
      TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_STREAM_BUFFER_SEND,                            \
                             (uint32_t)(xStreamBuffer),                         \
                             (uint32_t)(xBytesSent));                           \
    } else {                                                                    \
      TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_STREAM_BUFFER_SEND_FAILED,                     \
                             (uint32_t)(xStreamBuffer));                        \
    }                                                                           \
  } while (0)

/*
 * traceBLOCKING_ON_STREAM_BUFFER_SEND(xStreamBuffer)
 *
 * Called from xStreamBufferSend() inside the same critical section
 * that vTaskPlaceOnEventList() uses, so pxCurrentTCB is guaranteed to
 * point at the about-to-block task at hook time.  Same pattern as
 * traceBLOCKING_ON_QUEUE_SEND — the host pairs this with the SWITCH_OUT
 * that immediately follows to surface the BLOCKED-on-buffer slice.
 * Only meaningful in task ctx (ISR sends never block).
 */
#define traceBLOCKING_ON_STREAM_BUFFER_SEND(xStreamBuffer)                      \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_STREAM_BUFFER_SEND_BLOCK,                          \
                         (uint32_t)(xStreamBuffer),                             \
                         (uint32_t)pxCurrentTCB)

/*
 * traceSTREAM_BUFFER_RECEIVE(xStreamBuffer, xBytesReceived)
 *
 * Mirror of SEND on the receive side.  Called from xStreamBufferReceive()
 * in the success branch (xReceivedLength > 0).  For message buffers the
 * value is the message length (the leading 4-byte length-prefix is
 * stripped by FreeRTOS — host renderer just shows "bytes" since the
 * read API is bytes-based even for message buffers).
 */
#define traceSTREAM_BUFFER_RECEIVE(xStreamBuffer, xBytesReceived)               \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_STREAM_BUFFER_RECV, (uint32_t)(xStreamBuffer),     \
                         (uint32_t)(xBytesReceived))

/*
 * traceSTREAM_BUFFER_RECEIVE_FAILED(xStreamBuffer)
 *
 * Called from xStreamBufferReceive() when no bytes were delivered —
 * this is either "trigger level not reached within timeout" (the
 * SB-specific failure mode that has no queue analogue) or "buffer
 * empty + xTicksToWait was 0".
 */
#define traceSTREAM_BUFFER_RECEIVE_FAILED(xStreamBuffer)                        \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_STREAM_BUFFER_RECV_FAILED,                         \
                         (uint32_t)(xStreamBuffer))

/*
 * traceSTREAM_BUFFER_RECEIVE_FROM_ISR(xStreamBuffer, xBytesReceived)
 *
 * Mirror of SEND_FROM_ISR on the receive side.  Same firmware-side
 * SUCCESS/FAILURE split — host sees TRAX_TID_STREAM_BUFFER_RECV vs
 * TRAX_TID_STREAM_BUFFER_RECV_FAILED, never an ambiguous "RECV with zero
 * count".
 */
#define traceSTREAM_BUFFER_RECEIVE_FROM_ISR(xStreamBuffer, xBytesReceived)      \
  do {                                                                          \
    if ((size_t)(xBytesReceived) > (size_t)0) {                                 \
      TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_STREAM_BUFFER_RECV,                            \
                             (uint32_t)(xStreamBuffer),                         \
                             (uint32_t)(xBytesReceived));                       \
    } else {                                                                    \
      TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_STREAM_BUFFER_RECV_FAILED,                     \
                             (uint32_t)(xStreamBuffer));                        \
    }                                                                           \
  } while (0)

/*
 * traceBLOCKING_ON_STREAM_BUFFER_RECEIVE(xStreamBuffer)
 *
 * Mirror of BLOCKING_ON_STREAM_BUFFER_SEND on the receive side.
 * Fires from inside the critical section that parks the calling task
 * on the buffer's xWaitingToReceive list.  pxCurrentTCB resolves the
 * about-to-block task.
 */
#define traceBLOCKING_ON_STREAM_BUFFER_RECEIVE(xStreamBuffer)                   \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_STREAM_BUFFER_RECV_BLOCK,                          \
                         (uint32_t)(xStreamBuffer),                             \
                         (uint32_t)pxCurrentTCB)

/*
 * traceSTREAM_BUFFER_RESET(xStreamBuffer)
 * traceSTREAM_BUFFER_RESET_FROM_ISR(xStreamBuffer)
 *
 * Called from xStreamBufferReset() / xStreamBufferResetFromISR() after
 * the buffer's head/tail pointers have been zeroed.  Single TID — the
 * caller context (task vs ISR) is reconstructed from the surrounding
 * ISR brackets, same trick as the queue family.  No before/after state
 * distinction because reset always succeeds when called (the function
 * returns pdFAIL only if a task is blocked on the buffer, in which
 * case the caller is responsible for handling the failure; the trace
 * just records "reset attempted").
 *
 * Version note: xStreamBufferResetFromISR (and its hook) exists since
 * V11.1.0.  Defining the macro on older kernels is harmless — nothing
 * references it — so no gate is needed.
 */
#define traceSTREAM_BUFFER_RESET(xStreamBuffer)                                 \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_STREAM_BUFFER_RESET, (uint32_t)(xStreamBuffer))

#define traceSTREAM_BUFFER_RESET_FROM_ISR(xStreamBuffer)                        \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_STREAM_BUFFER_RESET, (uint32_t)(xStreamBuffer))

/* ---------------------------------------------------------------------------
 * Event groups — config-gated by configUSE_EVENT_GROUPS (permissive form)
 * ---------------------------------------------------------------------------
 *
 * GUARD SEMANTICS — the strict `== 1` form was a latent bug on EVERY
 * kernel: configUSE_EVENT_GROUPS only exists since FreeRTOS V11.1.0, and
 * even there it is defaulted to 1 by FreeRTOS.h AFTER this header has
 * been parsed.  So unless the user explicitly defined it in
 * FreeRTOSConfig.h before the trax include, `#if (configUSE_EVENT_GROUPS
 * == 1)` evaluated false and silently compiled out every event-group
 * hook — on V10.x kernels (macro never exists) AND on V11.1+ defaults.
 * "Undefined" must therefore count as ENABLED; only an explicit
 * user-defined 0 disables the hooks (matching the kernel, which then
 * compiles out event_groups.c — the only place these macros expand).
 *
 * FreeRTOS event groups are bitmask-based synchronization primitives —
 * each group holds a 24-bit (or 8-bit, with configUSE_16_BIT_TICKS)
 * value and tasks wait for specific bit patterns to appear.  The 11
 * trace hooks in event_groups.c collapse to 7 wire TIDs after merging
 * the queue-family-style ISR/non-ISR pairs (SET_BITS / CLEAR_BITS) —
 * the host reconstructs caller context from the surrounding ISR
 * brackets the same way it does for queues / sems / SBs, halving the
 * TID space cost for what's purely an ergonomics distinction.
 *
 * Lifecycle uses the existing object-registration infrastructure:
 *   CREATE       → trax_rtos_object_register(handle, NULL, 10, 0, 0)
 *                  with objType = 10 (event_group) — appears in osStore
 *                  and inherits name resolution / filtering "for free",
 *                  same pattern timers (objType=6) and SBs (objType=7-9)
 *                  follow.
 *   DELETE       → trax_rtos_object_delete_by_handle(handle) — emits
 *                  TRAX_TID_OBJ_DELETE.  Same P1 helper stream buffers and
 *                  timers use, no new firmware code needed.
 *   CREATE_FAILED → TRAX_TID_EVT_GROUP_CREATE_FAILED (no payload — the
 *                  failure tells the user "OOM at create time"; there's
 *                  no per-class tuning info we could carry that the
 *                  HEAP_ALLOC_FAILED stream wouldn't already provide).
 *
 * No SYNC_END / WAIT_BITS_END enrichment in this layer — the host
 * splits success vs timeout by the timeout_flag wire param and renders
 * differently per EventType.  Future enrichment (e.g. block-time
 * computation between BLOCK and END) can layer onto Probe.cpp without
 * touching firmware.
 */
#ifndef TRAX_OBJ_TYPE_EVENT_GROUP
#define TRAX_OBJ_TYPE_EVENT_GROUP       10U
#endif

#if !defined(configUSE_EVENT_GROUPS) || (configUSE_EVENT_GROUPS == 1)

/*
 * traceEVENT_GROUP_CREATE(xEventGroup)
 *
 * Called from xEventGroupCreate / xEventGroupCreateStatic immediately
 * after the EventGroup_t allocation succeeds.  Registers the group in
 * trax_rtos's object table as obj_type = TRAX_OBJ_TYPE_EVENT_GROUP
 * (10) — name comes through later via traceQUEUE_REGISTRY (event
 * groups share queue's registry plumbing in V11, so xQueueAddToRegistry
 * on an event-group handle hits the same OBJ_NAME path queues/sems
 * already do, no separate firmware hook needed).  length=0 / item_size=0
 * because event groups have no capacity concept — a group IS the
 * bitmask, not a buffer of items.  Return value (table_index) is
 * intentionally discarded, mirroring traceSTREAM_BUFFER_CREATE: name
 * resolution will rediscover it via the scan-by-handle helper if and
 * when xQueueAddToRegistry fires later in the boot sequence.
 */
#define traceEVENT_GROUP_CREATE(xEventGroup)                                   \
  (void)trax_rtos_object_create((uint32_t)(xEventGroup),                       \
                                TRAX_OBJ_TYPE_EVENT_GROUP, 0U, 0U)

/*
 * traceEVENT_GROUP_CREATE_FAILED()
 *
 * Called from xEventGroupCreate when the underlying pvPortMalloc
 * returned NULL.  No handle to carry (allocation never succeeded);
 * payload is empty, the host pairs the event with the immediately-
 * preceding HEAP_ALLOC_FAILED if the user wants the request size.
 */
#define traceEVENT_GROUP_CREATE_FAILED()                                       \
  TRAX_FRAME_NOARGS_ATOMIC(TRAX_TID_EVT_GROUP_CREATE_FAILED)

/*
 * traceEVENT_GROUP_DELETE(xEventGroup)
 *
 * Called from vEventGroupDelete before the kernel tears down the
 * waiting-tasks list and (for dynamic groups) calls vPortFree.  Fires
 * trax_rtos_object_delete_by_handle which emits TRAX_TID_OBJ_DELETE and
 * removes the entry from osStore — same path stream buffers / timers
 * use.  Doing the host notification BEFORE the actual teardown means
 * the handle is still valid; doing it inside the macro (vs intercepting
 * vPortFree) means static event groups also get a delete event.
 */
#define traceEVENT_GROUP_DELETE(xEventGroup)                                   \
  trax_rtos_object_delete_by_handle((uint32_t)(xEventGroup))

/*
 * traceEVENT_GROUP_SET_BITS(xEventGroup, uxBitsToSet)
 * traceEVENT_GROUP_SET_BITS_FROM_ISR(xEventGroup, uxBitsToSet)
 *
 * Called from xEventGroupSetBits / xEventGroupSetBitsFromISR after the
 * bits have been OR-ed into pxEventBits->uxEventBits and any waiters
 * potentially unblocked.  Single TID across both contexts — the host
 * reconstructs task vs ISR from the surrounding ISR brackets, same
 * trick the queue/sem/SB families use.  Bits cast to uint32_t to
 * normalize the wire format across configUSE_16_BIT_TICKS variants.
 */
#define traceEVENT_GROUP_SET_BITS(xEventGroup, uxBitsToSet)                    \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_EVT_GROUP_SET_BITS,                               \
                         (uint32_t)(xEventGroup), (uint32_t)(uxBitsToSet))

#define traceEVENT_GROUP_SET_BITS_FROM_ISR(xEventGroup, uxBitsToSet)           \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_EVT_GROUP_SET_BITS,                               \
                         (uint32_t)(xEventGroup), (uint32_t)(uxBitsToSet))

/*
 * traceEVENT_GROUP_CLEAR_BITS(xEventGroup, uxBitsToClear)
 * traceEVENT_GROUP_CLEAR_BITS_FROM_ISR(xEventGroup, uxBitsToClear)
 *
 * Mirror of SET_BITS on the clear side.  The kernel masks the named
 * bits OUT of uxEventBits.  Single TID, same context-reconstruction
 * pattern.
 */
#define traceEVENT_GROUP_CLEAR_BITS(xEventGroup, uxBitsToClear)                \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_EVT_GROUP_CLEAR_BITS,                             \
                         (uint32_t)(xEventGroup), (uint32_t)(uxBitsToClear))

#define traceEVENT_GROUP_CLEAR_BITS_FROM_ISR(xEventGroup, uxBitsToClear)       \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_EVT_GROUP_CLEAR_BITS,                             \
                         (uint32_t)(xEventGroup), (uint32_t)(uxBitsToClear))

/*
 * traceEVENT_GROUP_WAIT_BITS_BLOCK(xEventGroup, uxBitsToWaitFor)
 *
 * Called from xEventGroupWaitBits inside the same critical section
 * that vTaskPlaceOnUnorderedEventList uses, so pxCurrentTCB is
 * guaranteed to point at the about-to-block task at hook time.  The
 * host pairs this with the SWITCH_OUT that immediately follows to
 * surface the BLOCKED-on-event-group slice.  No ISR variant exists
 * (xEventGroupWaitBitsFromISR doesn't exist either — wait operations
 * are inherently task-context).
 */
#define traceEVENT_GROUP_WAIT_BITS_BLOCK(xEventGroup, uxBitsToWaitFor)         \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_EVT_GROUP_WAIT_BITS_BLOCK,                        \
                         (uint32_t)(xEventGroup), (uint32_t)(uxBitsToWaitFor))

/*
 * traceEVENT_GROUP_WAIT_BITS_END(xEventGroup, uxBitsToWaitFor, xTimeoutOccurred)
 *
 * Called from xEventGroupWaitBits unconditionally at the end of the
 * function — fires whether the task blocked or not.  xTimeoutOccurred
 * is pdFALSE when the wait was satisfied (bits matched, possibly
 * without ever blocking) and pdTRUE when xTicksToWait expired before
 * the bits were set.  Host decoder splits on this flag into two
 * EventTypes (EvtGroupWaitBitsEnd vs EvtGroupWaitBitsTimeout) so the
 * UI can color/filter success vs failure separately, same way the
 * queue family splits Send vs SendFailed.
 */
#define traceEVENT_GROUP_WAIT_BITS_END(xEventGroup, uxBitsToWaitFor,           \
                                       xTimeoutOccurred)                       \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_EVT_GROUP_WAIT_BITS_END,                          \
                         (uint32_t)(xEventGroup),                              \
                         (uint32_t)(uxBitsToWaitFor),                          \
                         (uint32_t)(xTimeoutOccurred))

/*
 * traceEVENT_GROUP_SYNC_BLOCK(xEventGroup, uxBitsToSet, uxBitsToWaitFor)
 *
 * Called from xEventGroupSync — the rendezvous primitive where each
 * task sets its arrival bit and waits for ALL named bits to appear
 * (everyone has arrived).  Fires inside the kernel's critical section
 * before vTaskPlaceOnUnorderedEventList, same pattern as WAIT_BITS_BLOCK.
 * Carries both bitmasks because the rendezvous is identified by the
 * (set, wait_for) tuple — host's lane shows what arm of a barrier
 * this caller is.
 */
#define traceEVENT_GROUP_SYNC_BLOCK(xEventGroup, uxBitsToSet, uxBitsToWaitFor) \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_EVT_GROUP_SYNC_BLOCK,                             \
                         (uint32_t)(xEventGroup),                              \
                         (uint32_t)(uxBitsToSet),                              \
                         (uint32_t)(uxBitsToWaitFor))

/*
 * traceEVENT_GROUP_SYNC_END(xEventGroup, uxBitsToSet, uxBitsToWaitFor,
 *                           xTimeoutOccurred)
 *
 * Mirror of WAIT_BITS_END for sync rendezvous.  Same timeout-flag
 * split semantic — host decoder produces EvtGroupSyncEnd or
 * EvtGroupSyncTimeout based on the wire flag.  4 args, well within
 * TRAX_FRAME_ARGS_ATOMIC's 8-param limit.
 */
#define traceEVENT_GROUP_SYNC_END(xEventGroup, uxBitsToSet, uxBitsToWaitFor,   \
                                  xTimeoutOccurred)                            \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_EVT_GROUP_SYNC_END,                               \
                         (uint32_t)(xEventGroup),                              \
                         (uint32_t)(uxBitsToSet),                              \
                         (uint32_t)(uxBitsToWaitFor),                          \
                         (uint32_t)(xTimeoutOccurred))

#endif /* configUSE_EVENT_GROUPS undefined or == 1 */

/* ---------------------------------------------------------------------------
 * Tickless idle / low-power sleep — config-gated by configUSE_TICKLESS_IDLE
 * ---------------------------------------------------------------------------
 *
 * Tickless idle replaces the idle task's busy-wait WFI with a port-supplied
 * portSUPPRESS_TICKS_AND_SLEEP() that puts the MCU into deep sleep (STOP /
 * standby on STM32, sleep mode on Nordic, etc.) and silences SysTick for
 * the duration.  When the part wakes (any IRQ, RTC alarm, etc.) the kernel
 * fast-forwards its tick counter by however many SysTick periods elapsed.
 *
 * The kernel exposes three trace hooks for this whole dance:
 *
 *   traceLOW_POWER_IDLE_BEGIN  — fires immediately before the port enters
 *     deep sleep.  Bracketing event for the sleep window.  After this
 *     point THIS CORE will emit no further trace events until END (other
 *     cores in SMP keep emitting normally — the brackets are per-core).
 *
 *   traceLOW_POWER_IDLE_END    — fires immediately after the port returns.
 *     Host pairs it with the preceding BEGIN to compute sleep duration
 *     and renders "Slept for N µs" on this event (mutex-hold-time pattern).
 *
 *   traceINCREASE_TICK_COUNT(xTicksToJump) — fires from vTaskStepTick after
 *     wake to announce how many SysTick periods were skipped (the kernel
 *     pretends that many tick ISRs happened so software-timer / delay
 *     bookkeeping stays consistent).  Useful diagnostic for verifying the
 *     port's sleep-timer math vs the actual wake source latency.
 *
 * No-args macros are TRAX_FRAME_NOARGS_ATOMIC (timestamp-only payload —
 * the wire is the smallest possible frame, hot-path-friendly given these
 * fire on every idle entry/exit cycle and a heavy MCU may sleep dozens
 * of times per second).  The guard uses the permissive "undefined counts
 * as enabled" form (see the recursive-mutex block for the include-order
 * rationale): a macro that is defined but whose feature is off costs
 * nothing — the kernel only expands these hooks from the tickless code
 * paths, which are themselves compiled out.
 *
 * Note: BEGIN/END deliberately do NOT carry pxCurrentTCB even though the
 * idle task is the trigger — there is exactly one idle task per core and
 * the host already tracks current-context per core via the SWITCH_IN
 * stream, so re-emitting the handle would be redundant on the wire.
 */
#if !defined(configUSE_TICKLESS_IDLE) || (configUSE_TICKLESS_IDLE != 0)

#define traceLOW_POWER_IDLE_BEGIN()                                            \
  TRAX_FRAME_NOARGS_ATOMIC(TRAX_TID_LOW_POWER_IDLE_BEGIN)

#define traceLOW_POWER_IDLE_END()                                              \
  TRAX_FRAME_NOARGS_ATOMIC(TRAX_TID_LOW_POWER_IDLE_END)

#define traceINCREASE_TICK_COUNT(xTicksToJump)                                 \
  TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_INCREASE_TICK_COUNT, (uint32_t)(xTicksToJump))

#endif /* configUSE_TICKLESS_IDLE undefined or != 0 */

#endif /* TRAX_ENABLE */

#endif /* TRAX_FREERTOS_PORT_H_ */
