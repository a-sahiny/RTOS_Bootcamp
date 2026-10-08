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
 * @file           : trax_tid.h
 * @brief          : TraxProbe Trace ID (TID) Definitions
 * @version        : 2.0.0
 ******************************************************************************
 * @attention
 * 
 * This file defines Trace IDs (TIDs) used by TraxProbe.
 * 
 * TID ARCHITECTURE:
 *   - 16-bit value identifying each trace item
 *   - PC application uses TID to look up metadata
 *   - Ranges prevent collisions between different trace types
 * 
 * TID RANGES:
 *   0x0000 - 0x00FF : Reserved (system/protocol)
 *   0x0100 - 0x1FFF : LOG messages
 *                       0x0100 - 0x010F : library-reserved (port-internal,
 *                                          e.g. TRAX_TID_LOG_DIAG for default
 *                                          diagnostic event logs emitted
 *                                          by trax_diag_report())
 *                       0x0110 - 0x1FFF : user LOG messages
 *                                          (start enums at TRAX_TID_RANGE_LOG_USER_START)
 *   0x2000 - 0x2FFF : VAR (variables/signals)
 *   0x3000 - 0x3FFF : STREAM (high-rate variable data)
 *   0x4000 - 0x4FFF : ISR (interrupt handlers)
 *                       0x4000 - 0x400F : library-reserved (port-internal,
 *                                          e.g. TRAX_TID_ISR_TICK for the
 *                                          RTOS-owned SysTick)
 *                       0x4010 - 0x4FFF : user ISRs
 *                                          (start enums at TRAX_TID_RANGE_ISR_USER_START)
 *   0x5000 - 0x5FFF : MARKER (code-section timing)
 *   0x6000 - 0x6FFF : KERNEL (task/thread/ISR/object events)
 *   0x7000 - 0x70FF : State Machine (SM)
 *   0x7100 - 0x7FFF : Reserved
 *   0x8000 - 0xFFFE : DYNAMIC (runtime allocated)
 * 
 * HOW TO ADD NEW TIDs:
 *   1. Find the appropriate section (LOG, VAR, STREAM, ISR)
 *   2. Add your TID name BEFORE the _COUNT entry
 *   3. Value is auto-assigned - NO DUPLICATES POSSIBLE!
 * 
 * Example:
 *   TRAX_TID_LOG_SENSOR,    // Add new log TID here
 *   TRAX_TID_LOG_MOTOR,     // Add another here
 * 
 ******************************************************************************
 */

#ifndef TRAX_TID_H_
#define TRAX_TID_H_

#include <stdint.h>

/*=============================================================================
 ====================SYSTEM TIDs (RESERVED - DO NOT MODIFY)====================
 ============================================================================*/

/**
 * @brief Reserved TID Range (0x0000 - 0x00FF)
 * 
 * System TIDs for protocol, metadata, and control frames.
 * DO NOT use these for user data!
 */
#define TRAX_TID_RANGE_RESERVED_START      0x0000
#define TRAX_TID_RANGE_RESERVED_END        0x00FF

/* Timestamp/Control TIDs (TRAX_TID_TIMESTAMP separate from protocol commands 0x0001-0x00FF) */
#define TRAX_TID_TIMESTAMP           0x0000  /**< Timestamp frame (periodic, global time sync) */
#define TRAX_TID_SESSION_STOP         0x0012  /**< Probe streaming stopped — payload [reason, info]
                                             where reason is enum trax_stop_reason_t and info
                                             carries reason-specific context (e.g. requested
                                             size for HEAP_EXHAUSTED). Payload is
                                             mandatory; host rejects short frames as
                                             protocol errors. */
#define TRAX_TID_SESSION_START        0x0015  /**< Probe streaming started with all metadata */

/**
 * @brief Reason a TRAX_TID_SESSION_STOP frame was emitted.
 *
 * Carried as param[0] of the SESSION_STOP frame so the host can render
 * an autopsy banner ("probe stopped because of buffer overflow…") instead
 * of having to infer the cause from the silence that follows.
 *
 * Values are wire-stable — never renumber. Add new reasons at the end.
 *
 * STOP_REASON_NORMAL is sent on host-requested stop (CMD_SESSION_STOP).
 * Every other value indicates the firmware decided to stop on its own
 * because continuing would produce corrupt frames (FRAME_CORRUPT,
 * TRANSPORT_FAIL, HEAP_EXHAUSTED). Ring overflow is NOT a stop reason:
 * it pauses the stream and resumes with a TRAX_TID_SESSION_GAP resync
 * frame. The matching diag snapshot emitted right after carries the
 * full forensic detail.
 */
enum trax_stop_reason_t {
	TRAX_STOP_REASON_NORMAL          = 0, /**< CMD_SESSION_STOP from host */
	TRAX_STOP_REASON_FRAME_CORRUPT   = 1, /**< Outbound frame failed pre-tx validation */
	TRAX_STOP_REASON_TRANSPORT_FAIL  = 2, /**< Transport hard-error (write returned err) */
	TRAX_STOP_REASON_HEAP_EXHAUSTED  = 3, /**< Heap alloc failure / leak alarm */
	TRAX_STOP_REASON_USER            = 4, /**< Application called trax_session_stop() */
};

/* Protocol Response TIDs */
#define TRAX_TID_ACK                 0x0023  /**< Acknowledge response */
#define TRAX_TID_NAK                 0x0024  /**< Negative acknowledge */

/* VAR Stream lifecycle TIDs (protocol-level, fire-and-forget) */
#define TRAX_TID_VAR_STREAM_START        0x0030  /**< VAR stream started: param[0]=stream_id, param[1]=tick_overflow_cntr, param[2]=timestamp */
#define TRAX_TID_VAR_STREAM_STOP         0x0031  /**< VAR stream stopped: param[0]=stream_id */

/* Multi-probe timestamp synchronization */
#define TRAX_TID_SYNC_TIMESTAMP      0x0016  /**< Timestamp sync frame: param[0]=raw_ts, param[1]=tick_overflow_cntr */
#define TRAX_TID_SESSION_GAP          0x0018  /**< Overflow-gap resume/resync frame.
                                             Emitted by trax_process() once the
                                             ring has fully drained after an overflow gate, immediately before
                                             streaming is re-enabled. Payload = struct trax_gap_header_t
                                             (gap start/end time anchors, current tick_overflow_cntr for host
                                             re-anchoring, dropped-frame count, gap sequence number) followed by
                                             word-padded sections: stream runtime meta, RTOS task wire structs,
                                             RTOS object wire structs — same section structs as SESSION_START.
                                             The host treats the snapshot as STATE REPLACEMENT (task/object
                                             creates/deletes during the gap were dropped), re-anchors its
                                             timebase, and renders the interval as a data gap, not an error. */
#define TRAX_TID_DIAG_REPORT         0x0017  /**< Diagnostic stats snapshot — payload = trax_diag_report_t.
                                            Emitted periodically (TRAX_CFG_DIAG_REPORT_PERIOD_MS,
                                            default 1 s) and on stream-critical events by
                                            trax_diag_send_report().  Host shows live values in
                                            the Probe Configuration panel. */

/* Bare-metal main-loop iteration boundaries (TRAX_MAIN_LOOP_BEGIN/END).
 * Singular by definition (one main loop per core; the core is already in
 * the frame header), so this is a single fixed TID rather than a range.
 * Wire format: param[0] = action — 0=BEGIN, 1=END. The host SliceTracker
 * uses these to bracket "main" lane slices in TraceView for firmware
 * that has no ISR or RTOS instrumentation to supply natural boundaries. */
#define TRAX_TID_MAIN_LOOP                   0x0040  /**< Main-loop iteration boundary: param[0]=action (0=BEGIN, 1=END) */
#define TRAX_MAIN_LOOP_ACTION_BEGIN     0
#define TRAX_MAIN_LOOP_ACTION_END       1

/*=============================================================================
 ====================USER TID RANGES (BASE VALUES)==============================
 ============================================================================*
 *
 * Naming convention:
 *   TRAX_TID_RANGE_<X>_START / _END / _SIZE
 *       Wire-protocol range. Used by library range-check macros
 *       (TRAX_IS_LOG_TID, …) and by the Traxcope host parser. Never
 *       appears in user trax_config.h enums.
 *
 *   TRAX_TID_RANGE_<X>_USER_START
 *       Where user-defined enums begin. Identical to TRAX_TID_RANGE_<X>_START
 *       for every range *except* the two with port-reserved sub-ranges:
 *           - LOG: 0x0100-0x010F reserved for library-internal logs
 *                  (e.g. TRAX_TID_LOG_DIAG for diagnostic event default logs)
 *           - ISR: 0x4000-0x400F reserved for port-internal ISRs
 *                  (e.g. TRAX_TID_ISR_TICK for the RTOS-owned SysTick)
 *       If we ever need to reserve port slots in another range, only
 *       this one macro needs to shift — the user-side enums recompile
 *       at the new base automatically.
 *
 *       Always use TRAX_TID_RANGE_<X>_USER_START in trax_config.h, e.g.:
 *           enum { TRAX_TID_LOG_BOOT = TRAX_TID_RANGE_LOG_USER_START, ... };
 *=============================================================================*/

#define TRAX_TID_RANGE_LOG_START           0x0100
#define TRAX_TID_RANGE_LOG_END             0x1FFF
#define TRAX_TID_RANGE_LOG_SIZE            (TRAX_TID_RANGE_LOG_END - TRAX_TID_RANGE_LOG_START + 1)

/* The bottom of the LOG range is reserved for TraxProbe and its
 * port/diagnostic modules. User application code MUST start its own
 * LOG enums at TRAX_TID_RANGE_LOG_USER_START. The 16-slot reservation
 * mirrors the ISR_PORT pattern below and leaves room for future
 * port- or library-internal LOG TIDs (e.g. self-test result logs,
 * stack-warning logs from the kernel port) without needing another
 * range sweep. */
#define TRAX_TID_RANGE_LOG_PORT_START      TRAX_TID_RANGE_LOG_START
#define TRAX_TID_RANGE_LOG_PORT_END        (TRAX_TID_RANGE_LOG_START + 0x000F)
#define TRAX_TID_RANGE_LOG_USER_START      (TRAX_TID_RANGE_LOG_START + 0x0010)
#define TRAX_TID_RANGE_LOG_USER_END        TRAX_TID_RANGE_LOG_END
#define TRAX_TID_RANGE_LOG_USER_SIZE       (TRAX_TID_RANGE_LOG_USER_END - TRAX_TID_RANGE_LOG_USER_START + 1)

/* Library-reserved LOG TIDs.
 *
 * TRAX_TID_LOG_DIAG_* — default-log channels emitted automatically by
 * trax_diag_report() when TRAX_CFG_DIAG_DEFAULT_LOGS=1 (default).
 * The trax_diag module synthesises ERROR-level lines for
 * data-loss events (BUFFER_OVERFLOW, FRAME_CORRUPT) and WARNING-
 * level lines for alert events (THRESHOLD_CROSSED, BACKPRESSURE).
 * Verbose / periodic events (BUF_PEAK, TRANSPORT_STATS) are NOT
 * default-logged — they would create per-second spam. Subscribe to
 * those explicitly via trax_diag_subscribe() if you want them.
 *
 * One TID per event is mandatory: TRAX_LOG_BASE generates a
 * per-TID static metadata struct (level + format string baked in
 * at compile time), so every distinct call site needs its own TID
 * to give the EventLog a distinct filter row and the right level.
 *
 * The tag string passed to TRAX_LOG_* on these TIDs is "diag" so
 * users can filter the Event View by that tag to isolate library
 * diagnostic traffic from their own application logs. */
/* IMPORTANT: These must be plain integer literals, not computed expressions.
 * TRAX_LOG_BASE uses token-pasting (##) to form a unique static struct name
 * like __trax_log_fmt_0x0100. If the TID expands to a parenthesised
 * expression such as (TRAX_TID_RANGE_LOG_PORT_START + 0x0), the paste produces
 * __trax_log_fmt_(0x0100 + 0x0), which is not a valid preprocessing token
 * and triggers a hard compiler error. Flat literals avoid this entirely. */
#define TRAX_TID_LOG_DIAG_OVERFLOW         0x0100  /* TRAX_TID_RANGE_LOG_PORT_START + 0 */
#define TRAX_TID_LOG_DIAG_FRAME_CORRUPT    0x0101  /* TRAX_TID_RANGE_LOG_PORT_START + 1 */
#define TRAX_TID_LOG_DIAG_THRESHOLD        0x0102  /* TRAX_TID_RANGE_LOG_PORT_START + 2 */
#define TRAX_TID_LOG_DIAG_BACKPRESSURE     0x0103  /* TRAX_TID_RANGE_LOG_PORT_START + 3 */

#define TRAX_TID_RANGE_VAR_START           0x2000
#define TRAX_TID_RANGE_VAR_END             0x2FFF
#define TRAX_TID_RANGE_VAR_SIZE            (TRAX_TID_RANGE_VAR_END - TRAX_TID_RANGE_VAR_START + 1)
#define TRAX_TID_RANGE_VAR_USER_START      TRAX_TID_RANGE_VAR_START
#define TRAX_TID_RANGE_VAR_USER_END        TRAX_TID_RANGE_VAR_END
#define TRAX_TID_RANGE_VAR_USER_SIZE       (TRAX_TID_RANGE_VAR_USER_END - TRAX_TID_RANGE_VAR_USER_START + 1)

#define TRAX_TID_RANGE_STREAM_START    0x3000
#define TRAX_TID_RANGE_STREAM_END      0x3FFF
#define TRAX_TID_RANGE_STREAM_SIZE     (TRAX_TID_RANGE_STREAM_END - TRAX_TID_RANGE_STREAM_START + 1)
#define TRAX_TID_RANGE_STREAM_USER_START  TRAX_TID_RANGE_STREAM_START
#define TRAX_TID_RANGE_STREAM_USER_END    TRAX_TID_RANGE_STREAM_END
#define TRAX_TID_RANGE_STREAM_USER_SIZE   (TRAX_TID_RANGE_STREAM_USER_END - TRAX_TID_RANGE_STREAM_USER_START + 1)

#define TRAX_TID_RANGE_ISR_START           0x4000
#define TRAX_TID_RANGE_ISR_END             0x4FFF
#define TRAX_TID_RANGE_ISR_SIZE            (TRAX_TID_RANGE_ISR_END - TRAX_TID_RANGE_ISR_START + 1)

/* The bottom of the ISR range is reserved for TraxProbe and its RTOS
 * ports. User application code MUST start its own ISR enums at
 * TRAX_TID_RANGE_ISR_USER_START. The 16-slot reservation leaves room for
 * future port-internal ISRs (e.g. timer-tick for new RTOS ports, an
 * SMP cross-core IPI, …) without needing another sweep. */
#define TRAX_TID_RANGE_ISR_PORT_START      TRAX_TID_RANGE_ISR_START
#define TRAX_TID_RANGE_ISR_PORT_END        (TRAX_TID_RANGE_ISR_START + 0x000F)
#define TRAX_TID_RANGE_ISR_USER_START      (TRAX_TID_RANGE_ISR_START + 0x0010)
#define TRAX_TID_RANGE_ISR_USER_END        TRAX_TID_RANGE_ISR_END
#define TRAX_TID_RANGE_ISR_USER_SIZE       (TRAX_TID_RANGE_ISR_USER_END - TRAX_TID_RANGE_ISR_USER_START + 1)

/* Library-reserved ISR TIDs.
 *
 * TRAX_TID_ISR_TICK — the RTOS kernel tick. Under TRAX_RTOS_FREERTOS this is
 * the SysTick exception (or whatever timer the kernel port uses) and is
 * referenced by os/FreeRTOS/trax_rtos_port.h's traceISR_ENTER macro
 * plus the META record in os/FreeRTOS/trax_freertos_meta.c. Defining it
 * here means the user no longer has to add it to their trax_config.h
 * just to satisfy the port — the contract is fully port-managed.
 *
 * Bare-metal users may also use this TID for their own SysTick handler
 * (and supply their own TRAX_ISR_DEFINE for it). It is not
 * special-cased anywhere in the wire protocol — Traxcope treats it
 * exactly like any other ISR TID. */
/* Flat literal required: TRAX_ISR_DEFINE token-pastes this value into
 * a struct name (__trax_isr_meta_##_tid). An expression would not paste. */
#define TRAX_TID_ISR_TICK                  0x4000  /* TRAX_TID_RANGE_ISR_PORT_START + 0 */

#define TRAX_TID_RANGE_MARKER_START        0x5000
#define TRAX_TID_RANGE_MARKER_END          0x5FFF
#define TRAX_TID_RANGE_MARKER_SIZE         (TRAX_TID_RANGE_MARKER_END - TRAX_TID_RANGE_MARKER_START + 1)
#define TRAX_TID_RANGE_MARKER_USER_START   TRAX_TID_RANGE_MARKER_START
#define TRAX_TID_RANGE_MARKER_USER_END     TRAX_TID_RANGE_MARKER_END
#define TRAX_TID_RANGE_MARKER_USER_SIZE    (TRAX_TID_RANGE_MARKER_USER_END - TRAX_TID_RANGE_MARKER_USER_START + 1)

/*=============================================================================
 ====================KERNEL TIDs (0x6000 - 0x6FFF)==============================
 ============================================================================*/

#define TRAX_TID_RANGE_KERNEL_START          0x6000
#define TRAX_TID_RANGE_KERNEL_END            0x6FFF
#define TRAX_TID_RANGE_KERNEL_SIZE           (TRAX_TID_RANGE_KERNEL_END - TRAX_TID_RANGE_KERNEL_START + 1)

/* Task/thread lifecycle */
#define TRAX_TID_TASK_CREATE                 0x6001  /**< params: [handle, priority] + name (raw) */
#define TRAX_TID_TASK_DELETE                 0x6002  /**< params: [handle] */
#define TRAX_TID_TASK_SWITCH_IN              0x6003  /**< params: [handle] */
#define TRAX_TID_TASK_SWITCH_OUT             0x6004  /**< params: [handle] */
#define TRAX_TID_TASK_READY                  0x6005  /**< params: [handle] */
#define TRAX_TID_TASK_SUSPEND                0x6006  /**< params: [handle] */
#define TRAX_TID_TASK_RESUME                 0x6007  /**< params: [handle] */
#define TRAX_TID_TASK_DELAY                  0x6008  /**< params: [handle, ticks] */
#define TRAX_TID_TASK_PRIORITY_SET           0x6009  /**< params: [handle, new_priority] */
/* Priority inheritance — distinct TIDs (NOT a reuse of TRAX_TID_TASK_PRIORITY_SET).
 *
 * INHERIT fires when a higher-priority task tries to take a mutex already held
 * by a lower-priority task: the kernel boosts the holder's effective priority
 * to the blocker's so the holder can finish and release the mutex without an
 * unbounded medium-priority task starving the chain. DISINHERIT fires when the
 * holder finally gives the mutex back and its priority is restored.
 *
 * Distinct TIDs (not a reuse of TRAX_TID_TASK_PRIORITY_SET) so the host can render
 * priority-inheritance events with their own visual treatment — this is the
 * marquee artefact for tut14.3 (mutex priority inversion). The legacy
 * TRAX_TID_TASK_PRIORITY_SET is reserved for explicit vTaskPrioritySet() calls
 * from application code.  Params [handle, new_priority] match the layout of
 * TRAX_TID_TASK_PRIORITY_SET so the host decoder reuses the same parser. */
#define TRAX_TID_TASK_PRIORITY_INHERIT       0x600A  /**< params: [handle, inherited_priority] */
#define TRAX_TID_TASK_PRIORITY_DISINHERIT    0x600B  /**< params: [handle, restored_priority] */
/* Task creation failed — fires from xTaskCreate's else branch when
 * pvPortMalloc(sizeof(TCB_t)) returns NULL.  No handle field (the task
 * never existed and there is nothing to register in the task table);
 * payload [requested_priority, requested_stack_words] gives the host
 * enough context to render a useful card ("task create failed: priority
 * 3, stack 256 words") and lets the user correlate to the matching
 * application-side xTaskCreate call site.  Mirror of the per-class
 * TRAX_TID_*_CREATE_FAILED family in the queue subblock (0x6053-0x6056) for
 * tasks.  Owned by the firmware diagnostics work that closes the
 * FreeRTOS create-failed coverage gap. */
#define TRAX_TID_TASK_CREATE_FAILED          0x600C  /**< params: [requested_priority, requested_stack_words] */
/* Task stack overflow — fires from the user's vApplicationStackOverflowHook
 * (FreeRTOS calls it from the context-switch path when configCHECK_FOR_
 * STACK_OVERFLOW = 1 or 2 detects either a stack pointer past the canary
 * region [method 1] or a stack-fill-pattern corruption [method 2]).  The
 * user wires it in by calling trax_report_stack_overflow(xTask) from
 * their hook implementation; the kernel-supplied pcTaskName is ignored
 * on the wire because the host already has the name in OsRegistry from
 * the matching TRAX_TID_TASK_CREATE event (and falls back to the raw handle
 * if the task wasn't registered before the overflow).  Complements the
 * periodic TRAX_TID_STACK_USAGE polling in trax_freertos_memory.c — that
 * polling captures the trend leading up to the failure, this TID
 * captures the failure itself (the moment the kernel tripped its own
 * canary check), so the two together give "saw it coming, watched it
 * happen" coverage of the most common embedded-RTOS failure mode. */
#define TRAX_TID_TASK_STACK_OVERFLOW         0x600D  /**< params: [task_handle] */

/* ISR runtime events (KERNEL_ prefix avoids collision with TRAX_TID_RANGE_ISR range) */
#define TRAX_TID_KERNEL_ISR_ENTER            0x6010  /**< params: [isr_id] */
#define TRAX_TID_KERNEL_ISR_EXIT             0x6011  /**< params: [isr_id] */

/* Task notifications — FreeRTOS's lightweight per-task signalling primitive.
 * Faster than semaphores (no queue object, no critical-section dance) and
 * the recommended modern primitive for ISR-to-task wake-up patterns; was
 * effectively invisible in the trace until this batch landed (an ISR-driven
 * wake showed up only as a SWITCH_OUT/SWITCH_IN pair with no event
 * explaining why the wake happened).
 *
 * Six TIDs collapse seven FreeRTOS hooks (NOTIFY task ctx and
 * NOTIFY_FROM_ISR share TRAX_TID_TASK_NOTIFY using the same caller-context-via-
 * ISR-bracket trick the queue family uses; the GIVE_FROM_ISR variant gets
 * its own TID because its semantic — "atomic increment, like a binary/
 * counting semaphore give from ISR" — is the marquee use case for tut21).
 *
 *   TRAX_TID_TASK_NOTIFY              params: [target_tcb, sender_tcb_or_0]
 *     Generic notify (xTaskNotify / xTaskNotifyIndexed and the FromISR
 *     variants of the same).  sender_tcb is pxCurrentTCB in task ctx and
 *     0 in ISR ctx (the surrounding ISR_ENTER / ISR_EXIT brackets remain
 *     the authoritative caller-context source; the explicit 0 is just a
 *     hint for the renderer).  Lands on the TARGET task's lane so the
 *     reader sees "this task got woken".
 *   TRAX_TID_TASK_NOTIFY_GIVE         params: [target_tcb, 0]
 *     vTaskNotifyGiveFromISR — the binary/counting-semaphore-replacement
 *     fast path.  ISR-only; sender is always the IRQ in scope (visible
 *     from the ISR_ENTER bracket).
 *   TRAX_TID_TASK_NOTIFY_TAKE         params: [self_tcb, index]
 *   TRAX_TID_TASK_NOTIFY_TAKE_BLOCK   params: [self_tcb, index]
 *     ulTaskNotifyTake / its about-to-block peer.  TAKE fires when the
 *     receiver actually consumes the value (post-wake); TAKE_BLOCK fires
 *     the instant the receiver discovers there's nothing to take and is
 *     about to be parked.  Mirrors the QUEUE_RECEIVE / RECV_BLOCK pair.
 *   TRAX_TID_TASK_NOTIFY_WAIT         params: [self_tcb, index]
 *   TRAX_TID_TASK_NOTIFY_WAIT_BLOCK   params: [self_tcb, index]
 *     ulTaskNotifyWait — the bit-flag variant of TAKE.  Distinct TID
 *     from TAKE because the consume semantic differs (TAKE clears the
 *     32-bit value; WAIT clears only the bits the caller asked about),
 *     and tut21 will need to render the two differently to teach when
 *     to pick which.
 *
 * Index is preserved on the receiver side (configTASK_NOTIFICATION_ARRAY_
 * ENTRIES > 1 is rare but legal); on the sender side index is sacrificed
 * in favor of sender_tcb (more useful for routing — without it you can't
 * tell who triggered the wake).  If a future use case needs index on the
 * sender side too, the framing supports a 3rd uint32_t param.
 *
 * Owned by tut21 (task-notify-as-lightweight-semaphore); retroactively
 * useful in any tutorial that uses xTaskNotifyGive / ulTaskNotifyTake. */
#define TRAX_TID_TASK_NOTIFY                 0x6012  /**< params: [target_tcb, sender_tcb_or_0] */
#define TRAX_TID_TASK_NOTIFY_GIVE            0x6013  /**< params: [target_tcb, 0] (ISR ctx only) */
#define TRAX_TID_TASK_NOTIFY_TAKE            0x6014  /**< params: [self_tcb, index] */
#define TRAX_TID_TASK_NOTIFY_TAKE_BLOCK      0x6015  /**< params: [self_tcb, index] */
#define TRAX_TID_TASK_NOTIFY_WAIT            0x6016  /**< params: [self_tcb, index] */
#define TRAX_TID_TASK_NOTIFY_WAIT_BLOCK      0x6017  /**< params: [self_tcb, index] */
/* 0x6018-0x601F reserved for future task-side hooks. */

/* Queue operations */
#define TRAX_TID_QUEUE_SEND                  0x6020  /**< params: [queue_handle, items_waiting] */
#define TRAX_TID_QUEUE_RECEIVE               0x6021  /**< params: [queue_handle, items_waiting] */
#define TRAX_TID_QUEUE_SEND_FAILED           0x6022  /**< params: [queue_handle] */
#define TRAX_TID_QUEUE_RECV_FAILED           0x6023  /**< params: [queue_handle] */
/* "About to block" events — fire from FreeRTOS's
 * traceBLOCKING_ON_QUEUE_SEND / traceBLOCKING_ON_QUEUE_RECEIVE the
 * instant a task discovers the queue is full / empty and is about to
 * be parked on the queue's wait list.  Distinct from the *_FAILED
 * variants above (those fire when a NON-blocking op or one that
 * timed out without waiting returns pdFAIL — the task is still
 * RUNNING).  These two events fire when the task is about to leave
 * RUNNING for BLOCKED, with the matching SWITCH_OUT immediately
 * following.  The wait is resolved by either a SUCCESS event on the
 * same queue (data eventually became available / space freed up) or
 * a *_FAILED event (the wait timed out).
 *
 * Carries [queue_handle, task_handle] so the host can route the
 * event to both the queue's lane (the wait-list it's joining) and
 * the task's lane (the explanation for the upcoming SWITCH_OUT).
 * pxCurrentTCB is in scope from queue.c so the second param is free.
 *
 * Owned by tut15.4 (queue starvation diagnosis); retroactively
 * useful in every tut14/tut15 capture (the SWITCH_OUT explanation). */
#define TRAX_TID_QUEUE_SEND_BLOCK            0x6024  /**< params: [queue_handle, task_handle] */
#define TRAX_TID_QUEUE_RECV_BLOCK            0x6025  /**< params: [queue_handle, task_handle] */
/* Queue PEEK family — fires from xQueuePeek() / xQueuePeekFromISR() and
 * the matching traceBLOCKING_ON_QUEUE_PEEK in queue.c.  PEEK is a
 * non-destructive read (the item stays on the queue, depth unchanged
 * after the op), distinct from RECEIVE which removes the item.
 *
 * Five FreeRTOS hooks collapse to three TIDs using the same caller-
 * context-via-ISR-bracket trick the SEND/RECEIVE family uses:
 *   traceQUEUE_PEEK              \
 *   traceQUEUE_PEEK_FROM_ISR     /  → TRAX_TID_QUEUE_PEEK   (success)
 *   traceQUEUE_PEEK_FAILED       \
 *   traceQUEUE_PEEK_FROM_ISR_FAIL/  → TRAX_TID_QUEUE_PEEK_FAILED
 *   traceBLOCKING_ON_QUEUE_PEEK     → TRAX_TID_QUEUE_PEEK_BLOCK
 *
 * items_waiting on a successful PEEK is the depth AT peek time
 * (PRE-op == POST-op since peek is non-destructive — this is the
 * one queue op where pre/post-op semantics happen to coincide).
 * The block event carries [queue_handle, task_handle] like the
 * SEND/RECV block peers.  Polymorphic on object type host-side:
 * a peek on a mutex never happens (xSemaphoreTake hits the RECEIVE
 * path), so PEEK is queue-only in practice — but the rendering
 * layer still routes through objectByHandle->objType to be defensive.
 *
 * Owned by tut15.6 (queue sets, message buffers, peek-vs-receive
 * comparison).  Closes the entire FreeRTOS queue trace-hook surface. */
#define TRAX_TID_QUEUE_PEEK                  0x6026  /**< params: [queue_handle, items_waiting] */
#define TRAX_TID_QUEUE_PEEK_FAILED           0x6027  /**< params: [queue_handle] */
#define TRAX_TID_QUEUE_PEEK_BLOCK            0x6028  /**< params: [queue_handle, task_handle] */

/* Stream / message buffer family — FreeRTOS's byte-stream / discrete-message
 * primitive (sbTYPE_STREAM_BUFFER = 0, sbTYPE_MESSAGE_BUFFER = 1,
 * sbTYPE_STREAM_BATCHING_BUFFER = 2 in stream_buffer.h).  Implemented in
 * stream_buffer.c, NOT through queue.c, so they need their own TID family
 * (cannot piggy-back on TRAX_TID_QUEUE_*).  Slot 0x6029-0x602F was previously
 * reserved by the peek family for "future queue-set hooks"; we re-purpose
 * it for stream buffers because (a) the seven slots are an exact fit and
 * (b) queue sets — if they ever need their own TIDs — will more naturally
 * sit in 0x6043-0x604F (between the mutex-take-failed and obj-create
 * blocks where there's still empty space).
 *
 * Seven TIDs cover the full surface that's actually called from
 * stream_buffer.c (lifecycle reuses OBJ_CREATE/OBJ_NAME — see
 * SOFTWARE TIMER COVERAGE for the same pattern):
 *
 *   TRAX_TID_STREAM_BUFFER_SEND          params: [sb_handle, bytes_sent]
 *     Fires from xStreamBufferSend() when xReturn > 0 and from
 *     xStreamBufferSendFromISR() when its xReturn > 0 (firmware-side
 *     split — see traceSTREAM_BUFFER_SEND_FROM_ISR in trax_rtos_port.h).
 *     bytes_sent is the actual number of bytes accepted by the buffer
 *     (may be < requested if the buffer didn't have enough free space
 *     for the full request and the call wasn't allowed to block).
 *   TRAX_TID_STREAM_BUFFER_SEND_FAILED   params: [sb_handle]
 *     Fires from xStreamBufferSend() when xReturn == 0 (full timeout
 *     elapsed without space) and from xStreamBufferSendFromISR() when
 *     its xReturn == 0 (no space at all in ISR ctx — no blocking
 *     semantics from ISR, fire-and-forget failure).
 *   TRAX_TID_STREAM_BUFFER_SEND_BLOCK    params: [sb_handle, task_handle]
 *     Fires from traceBLOCKING_ON_STREAM_BUFFER_SEND just before the
 *     calling task is parked on the buffer's wait-list (xWaitingToSend).
 *     Only meaningful in task ctx — ISR variants don't block.
 *     pxCurrentTCB resolves the about-to-block task at hook time, same
 *     trick as TRAX_TID_QUEUE_SEND_BLOCK.
 *   TRAX_TID_STREAM_BUFFER_RECV          params: [sb_handle, bytes_received]
 *     Mirror of SEND on the receive side — fires from xStreamBufferReceive
 *     and xStreamBufferReceiveFromISR when their xReturn > 0.  For
 *     message buffers the bytes_received value is the message length
 *     (the leading 4-byte length-prefix is stripped by FreeRTOS — the
 *     host-renderer just shows "bytes" since the read API is bytes-based
 *     even for message buffers).
 *   TRAX_TID_STREAM_BUFFER_RECV_FAILED   params: [sb_handle]
 *     Mirror of SEND_FAILED on the receive side.  Includes "trigger
 *     level not reached within timeout" (the SB-specific failure mode
 *     that has no queue analogue).
 *   TRAX_TID_STREAM_BUFFER_RECV_BLOCK    params: [sb_handle, task_handle]
 *     Mirror of SEND_BLOCK on the receive side.  Fires from
 *     traceBLOCKING_ON_STREAM_BUFFER_RECEIVE.
 *   TRAX_TID_STREAM_BUFFER_RESET         params: [sb_handle]
 *     Fires from xStreamBufferReset() (task ctx) and
 *     xStreamBufferResetFromISR() (ISR ctx).  Single TID — caller ctx
 *     reconstructed from surrounding ISR brackets, same trick as the
 *     queue family.  No before/after state distinction (reset always
 *     succeeds when called — it returns pdFAIL only if a task is
 *     blocked on the buffer, in which case the caller is supposed to
 *     handle it; the trace just records "reset attempted").
 *
 * traceSTREAM_BUFFER_CREATE / traceSTREAM_BUFFER_DELETE are routed
 * through the existing OBJ_CREATE / OBJ_DELETE infrastructure (CREATE
 * uses obj_type = 7 / 8 / 9 for stream / message / batching buffer;
 * DELETE is intentionally NOT yet implemented because StreamBuffer_t
 * has no uxQueueNumber-equivalent field to stash the table_index in
 * — a future "scan-by-handle" helper would unlock both this and
 * traceTIMER_DELETE in one stroke).
 *
 * traceSTREAM_BUFFER_CREATE_FAILED and traceSTREAM_BUFFER_CREATE_STATIC_FAILED
 * are intentionally NOT implemented because (a) they fire only on
 * malloc failure, (b) the buffer pointer doesn't exist (so the event
 * has no handle to attach to and would orphan in osStore), and (c) the
 * application already sees the NULL return value and can log it via
 * its own LOG mechanism.  If a future tutorial needs to instrument
 * malloc-failure shape on stream buffers, allocate from 0x6083+ and
 * add a handle=0 special-case in the host renderer.
 *
 * Owned by tut15.6 (queue sets, message buffers, peek-vs-receive
 * comparison).  The marquee tut15.6 measurement is the SEND→RECV
 * cursor delta on a message buffer — the byte-stream-vs-message-record
 * distinction shows up as the host renderer reading bytes_sent /
 * bytes_received as either "stream-byte count" or "message length"
 * depending on the obj_type recorded at CREATE time. */
#define TRAX_TID_STREAM_BUFFER_SEND          0x6029  /**< params: [sb_handle, bytes_sent] */
#define TRAX_TID_STREAM_BUFFER_SEND_FAILED   0x602A  /**< params: [sb_handle] */
#define TRAX_TID_STREAM_BUFFER_SEND_BLOCK    0x602B  /**< params: [sb_handle, task_handle] */
#define TRAX_TID_STREAM_BUFFER_RECV          0x602C  /**< params: [sb_handle, bytes_received] */
#define TRAX_TID_STREAM_BUFFER_RECV_FAILED   0x602D  /**< params: [sb_handle] */
#define TRAX_TID_STREAM_BUFFER_RECV_BLOCK    0x602E  /**< params: [sb_handle, task_handle] */
#define TRAX_TID_STREAM_BUFFER_RESET         0x602F  /**< params: [sb_handle] */

/* Semaphore operations */
#define TRAX_TID_SEM_GIVE                    0x6030  /**< params: [sem_handle, count] */
#define TRAX_TID_SEM_TAKE                    0x6031  /**< params: [sem_handle, count] */
#define TRAX_TID_SEM_TAKE_FAILED             0x6032  /**< params: [sem_handle] */

/* Mutex operations */
#define TRAX_TID_MUTEX_GIVE                  0x6040  /**< params: [mutex_handle] */
#define TRAX_TID_MUTEX_TAKE                  0x6041  /**< params: [mutex_handle] */
#define TRAX_TID_MUTEX_TAKE_FAILED           0x6042  /**< params: [mutex_handle] */

/* Recursive mutex operations — config-gated by configUSE_RECURSIVE_MUTEXES.
 * Distinct TID block (vs reusing TRAX_TID_MUTEX_TAKE / TRAX_TID_MUTEX_GIVE) because:
 *
 *   1. Recursive mutexes have nesting-count semantics — a single take/give
 *      pair is one of N pairs at different recursion levels.  The host
 *      needs to render "MTX_TAKE_REC level=2" not just "MTX_TAKE", and
 *      the mutex hold-time enrichment in Probe.cpp must differentiate
 *      outermost-take (count_before==0, start timing) from inner-takes
 *      (count_before≥1, just bump count).  The existing
 *      Probe.cpp:998-999 comment explicitly documents the broken-for-
 *      recursive-mutexes hold-time as a known limitation that this
 *      split fixes.
 *
 *   2. FreeRTOS V11 has a dedicated GIVE_RECURSIVE_FAILED hook (caller
 *      isn't the holder) that has no non-recursive equivalent — there
 *      is no traceMUTEX_GIVE_FAILED.  Allocating the recursive give-
 *      failed event a TID of its own keeps the four-arm TAKE/GIVE/
 *      *_FAILED symmetry the host UI is structured around.
 *
 * Wire payload conventions:
 *   TRAX_TID_MUTEX_TAKE_RECURSIVE         params: [handle, count_before_op]
 *     count_before_op == 0   → outermost take attempt (will go through
 *                              xQueueSemaphoreTake — may block / fail)
 *     count_before_op >= 1   → recursive take (caller already owns it,
 *                              just bumps the counter)
 *   TRAX_TID_MUTEX_GIVE_RECURSIVE         params: [handle, count_before_op]
 *     count_before_op == 1   → outermost give (about to fully release —
 *                              this is the one that should bracket
 *                              the hold-time measurement)
 *     count_before_op >= 2   → inner give (just decrements counter)
 *   TRAX_TID_MUTEX_TAKE_RECURSIVE_FAILED  params: [handle]
 *     Underlying xQueueSemaphoreTake timed out.  No count change
 *     happened, and the count we'd report would be some other task's
 *     hold depth — so we omit it.
 *   TRAX_TID_MUTEX_GIVE_RECURSIVE_FAILED  params: [handle]
 *     Caller is not the mutex holder.  No count change, same omission
 *     reasoning as TAKE_RECURSIVE_FAILED.
 *
 * Wire truth: count_before_op is what the macro literally observes at
 * hook expansion time (kernel hasn't mutated it yet).  Host renderer
 * adds/subtracts 1 to compute the post-op level for display, which
 * keeps the wire format trivially testable from the kernel side
 * (count_before is just a register read; deriving count_after on the
 * firmware would need extra arithmetic in the macro). */
#define TRAX_TID_MUTEX_TAKE_RECURSIVE        0x6044  /**< params: [mutex_handle, count_before_op] */
#define TRAX_TID_MUTEX_GIVE_RECURSIVE        0x6045  /**< params: [mutex_handle, count_before_op] */
#define TRAX_TID_MUTEX_TAKE_RECURSIVE_FAILED 0x6046  /**< params: [mutex_handle] */
#define TRAX_TID_MUTEX_GIVE_RECURSIVE_FAILED 0x6047  /**< params: [mutex_handle] */

/* Queue sets — FreeRTOS's primitive for blocking on multiple queues /
 * semaphores at once.  Lifecycle (CREATE / DELETE / REGISTRY) reuses
 * the existing TRAX_TID_OBJ_CREATE / TRAX_TID_OBJ_DELETE / TRAX_TID_OBJ_NAME family
 * because xQueueCreateSet calls xQueueGenericCreate with ucQueueType =
 * queueQUEUE_TYPE_SET (=5), so the set registers in osStore as objType
 * = 5 ("queue set") for free with no firmware-side changes.  The
 * receive side (xQueueSelectFromSet) is an xQueueReceive on the set's
 * internal queue and therefore already fires TRAX_TID_QUEUE_RECEIVE — also
 * for free.  Membership operations (xQueueAddToSet / xQueueRemoveFrom
 * Set) have no upstream FreeRTOS V11 trace hook, so they're not
 * traceable without patching the kernel — we accept that gap.
 *
 * The ONE queue-set-specific hook in V11 is traceQUEUE_SET_SEND, which
 * fires from prvNotifyQueueSetContainer in queue.c the instant a send
 * to a member queue/sem causes the set to be notified (the member's
 * handle is pushed into the set's internal queue, and any task
 * blocked on xQueueSelectFromSet gets unblocked).  Without this hook
 * a trace shows "data went into queue M" followed by "task T got
 * woken on set S" with no event explaining the chain — the host
 * cannot otherwise correlate sender→set membership.
 *
 * Wire payload [set_handle, member_handle] gives the host a complete
 * (set, member) pair so the renderer can show "set <S> notified by
 * send to <M>" without a registry roundtrip.  Lands on the set's lane
 * for visual correlation with the upcoming SWITCH_IN of whoever was
 * blocked on xQueueSelectFromSet.  Single TRAX_FRAME_ARGS_ATOMIC,
 * fires only on the cold "member is in a set" path so hot-path
 * overhead is zero for the (common) case of standalone queues.
 *
 * Owned by tut15.6 (queue sets, message buffers, peek-vs-receive). */
#define TRAX_TID_QUEUE_SET_SEND              0x6043  /**< params: [set_handle, member_handle] */
/* 0x6044-0x6047 — recursive mutex hooks (see TRAX_TID_MUTEX_*_RECURSIVE
 * block above).  Sandwiched into the mutex 0x6040-0x6047 area to keep
 * all mutex-family TIDs contiguous.
 *
 * 0x6048-0x604E — event groups (see TRAX_TID_EVT_GROUP_* block below).
 * Filled the previously-free area immediately after the mutex/queue-set
 * region to keep the entire kernel-object TID range contiguous through
 * 0x604E.  The host's isKernelObjectEvent() bound moves accordingly. */

/* Event groups (FreeRTOS event flags / bitmask synchronization) — config-
 * gated by configUSE_EVENT_GROUPS (defaults to 1 in V11 if undefined).
 *
 * Lifecycle (CREATE / DELETE / NAME) reuses existing infrastructure:
 *
 *   traceEVENT_GROUP_CREATE     → trax_rtos_object_register with
 *     objType = 10 (event_group), length = 0 (no capacity concept),
 *     item_size = 0.  Lands on the host's osStore as a normal kernel
 *     object and inherits name resolution / filtering / OBJ_NAME
 *     decoder paths "for free" — same pattern timers / stream buffers
 *     followed.
 *
 *   traceEVENT_GROUP_DELETE     → trax_rtos_object_delete_by_handle
 *     (P1 helper).  Emits TRAX_TID_OBJ_DELETE.  No new TID needed.
 *
 *   traceEVENT_GROUP_CREATE_FAILED → TRAX_TID_EVT_GROUP_CREATE_FAILED below.
 *     Per-class create-failed allocation, parallel to the four queue/
 *     sem/mutex create-failed TIDs (0x6053-0x6056).
 *
 * The seven operational TIDs cover the 11 V11 event-group hooks after
 * collapsing the queue-family-style ISR/non-ISR pairs:
 *
 *   SET_BITS              \  one TID — caller-context (task vs ISR) is
 *   SET_BITS_FROM_ISR     /  reconstructed from the surrounding ISR
 *                            brackets, same trick the queue/sem/SB
 *                            families use.  Halves the kernel-object
 *                            space cost for what's purely a hot-path
 *                            ergonomics distinction.
 *
 *   CLEAR_BITS            \  same pattern as SET_BITS — single TID
 *   CLEAR_BITS_FROM_ISR   /  with ISR-context reconstruction.
 *
 * The remaining hooks have no ISR variant and get one TID each:
 *
 *   WAIT_BITS_BLOCK       — caller about to block on bits not yet set
 *   WAIT_BITS_END         — wait completed (success OR timeout — flag
 *                           on the wire; host splits into _END /
 *                           _Timeout EventTypes for clean rendering)
 *   SYNC_BLOCK            — caller about to block on rendezvous not
 *                           yet met
 *   SYNC_END              — sync completed (success OR timeout — same
 *                           flag-on-wire pattern as WAIT_BITS_END)
 *
 * Wire payload conventions:
 *   TRAX_TID_EVT_GROUP_CREATE_FAILED   params: [] (size 1)
 *   TRAX_TID_EVT_GROUP_SET_BITS        params: [eg_handle, bits_to_set]
 *   TRAX_TID_EVT_GROUP_CLEAR_BITS      params: [eg_handle, bits_to_clear]
 *   TRAX_TID_EVT_GROUP_WAIT_BITS_BLOCK params: [eg_handle, bits_to_wait_for]
 *   TRAX_TID_EVT_GROUP_WAIT_BITS_END   params: [eg_handle, bits_to_wait_for,
 *                                          timeout_flag]
 *     timeout_flag == 0 → success path (bits matched, host renders
 *                         EvtGroupWaitBitsEnd)
 *     timeout_flag != 0 → timeout (host renders EvtGroupWaitBitsTimeout)
 *   TRAX_TID_EVT_GROUP_SYNC_BLOCK      params: [eg_handle, bits_to_set,
 *                                          bits_to_wait_for]
 *   TRAX_TID_EVT_GROUP_SYNC_END        params: [eg_handle, bits_to_set,
 *                                          bits_to_wait_for, timeout_flag]
 *
 * Bits are EventBits_t (typically 32-bit unsigned, 24 bits significant on
 * default config; 16-bit shrunk further when configUSE_16_BIT_TICKS=1 —
 * we widen to uint32_t on the wire because TRAX_FRAME_ARGS_ATOMIC's
 * argument-pack is uint32_t-aligned anyway and the high bits are zero
 * for free.  This keeps the wire format identical regardless of the
 * EventBits_t width on the firmware build, simplifying the decoder.
 *
 * 0x604F free for future event-group expansion (e.g. if the kernel ever
 * adds a SYNC_BITS_NOTIFICATION or similar).  Beyond that the kernel-
 * object range continues into the create-failed block at 0x6053+. */
#define TRAX_TID_EVT_GROUP_CREATE_FAILED     0x6048  /**< params: [] (size 1) */
#define TRAX_TID_EVT_GROUP_SET_BITS          0x6049  /**< params: [eg_handle, bits_to_set] (collapses _FROM_ISR) */
#define TRAX_TID_EVT_GROUP_CLEAR_BITS        0x604A  /**< params: [eg_handle, bits_to_clear] (collapses _FROM_ISR) */
#define TRAX_TID_EVT_GROUP_WAIT_BITS_BLOCK   0x604B  /**< params: [eg_handle, bits_to_wait_for] */
#define TRAX_TID_EVT_GROUP_WAIT_BITS_END     0x604C  /**< params: [eg_handle, bits_to_wait_for, timeout_flag] */
#define TRAX_TID_EVT_GROUP_SYNC_BLOCK        0x604D  /**< params: [eg_handle, bits_to_set, bits_to_wait_for] */
#define TRAX_TID_EVT_GROUP_SYNC_END          0x604E  /**< params: [eg_handle, bits_to_set, bits_to_wait_for, timeout_flag] */

/* Object lifecycle (queues, semaphores, mutexes) */
#define TRAX_TID_OBJ_CREATE                  0x6050  /**< params: [handle, obj_type, length, item_size] + name */
#define TRAX_TID_OBJ_DELETE                  0x6051  /**< params: [handle] */
#define TRAX_TID_OBJ_NAME                    0x6052  /**< params: [handle] + name (from registry add) */

/* Per-class "create failed" TIDs, mirroring the per-class create-success
 * TIDs (TRAX_TID_OBJ_CREATE with obj_type byte) and matching upstream
 * FreeRTOS's deliberate split of the failure hooks into three distinct
 * macros (traceQUEUE_CREATE_FAILED, traceCREATE_MUTEX_FAILED,
 * traceCREATE_COUNTING_SEMAPHORE_FAILED).  All four fire when the
 * underlying pvPortMalloc returns NULL (heap exhausted / fragmentation /
 * static-build assertion), the object never existed, no handle is
 * carried — the event is purely a timeline / Event View entry like
 * TRAX_TID_HEAP_ALLOC_FAILED, with no osStore side-effect on the host.
 *
 * Each TID identifies the kernel object class via its hex value (no
 * obj_type discriminator needed in the payload), so a raw frame dump
 * filterable by TID alone tells you exactly what failed.  Payloads
 * carry only the class-specific "what was requested" field that
 * survives the failure path; mutex / binary semaphore have nothing
 * useful (size is always 1) and therefore go out as zero-arg frames.
 *
 * Note: traceQUEUE_CREATE_FAILED's hook fires from xQueueGenericCreate
 * for BOTH queues and binary semaphores (queueQUEUE_TYPE_BASE and
 * queueQUEUE_TYPE_BINARY_SEMAPHORE flow through the same code path);
 * the firmware-side macro branches on ucQueueType to dispatch to
 * TRAX_TID_QUEUE_CREATE_FAILED vs TRAX_TID_BIN_SEM_CREATE_FAILED on the cold
 * malloc-fail path, so the wire stays one-TID-per-class.  Owned by the
 * firmware diagnostics work that closes the FreeRTOS create-failed
 * coverage gap. */
#define TRAX_TID_QUEUE_CREATE_FAILED         0x6053  /**< params: [requested_length] */
#define TRAX_TID_BIN_SEM_CREATE_FAILED       0x6054  /**< params: [] (size always 1) */
#define TRAX_TID_MUTEX_CREATE_FAILED         0x6055  /**< params: [] (size always 1; recursive vs normal not distinguishable from hook) */
#define TRAX_TID_COUNTING_SEM_CREATE_FAILED  0x6056  /**< params: [requested_max_count] */

/* Heap operations */
#define TRAX_TID_HEAP_ALLOC                  0x6060  /**< params: [address, size, heap_remaining] */
#define TRAX_TID_HEAP_FREE                   0x6061  /**< params: [address, size, heap_remaining] */
#define TRAX_TID_HEAP_ALLOC_FAILED           0x6062  /**< params: [requested_size] */

/* Stack monitoring */
#define TRAX_TID_STACK_USAGE                 0x6070  /**< params: [task_handle, unused_words] */

/* Tickless idle / low-power sleep — config-gated by configUSE_TICKLESS_IDLE.
 *
 * When configUSE_TICKLESS_IDLE != 0 the FreeRTOS idle task hands control to
 * portSUPPRESS_TICKS_AND_SLEEP (a port-supplied function that puts the MCU
 * into a deep-sleep / stop / standby mode and stops the SysTick) instead of
 * just spinning a WFI.  The port wraps that call with three trace hooks the
 * kernel exposes:
 *
 *   traceLOW_POWER_IDLE_BEGIN     — fires immediately before the port's
 *     deep-sleep entry.  After this point the CPU may be powered down; no
 *     other trace events can fire from this core until LOW_POWER_IDLE_END.
 *
 *   traceLOW_POWER_IDLE_END       — fires immediately after the port returns
 *     from deep sleep.  Bracketed with BEGIN this defines the actual sleep
 *     window — the host enriches the END event with (END - BEGIN) hold-time
 *     so the user sees "Slept for N µs" at a glance (same enrichment shape
 *     mutex hold-time uses).
 *
 *   traceINCREASE_TICK_COUNT      — fires from vTaskStepTick after wake to
 *     announce how many SysTick periods were skipped while the MCU was
 *     asleep (the kernel fast-forwards its tick counter by xTicksToJump
 *     ticks instead of taking that many real SysTick interrupts).  Lets the
 *     host correlate "deep sleep duration" against "kernel tick advance"
 *     and detect timing skew (e.g. wake source fired later than expected).
 *
 * Why this TID block (0x6071-0x6073, just past TRAX_TID_STACK_USAGE):
 *   These are SYSTEM events — they have no per-task / per-object handle and
 *   describe whole-kernel state, exactly like TRAX_TID_STACK_USAGE (which is
 *   per-task but reports a system-health metric).  Sitting them in the
 *   0x6070-0x607F system-health area keeps semantic neighbours together.
 *   Allocating a separate block (vs co-locating with task TIDs at 0x6018+)
 *   avoids dragging them into isKernelTaskEvent() — the host should NOT
 *   route LOW_POWER_IDLE_* through the per-task lane writer because no
 *   handle is on the wire.
 *
 * Wire payload conventions:
 *   TRAX_TID_LOW_POWER_IDLE_BEGIN      params: [] (size = 1; timestamp IS the
 *                                  payload — that's what brackets the
 *                                  sleep window for the host enrichment).
 *   TRAX_TID_LOW_POWER_IDLE_END        params: [] (size = 1)
 *   TRAX_TID_INCREASE_TICK_COUNT       params: [ticks_jumped] (uint32_t — the
 *                                  TickType_t xTicksToJump argument from
 *                                  vTaskStepTick).  We deliberately do NOT
 *                                  include the post-jump tick count: the
 *                                  host already maintains its own kernel
 *                                  clock and the jump count is the only
 *                                  diagnostic value here.
 *
 * 0x6074-0x607F free for future system-health TIDs (e.g. CPU load samples,
 * heap fragmentation snapshots) — keep this block reserved for whole-kernel
 * metrics, not per-object events. */
#define TRAX_TID_LOW_POWER_IDLE_BEGIN        0x6071  /**< params: [] (size 1) */
#define TRAX_TID_LOW_POWER_IDLE_END          0x6072  /**< params: [] (size 1) */
#define TRAX_TID_INCREASE_TICK_COUNT         0x6073  /**< params: [ticks_jumped] */

/* Software timers — FreeRTOS's timer-service-task-driven periodic / one-shot
 * callback primitive.  Timer creation reuses the existing object-lifecycle
 * infrastructure (TRAX_TID_OBJ_CREATE / TRAX_TID_OBJ_NAME) with obj_type = 6 (timer),
 * length = xTimerPeriodInTicks, item_size = autoreload_flag (0 / 1) — same
 * pattern queues / semaphores / mutexes already follow, so timers appear in
 * the host osStore as a normal RTOS object and inherit name resolution,
 * filtering, and the existing OBJ_CREATE / OBJ_NAME decoder paths "for
 * free" (no new EventType, no decoder case, no osStore registration logic).
 *
 * Three dedicated TIDs cover the operational events that have no queue /
 * sem / mutex equivalent:
 *
 *   TRAX_TID_TIMER_COMMAND_SEND      params: [timer_handle, command_id]
 *     Fires from xTimerGenericCommandFromTask / FromISR right after
 *     xQueueSendToBack pushes a DaemonTaskMessage_t onto xTimerQueue.
 *     command_id is the tmrCOMMAND_* enum value (1=START, 2=RESET,
 *     3=STOP, 4=CHANGE_PERIOD, 5=DELETE, 6=START_FROM_ISR, ...,
 *     -1=EXECUTE_CALLBACK, -2=EXECUTE_CALLBACK_FROM_ISR) — the host
 *     decodes the command name for the card body.  Lands on the
 *     timer's lane (handle = timer_handle).  pxCurrentTCB at this
 *     point is the SENDER (caller of xTimerStart / xTimerStop / etc.
 *     in task ctx; the preempted task in ISR ctx — the surrounding
 *     ISR brackets are authoritative).
 *   TRAX_TID_TIMER_COMMAND_RECEIVED  params: [timer_handle, command_id]
 *     Fires from prvProcessReceivedCommands inside the timer service
 *     task ("Tmr Svc") after it pops the command off xTimerQueue.
 *     Same payload as SEND so the host can pair them by (handle,
 *     command_id) and surface the SEND→RECEIVED gap as the marquee
 *     tut16.2 measurement (latency through the timer queue, which
 *     bounds how responsive timer commands can be on a busy system).
 *   TRAX_TID_TIMER_EXPIRED           params: [timer_handle]
 *     Fires the instant before pxCallbackFunction(timer) is invoked
 *     from the timer service task.  Headline event for tut16:
 *     measures actual vs. requested fire interval (drift) for
 *     auto-reload timers and confirms one-shot timers fire exactly
 *     once.
 *
 * traceTIMER_CREATE_FAILED is defined as a fallback no-op in FreeRTOS.h
 * but is never actually called from timers.c in this version (no malloc-
 * fail trace site exists in xTimerCreate); intentionally not implemented
 * to avoid the dead-hook code-cost.  If a future FreeRTOS version starts
 * emitting it, add a TID in 0x6083+ and a stub macro.
 *
 * Owned by tut16 (software timers); the COMMAND_SEND→COMMAND_RECEIVED
 * gap is tut16.2's headline measurement. */
#define TRAX_TID_TIMER_COMMAND_SEND          0x6080  /**< params: [timer_handle, command_id] */
#define TRAX_TID_TIMER_COMMAND_RECEIVED      0x6081  /**< params: [timer_handle, command_id] */
#define TRAX_TID_TIMER_EXPIRED               0x6082  /**< params: [timer_handle] */
/* 0x6083-0x608F reserved for future timer hooks (e.g. CREATE_FAILED if
 * FreeRTOS ever adds the trace site, or a TIMER_DELETE if tut16 needs
 * explicit deletion tracking beyond OBJ_DELETE). */

/*=============================================================================
 ====================STATE MACHINE TIDs (0x7000 - 0x70FF)=====================
 ============================================================================*/

#define TRAX_TID_RANGE_SM_START              0x7000
#define TRAX_TID_RANGE_SM_END                0x70FF
#define TRAX_TID_RANGE_SM_SIZE               (TRAX_TID_RANGE_SM_END - TRAX_TID_RANGE_SM_START + 1)
#define TRAX_TID_RANGE_SM_USER_START         TRAX_TID_RANGE_SM_START
#define TRAX_TID_RANGE_SM_USER_END           TRAX_TID_RANGE_SM_END
#define TRAX_TID_RANGE_SM_USER_SIZE          (TRAX_TID_RANGE_SM_USER_END - TRAX_TID_RANGE_SM_USER_START + 1)

/*=============================================================================
 ====================FAULT / POST-MORTEM TIDs (0x7100 - 0x71FF)================
 ============================================================================*/

/* TraxFault: crash / reset post-mortem events (see inc/trax_fault.h).
 * Emitted by the library itself — no user TIDs in this range. */
#define TRAX_TID_RANGE_FAULT_START           0x7100
#define TRAX_TID_RANGE_FAULT_END             0x71FF
#define TRAX_TID_RANGE_FAULT_SIZE            (TRAX_TID_RANGE_FAULT_END - TRAX_TID_RANGE_FAULT_START + 1)

/* Reset reason — emitted once per SESSION_START.
 * params: [0] = normalized reason (enum trax_fault_reset_reason_t)
 *         [1] = raw reset-status register value (vendor-specific, 0 if n/a) */
#define TRAX_TID_FAULT_RESET_REASON          0x7100

/* Recovered fault record — emitted after the first SESSION_START following
 * a fault-induced reset.
 * params: [0]  = wire format version (TRAX_FAULT_WIRE_VERSION)
 *         [1+] = struct trax_fault_record_t (packed, see trax_fault.h) */
#define TRAX_TID_FAULT_RECORD                0x7110

/* 0x7120-0x712F reserved: stack snapshot chunks (Phase 2). */

/* Flight recorder (Phase 3): pre-crash trace history replay.  The fault
 * handler snapshots the newest committed frames from the trace ring into
 * a noinit buffer; on the first session start after the crash the bytes
 * are replayed between BEGIN/END brackets, paced by trax_process() so
 * the replay can never overflow the new session's ring.
 *
 * BEGIN params: [0] = session_id of the crashed run
 *               [1] = total replay bytes that will follow
 * DATA  params: [0] = byte offset of this chunk
 *               [1] = exact chunk byte count (payload is word-padded)
 *               [2+] = raw frame bytes from the crashed run's ring
 * END   params: [0] = total bytes actually replayed */
#define TRAX_TID_FAULT_FLIGHT_BEGIN          0x7130
#define TRAX_TID_FAULT_FLIGHT_END            0x7131
#define TRAX_TID_FAULT_FLIGHT_DATA           0x7132

/* Dynamic-metadata snapshot (Phase 3): the crashed run's RTOS task and
 * object tables, captured by the fault handler into noinit RAM and
 * emitted once after the first SESSION_START following the crash (right
 * after TRAX_TID_FAULT_RECORD, before the flight replay).  `total` is the
 * live table count at fault time and `stored` the entries that fit
 * TRAX_CFG_FAULT_DYNMETA_TASKS/_OBJECTS — stored < total means the
 * snapshot was truncated and the host should tell the user to raise the
 * config cap.
 * TASKS params: [0] = session_id of the crashed run
 *               [1] = total task-table entries at fault time
 *               [2] = entries stored/following
 *               [3+] = stored × struct trax_rtos_task_wire_t (40 B each)
 * OBJS  params: [0] = session_id of the crashed run
 *               [1] = total object-table entries at fault time
 *               [2] = entries stored/following
 *               [3+] = stored × struct trax_rtos_object_wire_t (32 B each) */
#define TRAX_TID_FAULT_DYNMETA_TASKS         0x7140
#define TRAX_TID_FAULT_DYNMETA_OBJS          0x7141

/*=============================================================================
 ====================TRIGGER TIDs (0x7200 - 0x72FF)=============================
 ============================================================================*/

/* Probe-side triggers (see inc/trax_trigger.h).  A trigger is a named,
 * statically-defined rare-condition hook (analog-comparator over-voltage,
 * watchdog near-miss, protocol fault, ...) fired from any context with
 * TRAX_TRIGGER_FIRE().  Firing emits a trigger event frame when streaming
 * and releases a paused stream (pause/resume rides the SESSION_GAP resync
 * machinery — the resume frame attributes itself to the trigger TID).
 *
 * Designed for band-limited links: when bandwidth is plentiful the host
 * captures everything and Trigger Studio analyzes offline; probe-side
 * triggers exist for high stream rates + rare conditions, where the user
 * pauses the stream and lets the firmware resume it at the moment of
 * interest.
 *
 * Wire payload: TRAX_TID trigger event frame carries one user context
 * word (e.g. the measured fault value). */
#define TRAX_TID_RANGE_TRIGGER_START         0x7200
#define TRAX_TID_RANGE_TRIGGER_END           0x72FF
#define TRAX_TID_RANGE_TRIGGER_SIZE          (TRAX_TID_RANGE_TRIGGER_END - TRAX_TID_RANGE_TRIGGER_START + 1)
#define TRAX_TID_RANGE_TRIGGER_USER_START    TRAX_TID_RANGE_TRIGGER_START
#define TRAX_TID_RANGE_TRIGGER_USER_END      TRAX_TID_RANGE_TRIGGER_END
#define TRAX_TID_RANGE_TRIGGER_USER_SIZE     (TRAX_TID_RANGE_TRIGGER_USER_END - TRAX_TID_RANGE_TRIGGER_USER_START + 1)

/*=============================================================================
 ====================RTOS/OS TYPE CONSTANTS=====================================
 ============================================================================*/
/* RTOS/OS type constants — defined in trax_rtos_types.h */
#include "trax_rtos_types.h"

/*=============================================================================
 ====================DYNAMIC TID RANGE==========================================
 ============================================================================*/
#define TRAX_TID_RANGE_DYNAMIC_START     0x8000
#define TRAX_TID_RANGE_DYNAMIC_END       0xFFFE
#define TRAX_TID_RANGE_DYNAMIC_SIZE      (TRAX_TID_RANGE_DYNAMIC_END - TRAX_TID_RANGE_DYNAMIC_START + 1)

#define TRAX_TID_INVALID             0xFFFF

/*=============================================================================
 ====================TID VALIDATION MACROS=====================================
 ============================================================================*/

/**
 * @brief Check if TID is in a specific range
 */
#define TRAX_IS_LOG_TID(tid)    ((tid) >= TRAX_TID_RANGE_LOG_START && (tid) <= TRAX_TID_RANGE_LOG_END)
#define TRAX_IS_VAR_TID(tid)    ((tid) >= TRAX_TID_RANGE_VAR_START && (tid) <= TRAX_TID_RANGE_VAR_END)
#define TRAX_IS_STREAM_TID(tid) ((tid) >= TRAX_TID_RANGE_STREAM_START && (tid) <= TRAX_TID_RANGE_STREAM_END)
#define TRAX_IS_ISR_TID(tid)    ((tid) >= TRAX_TID_RANGE_ISR_START && (tid) <= TRAX_TID_RANGE_ISR_END)
#define TRAX_IS_USER_TID(tid)   ((tid) >= TRAX_TID_RANGE_USER_START && (tid) <= TRAX_TID_RANGE_USER_END)
#define TRAX_IS_KERNEL_TID(tid) ((tid) >= TRAX_TID_RANGE_KERNEL_START && (tid) <= TRAX_TID_RANGE_KERNEL_END)

/**
 * @brief Convert TID to array index (0-based)
 */
#define TRAX_TID_TO_INDEX(tid, base) ((tid) - (base))
#define TRAX_LOG_TID_INDEX(tid)      TRAX_TID_TO_INDEX(tid, TRAX_TID_RANGE_LOG_START)
#define TRAX_VAR_TID_INDEX(tid)      TRAX_TID_TO_INDEX(tid, TRAX_TID_RANGE_VAR_START)
#define TRAX_STREAM_TID_INDEX(tid)   TRAX_TID_TO_INDEX(tid, TRAX_TID_RANGE_STREAM_START)
#define TRAX_ISR_TID_INDEX(tid)      TRAX_TID_TO_INDEX(tid, TRAX_TID_RANGE_ISR_START)
#define TRAX_USER_TID_INDEX(tid)     TRAX_TID_TO_INDEX(tid, TRAX_TID_RANGE_USER_START)
#define TRAX_IS_SM_TID(tid)     ((tid) >= TRAX_TID_RANGE_SM_START && (tid) <= TRAX_TID_RANGE_SM_END)
#define TRAX_SM_TID_INDEX(tid)  TRAX_TID_TO_INDEX(tid, TRAX_TID_RANGE_SM_START)
#define TRAX_IS_TRIGGER_TID(tid)    ((tid) >= TRAX_TID_RANGE_TRIGGER_START && (tid) <= TRAX_TID_RANGE_TRIGGER_END)
#define TRAX_TRIGGER_TID_INDEX(tid) TRAX_TID_TO_INDEX(tid, TRAX_TID_RANGE_TRIGGER_START)

#define TRAX_IS_HEAP_TID(tid)   ((tid) >= TRAX_TID_HEAP_ALLOC && (tid) <= TRAX_TID_HEAP_ALLOC_FAILED)
#define TRAX_IS_STACK_TID(tid)  ((tid) == TRAX_TID_STACK_USAGE)

/* TRAX_CFG_STREAM_CNT — declared by the application in trax_config.h and defaulted
 * to 0 inside trax_stream.h.  Kept out of this file because TID ranges
 * are a different concern from runtime-array sizing. */

#endif /* TRAX_TID_H_ */
