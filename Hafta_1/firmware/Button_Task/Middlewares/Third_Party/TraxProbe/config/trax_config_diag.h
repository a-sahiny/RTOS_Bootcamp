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
 * @file           : trax_config_diag.h
 * @brief          : TraxProbe Diagnostic Module Configuration
 * @version        : 2.0.0
 ******************************************************************************
 * @attention
 *
 * Self-diagnostic module for TraxProbe (NOT for application diagnostics).
 *
 *   - Layer 1: METRICS    — trax_diag_stats counters (frames, peaks, rates)
 *   - Layer 2: EVENTS     — mask-based subscription, fired from trax_process()
 *   - Layer 3: THRESHOLDS — per-metric alert limits with hysteresis
 *   - Layer 4: AUTOPSY    — at-stop snapshot embedded in TRAX_TID_SESSION_STOP body
 *
 * Used to debug **the probe itself** — typical signals are a hot loop
 * flooding the ring buffer (`ring_buf_overflow_count` climbs), a transport
 * that is too slow (`backpressure_count` climbs), or frame corruption
 * caused by bad config (`frame_corrupt_count` climbs).
 *
 * Diagnostics are MANDATORY (always compiled in).  The cost is small —
 * roughly 2-3 KB code, 150 B static RAM, < 0.1 % CPU on a 100 MHz M4 in
 * steady state, and one ~88-byte snapshot frame per
 * TRAX_CFG_DIAG_REPORT_PERIOD_MS.  Disabling self-diagnostics on a
 * library whose entire purpose is observability was a footgun: it left
 * operators without an autopsy when streams stopped.
 *
 * If you need to dial down the cost on a constrained target:
 *
 *   - Raise TRAX_CFG_DIAG_REPORT_PERIOD_MS    (1 s -> 60 s saves wire B/s)
 *   - Lower TRAX_CFG_DIAG_MAX_SUBSCRIBERS     (default 2; min 1)
 *   - Set   TRAX_CFG_DIAG_DEFAULT_LOGS = 0    (suppress library log lines)
 *
 * The autopsy snapshot embedded in TRAX_TID_SESSION_STOP cannot be disabled —
 * it is the host's only reliable source of "why did the probe stop?".
 *
 ******************************************************************************
 */

#ifndef TRAX_CONFIG_DIAG_H_
#define TRAX_CONFIG_DIAG_H_

/*=============================================================================
 ====================DIAGNOSTIC SUBSCRIBER TABLE SIZE==========================
 ============================================================================*/

/**
 * @brief Maximum number of concurrent diagnostic subscribers.
 *
 * Each subscriber is one {callback, event_mask} entry in a static table.
 * No dynamic allocation — increasing this costs (N x 8) bytes of BSS.
 *
 * Override in trax_config.h:
 *   #define TRAX_CFG_DIAG_MAX_SUBSCRIBERS  4
 */
#ifndef TRAX_CFG_DIAG_MAX_SUBSCRIBERS
#define TRAX_CFG_DIAG_MAX_SUBSCRIBERS   2
#endif

/*=============================================================================
 ====================DEFAULT DIAGNOSTIC LOGS===================================
 ============================================================================*/

/**
 * @brief Emit default TRAX_LOG lines for critical diagnostic events.
 *
 * 0 — silent: trax_diag_report() only invokes user-installed subscribers.
 * 1 (default) — the diag module itself emits a TRAX_LOG_ERROR /
 *     TRAX_LOG_WARNING line on TRAX_TID_LOG_DIAG for the curated set of
 *     critical events listed below, *before* dispatching to subscribers.
 *     This guarantees the most important probe-health signals are visible
 *     in the Event View without any user setup.
 *
 * Curated default-log set:
 *
 *   ERROR   TRAX_DIAG_BUFFER_OVERFLOW   ring buffer full, frame dropped
 *   ERROR   TRAX_DIAG_FRAME_CORRUPT     decoder rejected a malformed frame
 *   WARNING TRAX_DIAG_THRESHOLD_CROSSED user-armed metric limit exceeded
 *   WARNING TRAX_DIAG_TRANSPORT_BACKPRESSURE  transport could not drain
 *
 * Verbose / periodic events (TRANSPORT_BUF_PEAK, TRANSPORT_STATS) are
 * intentionally NOT default-logged — they fire often enough to flood the
 * stream. Subscribe to them explicitly via trax_diag_subscribe() if you
 * need them in the trace.
 *
 * Re-entrancy: the dispatch is guarded by a per-call static flag, so a
 * default-log that itself triggers BUFFER_OVERFLOW (because the ring is
 * full) cannot recurse into trax_diag_report() and self-amplify.
 *
 * Override in trax_config.h:
 *   #define TRAX_CFG_DIAG_DEFAULT_LOGS  0   // suppress library logs
 */
#ifndef TRAX_CFG_DIAG_DEFAULT_LOGS
#define TRAX_CFG_DIAG_DEFAULT_LOGS      1
#endif

/*=============================================================================
 ====================SNAPSHOT REPORT CADENCE===================================
 ============================================================================*/

/**
 * @brief Period (in milliseconds) for periodic TRAX_TID_DIAG_REPORT snapshots.
 *
 * trax_diag_process() emits a TRAX_TID_DIAG_REPORT frame whenever this much wall
 * time has elapsed since the previous emission. The frame carries the full
 * trax_diag_stats_t snapshot, so the host UI can render a "live device
 * state" panel without polling.
 *
 * Cost at default 1000 ms: ~80 bytes payload + frame overhead per second.
 * Negligible compared to the trace stream itself but high enough to keep
 * peak/rate fields visibly alive on the host. Drop to 500 ms for a more
 * responsive UI; raise to 5000 ms or 60000 ms for slow links where every
 * byte counts.
 *
 * Critical events bypass this gate and ship immediately (see Layer 5 in
 * trax_diag.c) — this knob only governs the routine periodic heartbeat.
 *
 * Implementation note: timekeeping is derived from
 * trax_timebase.tick_overflow_cntr (the same source used for the
 * write_rate_bps calculation) — no extra platform timestamp port is
 * required. Resolution is one tick-overflow period
 * (TRAX_CFG_TICK_OVERFLOW_PERIOD / TRAX_CFG_TICK_RATE_HZ seconds).
 *
 * Override in trax_config.h:
 *   #define TRAX_CFG_DIAG_REPORT_PERIOD_MS  500
 */
#ifndef TRAX_CFG_DIAG_REPORT_PERIOD_MS
#define TRAX_CFG_DIAG_REPORT_PERIOD_MS  1000u
#endif

/*=============================================================================
 ====================FATAL-STOP LIFECYCLE (NOT CONFIGURABLE)===================
 ============================================================================*/

/* Fatal stream errors (frame validator reject, transport hard-error)
 * ALWAYS terminate streaming and ship a TRAX_TID_SESSION_STOP frame
 * to the host carrying the at-stop diag snapshot in its body.  The
 * mechanism is owned by trax_session's pending-stop latch (see
 * trax_session.h); the autopsy is built by trax_diag_build_report()
 * inside the same critical section as the stop reason word, so the
 * snapshot is atomic with the reason and cannot reach the host out of
 * order.
 *
 * Lifecycle:
 *   Phase 1 (hot path)  — the alloc-fail callback / send-frames validator
 *                         hit / transport hard-error flips
 *                         trax_session.streaming = 0 and arms
 *                         trax_session_arm_stop(reason, info).
 *   Phase 2 (slow path) — trax_process() drains pre-error frames via
 *                         trax_send_frames(), then trax_session_check_stop()
 *                         emits a single TRAX_TID_SESSION_STOP via the normal
 *                         pipeline.  The frame uses TRAX_FRAME_ARGS_PROTOCOL
 *                         and is NOT gated by TRAX_IS_SESSION_ACTIVE.  Body is
 *                         8 bytes (reason + info) followed by an 80-byte
 *                         WireDiagReport autopsy = 88 bytes total. */

/*=============================================================================
 ====================VALIDATION================================================
 ============================================================================*/

#if (TRAX_CFG_DIAG_MAX_SUBSCRIBERS < 1)
#error "TRAX_CFG_DIAG_MAX_SUBSCRIBERS must be >= 1"
#endif

#if (TRAX_CFG_DIAG_DEFAULT_LOGS != 0) && (TRAX_CFG_DIAG_DEFAULT_LOGS != 1)
#error "TRAX_CFG_DIAG_DEFAULT_LOGS must be 0 (suppress) or 1 (emit)"
#endif

#if (TRAX_CFG_DIAG_REPORT_PERIOD_MS < 100u)
#error "TRAX_CFG_DIAG_REPORT_PERIOD_MS must be >= 100 ms (faster cadence floods the wire)"
#endif

/*=============================================================================
 ====================LEGACY KNOB GUARD=========================================
 ============================================================================*/

/* The TRAX_CFG_ENABLE_DIAG toggle was removed in v3.0.0 — diagnostics are
 * now mandatory.  Catch stale config files that still try to disable
 * the module so the user gets a build-time error rather than a silent
 * "but where are my diag frames?" runtime surprise. */
#if defined(TRAX_CFG_ENABLE_DIAG) && (TRAX_CFG_ENABLE_DIAG == 0)
#error "TRAX_CFG_ENABLE_DIAG=0 is no longer supported; diagnostics are mandatory. Remove the #define from your trax_config.h."
#endif

#endif /* TRAX_CONFIG_DIAG_H_ */
