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
 * @file           : trax_diag.h
 * @brief          : TraxProbe Unified Diagnostic Module
 * @version        : 3.0.0
 ******************************************************************************
 * @attention
 *
 * Four-layer diagnostic architecture:
 *
 *   Layer 1 — METRICS (always available, zero-overhead polling)
 *     trax_diag_stats_t updated on the hot path and ~1 Hz.
 *     Read at any time via trax_diag_get_stats() — no callbacks required.
 *
 *   Layer 2 — EVENTS (mask-based subscription, up to N independent consumers)
 *     Each subscriber registers an event bitmask and a callback.
 *     Only events whose bit is set in the subscriber's mask are delivered.
 *     All dispatch happens from trax_process() context, never from ISR.
 *     Maximum subscribers: TRAX_CFG_DIAG_MAX_SUBSCRIBERS (default 2).
 *
 *   Layer 3 — THRESHOLDS (per-metric alert limits, hysteresis-protected)
 *     Configure a limit on any metric with trax_diag_set_threshold().
 *     When the metric crosses the limit, TRAX_DIAG_THRESHOLD_CROSSED is fired
 *     once (one-shot until the metric drops back below the limit).
 *     Subscribe with TRAX_DIAG_EVT_THRESHOLD to receive these alerts.
 *
 *   Layer 4 — DEFAULT LOGS (automatic, on by default)
 *     trax_diag_report() emits a TRAX_LOG_ERROR / TRAX_LOG_WARNING line on
 *     TRAX_TID_LOG_DIAG (tag "diag") for the curated set of critical events —
 *     BUFFER_OVERFLOW, FRAME_CORRUPT (ERROR), THRESHOLD_CROSSED,
 *     TRANSPORT_BACKPRESSURE (WARNING) — *before* dispatching to user
 *     subscribers. The point is that the most important probe-health
 *     signals are visible in the Event View with zero application setup.
 *     Verbose / periodic events (BUF_PEAK, TRANSPORT_STATS) are NOT
 *     default-logged to avoid stream spam — subscribe to them explicitly
 *     if you need them. Disable the whole layer with
 *     TRAX_CFG_DIAG_DEFAULT_LOGS = 0.
 *
 * MANDATORY MODULE:
 *   Diagnostics are always compiled in.  The TRAX_CFG_ENABLE_DIAG toggle
 *   was removed in v3.0.0 — disabling self-observation on a probe library
 *   was a footgun (no autopsy on stream stop, no overflow visibility).
 *
 *   Cost is small: ~2-3 KB code, ~150 B static RAM, < 0.1 % CPU on a
 *   100 MHz M4, and one ~96-byte snapshot frame per
 *   TRAX_CFG_DIAG_REPORT_PERIOD_MS.  See config/trax_config_diag.h for
 *   the tunable knobs (period, subscriber count, default-log emission).
 *
 ******************************************************************************
 */

#ifndef TRAX_DIAG_H_
#define TRAX_DIAG_H_

#include <stdint.h>
#include <stddef.h>
#include "trax_config_default.h"  /* For TRAX_CFG_DIAG_MAX_SUBSCRIBERS, period */
#include "trax_data_types.h"      /* TRAX_PACKED, TRAX_STATIC_ASSERT */
#include "trax_tid.h"             /* enum trax_stop_reason_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Defaults for TRAX_CFG_DIAG_MAX_SUBSCRIBERS / DEFAULT_LOGS / REPORT_PERIOD_MS
 * live in config/trax_config_diag.h (included via trax_config_default.h). */

/*=============================================================================
 *                      DIAGNOSTIC EVENTS
 *============================================================================*/

/**
 * @brief Diagnostic event types.
 */
enum trax_diag_event_t {
	TRAX_DIAG_BUFFER_OVERFLOW      = 0,  /**< Ring buffer full — frames dropped.
	                                          info = new overflow count since last report */
	TRAX_DIAG_TRANSPORT_WRITE_FAIL = 1,  /**< Transport write incomplete (HARD error).
	                                          info = bytes that failed to write */
	TRAX_DIAG_FRAME_CORRUPT        = 2,  /**< Frame failed validation before TX.
	                                          info = trax_validate_err_total count */
	TRAX_DIAG_TRANSPORT_STATS      = 3,  /**< Periodic throughput report (~1 Hz).
	                                          info = write_rate_bps */
	TRAX_DIAG_TRANSPORT_BUF_PEAK   = 4,  /**< Transport buffer new high-water mark.
	                                          info = new peak usage in bytes */
	TRAX_DIAG_THRESHOLD_CROSSED    = 5,  /**< A configured metric exceeded its limit.
	                                          info = trax_diag_metric_t that crossed */
	TRAX_DIAG_TRANSPORT_BACKPRESSURE = 6, /**< Transport temporarily full; batch
	                                          deferred to next trax_process() (SOFT —
	                                          no data lost). info = bytes deferred */
};

/*=============================================================================
 *                      EVENT BITMASK CONSTANTS
 *============================================================================*/

/**
 * @brief Bitmask constants for trax_diag_subscribe().
 *
 * OR together any combination to receive exactly the events you care about.
 *
 * Example — subscribe to errors only:
 *   trax_diag_subscribe(TRAX_DIAG_EVT_OVERFLOW | TRAX_DIAG_EVT_WRITE_FAIL, cb);
 */
#define TRAX_DIAG_EVT_OVERFLOW       (1u << TRAX_DIAG_BUFFER_OVERFLOW)
#define TRAX_DIAG_EVT_WRITE_FAIL     (1u << TRAX_DIAG_TRANSPORT_WRITE_FAIL)
#define TRAX_DIAG_EVT_FRAME_CORRUPT  (1u << TRAX_DIAG_FRAME_CORRUPT)
#define TRAX_DIAG_EVT_STATS          (1u << TRAX_DIAG_TRANSPORT_STATS)
#define TRAX_DIAG_EVT_BUF_PEAK       (1u << TRAX_DIAG_TRANSPORT_BUF_PEAK)
#define TRAX_DIAG_EVT_THRESHOLD      (1u << TRAX_DIAG_THRESHOLD_CROSSED)
#define TRAX_DIAG_EVT_BACKPRESSURE   (1u << TRAX_DIAG_TRANSPORT_BACKPRESSURE)
#define TRAX_DIAG_EVT_ALL            0xFFFFFFFFu

/*=============================================================================
 *                      THRESHOLD METRICS
 *============================================================================*/

/**
 * @brief Metrics that can have alert thresholds configured.
 *
 * Pass one of these to trax_diag_set_threshold().
 * When the metric's current value >= the configured limit,
 * TRAX_DIAG_THRESHOLD_CROSSED is fired (info = metric id).
 */
/**
 * Metrics available for threshold monitoring (trax_diag_set_threshold).
 *
 * These are continuous, fill-level metrics that can rise and fall.
 * Ring buffer allocation failures are NOT included here — they are
 * discrete error events reported directly via TRAX_DIAG_BUFFER_OVERFLOW,
 * which fires on every new batch of failures with an exact count.
 */
enum trax_diag_metric_t {
	TRAX_DIAG_METRIC_RING_BUF_USED       = 0,  /**< Current trax ring buffer bytes used   */
	TRAX_DIAG_METRIC_TRANSPORT_BUF_USED  = 1,  /**< Current transport buffer bytes used   */
	TRAX_DIAG_METRIC_WRITE_FAIL_COUNT    = 2,  /**< Accumulated transport write failures  */
	TRAX_DIAG_METRIC_COUNT                      /**< Sentinel — do not use directly        */
};

/*=============================================================================
 *                      CALLBACK TYPE
 *============================================================================*/

/**
 * @brief Diagnostic callback function type.
 *
 * Called from trax_process() context (main loop, never from ISR).
 *
 * @param event  Which event occurred (see trax_diag_event_t)
 * @param info   Event-specific value (see per-event docs above)
 */
typedef void (*trax_diag_callback_t)(enum trax_diag_event_t event, uint32_t info);


/*=============================================================================
 *                      DIAGNOSTIC STATISTICS  (Layer 1 — Metrics)
 *============================================================================*/



/**
 * @brief Unified diagnostic statistics — transport-agnostic.
 *
 * Updated by trax_diag_record_write() on every frame (hot path).
 * Buffer usage and rate computed periodically by trax_diag_process() (~1 Hz).
 *
 * All counters are volatile for safe debugger inspection at any breakpoint.
 * Read via trax_diag_get_stats() without registering any callback.
 */
struct trax_diag_stats_t {
	/* ── Per-frame write counters ── */
	volatile uint32_t total_bytes_written;       /**< Bytes accepted by transport              */
	volatile uint32_t total_bytes_requested;     /**< Bytes attempted (including failures)     */
	volatile uint32_t total_frames;              /**< Frames sent successfully                 */
	volatile uint32_t write_fail_count;          /**< Writes where written < requested         */
	volatile uint32_t backpressure_count;        /**< Times transport was full at flush time
	                                                  (data deferred to next tick — NO loss)   */
	volatile uint32_t peak_frame_size;           /**< Largest single frame, bytes              */

	/* ── Buffer usage peaks ── */
	volatile uint32_t peak_trax_buf_used;        /**< Ring buffer high-water mark, bytes       */
	volatile uint32_t peak_transport_buf_used;   /**< Transport buffer high-water mark, bytes  */
	volatile uint32_t peak_transport_write_size; /**< Frame size at last transport peak        */
	uint32_t at_peak_tick_overflow;           /**< Tick overflow counter at time of last transport peak */

	/* ── Current snapshot (updated ~1 Hz) ── */
	volatile uint32_t trax_buf_used;             /**< Current ring buffer usage, bytes         */
	volatile uint32_t transport_buf_used;        /**< Current transport buffer usage, bytes    */

	/* ── Throughput (updated ~1 Hz) ── */
	volatile uint32_t write_rate_bps;            /**< Current throughput, bits/sec             */

	/* ── Always-on event accounting (updated by trax_diag_report) ──
	 * These fields make event activity visible in the debugger
	 * (Eclipse Expressions on trax_diag_stats) without any subscriber
	 * having to be registered. */
	volatile uint32_t frame_corrupt_count;       /**< TRAX_DIAG_FRAME_CORRUPT total fires      */
	volatile uint32_t ring_buf_overflow_count;   /**< Total ring buffer alloc failures
	                                                  (sum of all TRAX_DIAG_BUFFER_OVERFLOW
	                                                   info values since reset)                */

	/* ── Last reported event (snapshot — updated by trax_diag_report) ──
	 * last_event holds the enum trax_diag_event_t id of the most recent
	 * event. UINT32_MAX (0xFFFFFFFF) means "no event since reset". */
	volatile uint32_t last_event;                /**< Last event id (UINT32_MAX = none)        */
	volatile uint32_t last_event_info;           /**< info argument of the last event          */
	volatile uint32_t last_event_at_tick_overflow;/**< tick_overflow_cntr at last event        */

	/* ── Static capacities (set once by trax_diag_init/reset) ──
	 * Shipped to the host so the Diagnostics panel can render buffer
	 * fill levels as "used / capacity" instead of a bare byte count —
	 * the capacities are configured on the probe (trax_config.h /
	 * transport backend) and are otherwise invisible host-side. */
	uint32_t trax_buf_size;                      /**< Ring buffer capacity, bytes
	                                                  (TRAX_CFG_OUT_BUFFER_SIZE32 * 4)         */
	uint32_t transport_buf_size;                 /**< Transport TX buffer capacity, bytes
	                                                  (0 = unknown / not reported)             */
};

/** Stats instance lives in trax_diag.c — access via trax_diag_get_stats().
 *  The extern is intentionally omitted: reading fields directly from
 *  multiple call sites can produce a torn snapshot if a write occurs
 *  between individual field accesses. Use the copy API instead. */

/*=============================================================================
 *                      WIRE PROTOCOL  (host receives this)
 *============================================================================*/

/**
 * @brief On-the-wire diagnostic report.
 *
 * Sent as the payload of a TRAX_TID_DIAG_REPORT (0x0017) frame under a
 * hybrid cadence:
 *
 *   PERIODIC  — one frame per TRAX_CFG_DIAG_REPORT_PERIOD_MS (default
 *               1000 ms), driven by trax_diag_process(). Wire cost is
 *               bounded and predictable: ~88 bytes/period regardless
 *               of trace activity. flags=0.
 *
 *   IMMEDIATE — driven by trax_diag_report() for the curated set of
 *               stream-critical events (BUFFER_OVERFLOW, FRAME_CORRUPT,
 *               THRESHOLD_CROSSED, TRANSPORT_BACKPRESSURE). Bypasses
 *               the period gate so the operator sees alarms within
 *               microseconds, not up to a full period late.
 *               flags=EVENT_FIRED.
 *
 * Soft events (peak ratchets, near-full warnings, periodic transport
 * stats) ride the next periodic snapshot — no immediate emit — so
 * busy systems don't flood the wire with 80-byte heartbeats every
 * time a new peak frame arrives.
 *
 * Layout: small wire header followed by a bit-for-bit copy of
 * trax_diag_stats_t (volatile qualifier dropped — wire view is a
 * point-in-time snapshot, not a live struct). Host parses by casting
 * the payload to a matching WireDiagReport struct.
 *
 * Wire-stable: any new fields added to trax_diag_stats_t must also
 * be appended to the host-side mirror; existing offsets must NOT
 * change. Total size is asserted below to catch silent layout drift.
 */
struct trax_diag_report_t {
	/* Wire header (8 bytes) */
	uint32_t snapshot_seq;        /**< Monotonic counter; host detects gaps. */
	uint32_t flags;               /**< [1]=event_fired (immediate emit
	                                   triggered by a stream-critical
	                                   event); 0 = periodic snapshot.
	                                   Bits [0] and [2] are reserved
	                                   (legacy peak/counter hints, no
	                                   longer set under the periodic
	                                   cadence model). */

	/* Stats payload — mirror of trax_diag_stats_t (volatile dropped) */
	uint32_t total_bytes_written;
	uint32_t total_bytes_requested;
	uint32_t total_frames;
	uint32_t write_fail_count;
	uint32_t backpressure_count;
	uint32_t peak_frame_size;
	uint32_t peak_trax_buf_used;
	uint32_t peak_transport_buf_used;
	uint32_t peak_transport_write_size;
	uint32_t at_peak_tick_overflow;
	uint32_t trax_buf_used;
	uint32_t transport_buf_used;
	uint32_t write_rate_bps;
	uint32_t frame_corrupt_count;
	uint32_t ring_buf_overflow_count;
	uint32_t last_event;
	uint32_t last_event_info;
	uint32_t last_event_at_tick_overflow;
	uint32_t trax_buf_size;
	uint32_t transport_buf_size;
} TRAX_PACKED;

/** Flag bits for trax_diag_report_t.flags
 *
 *  EVENT_FIRED  — set on snapshots emitted in response to a stream-
 *                 critical event (BUFFER_OVERFLOW, FRAME_CORRUPT,
 *                 THRESHOLD_CROSSED, TRANSPORT_BACKPRESSURE) and on the
 *                 autopsy snapshot embedded in TRAX_TID_SESSION_STOP.  The
 *                 host should highlight the row; absence means this is
 *                 a routine periodic heartbeat.
 *  PEAK_CHANGED, COUNTER_CHANGED — reserved; the current cadence model
 *                 does not differentiate these from a normal snapshot,
 *                 but the bits are kept in the wire layout for forward-
 *                 compatible expansion. */
#define TRAX_DIAG_RPT_FLAG_PEAK_CHANGED      (1u << 0)
#define TRAX_DIAG_RPT_FLAG_EVENT_FIRED       (1u << 1)
#define TRAX_DIAG_RPT_FLAG_COUNTER_CHANGED   (1u << 2)

TRAX_STATIC_ASSERT(sizeof(struct trax_diag_report_t) == 88,
                   "trax_diag_report_t must be 88 bytes (8 header + 20 stats * 4)");

/*=============================================================================
 *                      API — Init / Reset
 *============================================================================*/

/**
 * @brief Initialize the diagnostic module.
 *
 * Clears all stats, clears the subscriber table, clears the threshold table,
 * and seeds the rate timer. Called automatically by trax_init().
 */
void trax_diag_init(void);

/**
 * @brief Reset diagnostic counters, re-arm thresholds.
 *
 * Zeros all statistics and reseeds the rate timer. Subscribers and threshold
 * limits are preserved; threshold triggered-flags are cleared so thresholds
 * will fire again if crossed after the reset.
 *
 * Use this on trace restart instead of trax_diag_init().
 */
void trax_diag_reset(void);

/*=============================================================================
 *                      API — Layer 2: Event Subscription
 *============================================================================*/

/**
 * @brief Subscribe to a specific set of diagnostic events.
 *
 * @param event_mask  OR of TRAX_DIAG_EVT_* constants. Use TRAX_DIAG_EVT_ALL
 *                    to receive every event.
 * @param callback    Function to call when a matching event fires.
 *                    Must not be NULL.
 *
 * @return  0  Subscription registered.
 * @return -1  Subscriber table full (TRAX_CFG_DIAG_MAX_SUBSCRIBERS reached).
 *
 * Subscribing the same callback again updates its mask in-place.
 */
int trax_diag_subscribe(uint32_t event_mask, trax_diag_callback_t callback);

/**
 * @brief Remove a previously registered subscriber.
 *
 * @param callback  The callback pointer passed to trax_diag_subscribe().
 *                  No-op if callback is not found.
 */
void trax_diag_unsubscribe(trax_diag_callback_t callback);

/*=============================================================================
 *                      API — Layer 3: Threshold Triggers
 *============================================================================*/

/**
 * @brief Configure an alert threshold on a metric.
 *
 * When the metric's current value >= limit, TRAX_DIAG_THRESHOLD_CROSSED is
 * fired once (hysteresis: will not fire again until the metric first drops
 * below limit and then crosses again).
 *
 * Subscribers must include TRAX_DIAG_EVT_THRESHOLD in their mask to receive
 * this event. The event's info field carries the trax_diag_metric_t enum value.
 *
 * @param metric  Which metric to monitor (see trax_diag_metric_t).
 * @param limit   Threshold value in the metric's native unit (bytes or counts).
 *
 * Example — alert when ring buffer exceeds 75% of capacity:
 *   trax_diag_set_threshold(TRAX_DIAG_METRIC_RING_BUF_USED,
 *                           TRAX_CFG_OUT_BUFFER_SIZE32 * 4 * 75 / 100);
 */
void trax_diag_set_threshold(enum trax_diag_metric_t metric, uint32_t limit);

/**
 * @brief Remove a previously configured threshold.
 *
 * @param metric  The metric whose threshold should be cleared.
 */
void trax_diag_clear_threshold(enum trax_diag_metric_t metric);

/**
 * @brief Re-arm a threshold so it can fire again immediately.
 *
 * Normally a threshold is suppressed after firing until the metric drops
 * back below the limit (hysteresis). Call this from within a
 * TRAX_DIAG_THRESHOLD_CROSSED callback — or at any point afterwards — to
 * allow the threshold to fire again on the very next check_thresholds()
 * cycle, even if the metric is still above the limit.
 *
 * The configured limit and active flag are preserved; only the suppression
 * flag is cleared.
 *
 * @param metric  The metric to re-arm. No-op if not active.
 *
 * Example — re-trigger every time the callback is invoked:
 *   void on_diag(enum trax_diag_event_t ev, uint32_t info) {
 *       if (ev == TRAX_DIAG_THRESHOLD_CROSSED)
 *           trax_diag_rearm_threshold((enum trax_diag_metric_t)info);
 *   }
 */
void trax_diag_rearm_threshold(enum trax_diag_metric_t metric);

/*=============================================================================
 *                      API — Layer 1: Polling
 *============================================================================*/

/**
 * @brief Copy a consistent snapshot of the diagnostic statistics.
 *
 * The live stats struct is only accessible inside trax_diag.c.
 * This function copies all fields into caller-provided storage so that
 * the caller always sees a coherent set of values rather than a mix of
 * pre- and post-write state from a concurrent trax_process() call.
 *
 * @param out  Destination buffer. Must not be NULL.
 *
 * Example:
 *   struct trax_diag_stats_t s;
 *   trax_diag_get_stats(&s);
 *   my_display_update(s.write_rate_bps, s.peak_trax_buf_used);
 */
void trax_diag_get_stats(struct trax_diag_stats_t *out);

/*=============================================================================
 *                      API — Internal (called by library modules)
 *============================================================================*/

/**
 * @brief Periodic processing — called automatically from trax_process().
 *
 * Snapshots buffer usage, checks allocation failures, evaluates thresholds,
 * computes throughput (~1 Hz), and dispatches events to subscribers.
 *
 * @note Not intended for direct application use.
 */
void trax_diag_process(void);

/**
 * @brief Report an immediate diagnostic event.
 *
 * Used internally by TraxProbe modules (trax_core.c, trax_record_write, etc.)
 *
 * Dispatch order:
 *   1. Update last_event / counter snapshot in trax_diag_stats_t.
 *   2. (TRAX_CFG_DIAG_DEFAULT_LOGS=1) Emit a TRAX_LOG line on TRAX_TID_LOG_DIAG
 *      for the curated critical-event set. Re-entrancy is blocked by an
 *      internal flag so a default-log that itself triggers BUFFER_OVERFLOW
 *      cannot recurse back into trax_diag_report() and self-amplify.
 *   3. Walk the subscriber table and invoke every callback whose mask
 *      includes the event bit.
 *   4. For stream-critical events (BUFFER_OVERFLOW, FRAME_CORRUPT,
 *      THRESHOLD_CROSSED, TRANSPORT_BACKPRESSURE) — emit an immediate
 *      TRAX_TID_DIAG_REPORT snapshot to the host with flags=EVENT_FIRED,
 *      bypassing the periodic gate. Soft events wait for the next
 *      periodic snapshot from trax_diag_process().
 *
 * Always called from trax_process() context, never from ISR.
 *
 * @param event  Event type
 * @param info   Event-specific payload
 */
void trax_diag_report(enum trax_diag_event_t event, uint32_t info);

/**
 * @brief Record one stream write — hot path, called per frame.
 *
 * @param requested             Bytes requested from the transport
 * @param written               Bytes the transport actually accepted
 * @param transport_buf_before  Transport buffer fill level before this write
 */
void trax_diag_record_write(size_t requested, size_t written,
                            size_t transport_buf_before);

/**
 * @brief Record a transport backpressure event — soft, no data loss.
 *
 * Called from trax_send_frames() when the transport's atomic write
 * returned 0 because the batch did not fit. The frames remain queued in
 * the application ring and will be retried on the next trax_process()
 * call. This is informational only — no counter in trax_diag_stats_t
 * is bumped that would suggest data loss.
 *
 * Increments stats.backpressure_count and fires
 * TRAX_DIAG_TRANSPORT_BACKPRESSURE with @p deferred_bytes as info.
 *
 * @param deferred_bytes  Total size of the batch that was deferred
 */
void trax_diag_record_backpressure(size_t deferred_bytes);

/**
 * @brief Build a wire-format diag report into caller-provided memory.
 *
 * Atomically copies the current trax_diag_stats into @p p_out under a
 * critical section, prepends the wire header (snapshot_seq + flags),
 * and increments the internal report sequence counter exactly once.
 *
 * Used by:
 *   - trax_diag_send_report() — to fill its stack-local payload before
 *     committing to the ring buffer (periodic + immediate cadence).
 *   - trax_meta_tx_send_stop() — to embed the at-stop autopsy snapshot
 *     directly in the TRAX_TID_SESSION_STOP frame body, eliminating the
 *     race where the autopsy and the stop reason arrive out of order
 *     (or where the autopsy frame fails to allocate separately, e.g.
 *     against a still-full ring).
 *
 * @param p_out  Destination wire frame slot (88 bytes). Must not be NULL.
 * @param flags  OR of TRAX_DIAG_RPT_FLAG_* constants stamped into the
 *               wire header so the host can render the snapshot's
 *               provenance (periodic, event-driven, autopsy, ...).
 *
 * @note Always called from trax_process() context, never from ISR.
 *       Safe to call with the buffer critical section already held.
 */
void trax_diag_build_report(struct trax_diag_report_t *p_out, uint32_t flags);

#ifdef __cplusplus
}
#endif

#endif /* TRAX_DIAG_H_ */
