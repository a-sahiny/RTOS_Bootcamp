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
 * @file           : trax_diag.c
 * @brief          : TraxProbe Unified Diagnostic Module Implementation
 * @version        : 3.0.0
 *
 * @attention
 *   Four-layer implementation:
 *
 *   Layer 1 — METRICS
 *     trax_diag_stats updated on hot-path (trax_diag_record_write) and
 *     at ~1 Hz (trax_diag_process). Always readable via trax_diag_get_stats().
 *
 *   Layer 2 — EVENTS / SUBSCRIBERS
 *     subscribers[]: fixed table of {callback, mask} entries.
 *     trax_diag_report() iterates and calls matching entries.
 *     Dispatch only from trax_process() context — never from ISR.
 *
 *   Layer 3 — THRESHOLDS
 *     thresholds[]: per-metric {limit, active, triggered} entries.
 *     Checked every trax_process() call via check_thresholds().
 *     Hysteresis: triggered flag prevents repeat-firing until metric drops.
 *
 *   Layer 4 — DEFAULT LOGS  (TRAX_CFG_DIAG_DEFAULT_LOGS = 1, default on)
 *     emit_default_log() maps the curated critical-event set to
 *     TRAX_LOG_ERROR / TRAX_LOG_WARNING calls on dedicated TRAX_TID_LOG_DIAG_*
 *     TIDs. Runs *before* the subscriber loop so users see the line even
 *     if no subscriber is registered. Re-entrancy is blocked by a static
 *     in_default_log flag — a default-log frame that itself triggers
 *     BUFFER_OVERFLOW (because the ring is full) cannot recurse back
 *     into trax_diag_report() and self-amplify the failure.
 *
 *   Layer 5 — SNAPSHOT REPORTS  (TRAX_TID_DIAG_REPORT, hybrid cadence)
 *     trax_diag_send_report() ships the full trax_diag_stats payload to
 *     the host. Cadence is hybrid:
 *       PERIODIC  — driven by trax_diag_process(), gated to one
 *                   emission per TRAX_CFG_DIAG_REPORT_PERIOD_MS
 *                   (default 1000 ms). Carries flags=0.
 *       IMMEDIATE — driven by trax_diag_report() for the curated set
 *                   of stream-critical events (BUFFER_OVERFLOW,
 *                   FRAME_CORRUPT, THRESHOLD_CROSSED,
 *                   TRANSPORT_BACKPRESSURE). Bypasses the period gate
 *                   so the operator sees alarms within microseconds,
 *                   not up to 1 second late. Carries flags=EVENT_FIRED.
 *     Soft events (peak ratchets, near-full warnings, periodic
 *     transport stats) ride the next periodic snapshot — keeps the
 *     wire quiet on busy-but-healthy systems where peaks oscillate
 *     many times per second.
 ******************************************************************************
 */

#include "trax_diag.h"
#include "trax_buffer.h"         /* trax_buffer.alloc_fail_cntr, pointers   */
#include "trax_timestamp.h"      /* trax_timebase, TRAX_FRAME_TIMEPACKED_PUT */
#include "trax_transport.h"      /* TRAX_TRANSPORT_BYTES_USED() selector     */
#include "trax_hw.h"       /* TRAX_PORT_ENTER/EXIT_CRITICAL_SECTION   */
#include "trax_tid.h"            /* TRAX_TID_DIAG_REPORT, TRAX_TID_LOG_DIAG_*          */
#include "trax_session.h"        /* TRAX_IS_SESSION_ACTIVE()                      */
#include "trax_frame.h"          /* trax_trans_cntr, frame layout            */
#include "trax_utility.h"        /* TRAX_PUT32                               */
#if (TRAX_CFG_DIAG_DEFAULT_LOGS)
#include "trax_log.h"            /* TRAX_LOG_ERROR / TRAX_LOG_WARNING        */
#endif
#include <stdbool.h>
#include <string.h>              /* memcpy */

/* TraxProbe disabled (TRAX_ENABLE==0): whole unit compiles out. The diag API is
 * provided as inline no-op stubs in trax_diag.h. */
#if TRAX_ENABLE

/*=============================================================================
 ====================LOCAL MACRO FUNCTIONS=====================================
 ============================================================================*/

/*=============================================================================
 ====================LOCAL MACRO DEFINITIONS===================================
 ============================================================================*/

/** Minimum elapsed time between throughput computations (milliseconds) */
#define RATE_UPDATE_PERIOD_MS   1000u

/*=============================================================================
 ====================GLOBAL VARIABLES==========================================
 ============================================================================*/

struct trax_diag_stats_t trax_diag_stats;

/*=============================================================================
 ====================LOCAL STRUCTURES==========================================
 ============================================================================*/

struct trax_diag_subscriber_t {
	trax_diag_callback_t callback;
	uint32_t mask;
};

struct trax_diag_threshold_t {
	uint32_t limit;
	bool active; /**< Threshold is configured */
	bool triggered; /**< Fired and not yet reset — suppress repeat */
};

/*=============================================================================
 ====================LOCAL VARIABLES===========================================
 ============================================================================*/

static struct trax_diag_subscriber_t subscribers[TRAX_CFG_DIAG_MAX_SUBSCRIBERS];

static struct trax_diag_threshold_t thresholds[TRAX_DIAG_METRIC_COUNT];

/** Last seen alloc-fail counter — detects new ring buffer overflows */
static uint32_t last_alloc_fail_cntr;

static uint32_t prev_tick_overflow;
static uint32_t prev_bytes;

/* ── Snapshot report dispatch state (Layer 5) ───────────────────────────────
 *
 * Cadence model is hybrid:
 *
 *   PERIODIC  trax_diag_process() emits one TRAX_TID_DIAG_REPORT per
 *             TRAX_CFG_DIAG_REPORT_PERIOD_MS (default 1000 ms). Carries the
 *             full trax_diag_stats_t snapshot so the host always has a
 *             current view of rates / peaks / counters. flags=0 (or
 *             EVENT_FIRED if a critical event happened to fire in the same
 *             trax_diag_process() pass).
 *
 *   IMMEDIATE trax_diag_report() ships a snapshot inline, bypassing the
 *             period gate, when the event is one of the curated "stream
 *             critical" set:
 *               - TRAX_DIAG_BUFFER_OVERFLOW
 *               - TRAX_DIAG_FRAME_CORRUPT
 *               - TRAX_DIAG_THRESHOLD_CROSSED
 *               - TRAX_DIAG_TRANSPORT_BACKPRESSURE
 *             Latency-to-host is what makes these useful — without
 *             bypass, a buffer-overflow alarm could sit up to 1 second
 *             before the operator notices. flags=EVENT_FIRED (+ extra
 *             bits if applicable). After an immediate emit we advance
 *             last_emit_overflow so the next periodic tick won't double-
 *             send within the same period.
 *
 * Soft events (BUFFER_NEAR_FULL, STACK_HIGH, CPU_HIGH, TRANSPORT_BUF_PEAK,
 * TRANSPORT_STATS) are NOT critical — they show up in the next periodic
 * snapshot via trax_diag_stats.last_event. This keeps the wire quiet on
 * busy-but-healthy systems where peaks ratchet many times per second.
 *
 * `report_seq` is a monotonic counter the host uses to detect dropped
 * report frames (ring overflow). `last_emit_overflow` is the value of
 * trax_timebase.tick_overflow_cntr at the last successful emission. */
static          uint32_t report_seq;
static          uint32_t last_emit_overflow;

/** Convert TRAX_CFG_DIAG_REPORT_PERIOD_MS to a tick_overflow_cntr delta.
 *  Each overflow unit represents (TICK_OVERFLOW_PERIOD / TICK_RATE_HZ)
 *  seconds, so:
 *
 *      overflows_per_period =
 *          (PERIOD_MS * TICK_RATE_HZ) /
 *          (1000 * TICK_OVERFLOW_PERIOD)
 *
 *  Rounded up so we never shorten the configured period.
 *  Floored to 1 so very fast clocks don't collapse the gate to zero. */
#if defined(TRAX_CFG_TICK_RATE_HZ) && (TRAX_CFG_TICK_RATE_HZ > 0) && \
	defined(TRAX_CFG_TICK_OVERFLOW_PERIOD) && (TRAX_CFG_TICK_OVERFLOW_PERIOD > 0)
/* No (uint32_t) casts here: this macro is also evaluated in a #if
 * preprocessor context (line below). C-type casts are illegal there —
 * the preprocessor would replace unknown identifiers with 0, turning
 * "(uint32_t)X" into "(0)(X)" which triggers "missing binary operator".
 * The preprocessor evaluates #if arithmetic in intmax_t so the cast
 * is both unnecessary and harmful. The cast is applied where needed
 * in the C expression that actually uses the value. */
#  define DIAG_PERIOD_OVERFLOW_DELTA                                      \
	(((TRAX_CFG_DIAG_REPORT_PERIOD_MS *                                   \
	   TRAX_CFG_TICK_RATE_HZ) +                                           \
	  (1000u * TRAX_CFG_TICK_OVERFLOW_PERIOD) - 1u) /                    \
	 (1000u * TRAX_CFG_TICK_OVERFLOW_PERIOD))
#else
/* Fallback: assume each overflow ≈ 1 second (matches the rate-calc fallback
 * in trax_diag_process). At 1 s per overflow, period_ms / 1000 overflows. */
#  define DIAG_PERIOD_OVERFLOW_DELTA                                      \
	((TRAX_CFG_DIAG_REPORT_PERIOD_MS + 999u) / 1000u)
#endif

#if (DIAG_PERIOD_OVERFLOW_DELTA == 0u)
#  undef  DIAG_PERIOD_OVERFLOW_DELTA
#  define DIAG_PERIOD_OVERFLOW_DELTA 1u
#endif

/*=============================================================================
 ====================LOCAL FUNCTION DECLARATION================================
 ============================================================================*/

static uint32_t get_metric(enum trax_diag_metric_t m);
static void check_thresholds(void);
static void trax_diag_send_report(uint32_t flags, bool force);
static bool is_stream_critical(enum trax_diag_event_t event);
#if (TRAX_CFG_DIAG_DEFAULT_LOGS)
static void emit_default_log(enum trax_diag_event_t event, uint32_t info);
#endif

/*=============================================================================
 ====================GLOBAL FUNCTION IMPLEMANTATION============================
 ============================================================================*/

void trax_diag_init(void)
{
	uint8_t i;

	for (i = 0; i < (uint8_t)TRAX_CFG_DIAG_MAX_SUBSCRIBERS; i++) {
		subscribers[i].callback = (trax_diag_callback_t)0;
		subscribers[i].mask = 0;
	}

	for (i = 0; i < (uint8_t)TRAX_DIAG_METRIC_COUNT; i++) {
		thresholds[i].limit = 0;
		thresholds[i].active = false;
		thresholds[i].triggered = false;
	}

	trax_diag_reset();
}

void trax_diag_reset(void)
{
	uint8_t i;

	/* Seed alloc-fail baseline to current value — avoids spurious overflow
	 * event on the first trax_process() after a trace restart */
	last_alloc_fail_cntr = trax_buffer.alloc_fail_cntr;

	trax_diag_stats.total_bytes_written = 0;
	trax_diag_stats.total_bytes_requested = 0;
	trax_diag_stats.total_frames = 0;
	trax_diag_stats.write_fail_count = 0;
	trax_diag_stats.backpressure_count = 0;
	trax_diag_stats.peak_frame_size = 0;
	trax_diag_stats.peak_trax_buf_used = 0;
	trax_diag_stats.peak_transport_buf_used = 0;
	trax_diag_stats.peak_transport_write_size = 0;
	trax_diag_stats.at_peak_tick_overflow = 0;
	trax_diag_stats.trax_buf_used = 0;
	trax_diag_stats.transport_buf_used = 0;
	trax_diag_stats.write_rate_bps = 0;
	trax_diag_stats.frame_corrupt_count = 0;
	trax_diag_stats.ring_buf_overflow_count = 0;
	trax_diag_stats.last_event = 0xFFFFFFFFu; /* sentinel: no event yet */
	trax_diag_stats.last_event_info = 0;
	trax_diag_stats.last_event_at_tick_overflow = 0;

	/* Static capacities — configuration, not counters, but re-stamped on
	 * every reset because the block above zeroed the whole struct's
	 * runtime fields and these ship in the same wire snapshot. The host
	 * renders buffer fill as "used / capacity" from these. */
	trax_diag_stats.trax_buf_size =
		(uint32_t)(TRAX_CFG_OUT_BUFFER_SIZE32 * sizeof(uint32_t));
	trax_diag_stats.transport_buf_size =
		(uint32_t)TRAX_TRANSPORT_BUF_CAPACITY();

	prev_tick_overflow = trax_timebase.tick_overflow_cntr;
	prev_bytes = 0;

	/* Snapshot dispatch state: zero seq so the host detects "session
	 * restarted" via the abrupt seq jump back to 1. Seed last_emit at
	 * "current minus one period" so the *first* trax_diag_process()
	 * call after reset emits a baseline snapshot immediately rather
	 * than waiting a full period. */
	report_seq         = 0;
	last_emit_overflow = trax_timebase.tick_overflow_cntr -
	                     DIAG_PERIOD_OVERFLOW_DELTA;

	/* Re-arm all threshold hysteresis flags so they can fire again after reset.
	 * Threshold limits and active flags are preserved — they are configuration. */
	for (i = 0; i < (uint8_t)TRAX_DIAG_METRIC_COUNT; i++) {
		thresholds[i].triggered = false;
	}
}

int trax_diag_subscribe(uint32_t event_mask, trax_diag_callback_t callback)
{
	uint8_t i;

	if (callback == (trax_diag_callback_t)0) {
		return -1;
	}

	/* Update existing entry if the same callback re-subscribes */
	for (i = 0; i < (uint8_t)TRAX_CFG_DIAG_MAX_SUBSCRIBERS; i++) {
		if (subscribers[i].callback == callback) {
			subscribers[i].mask = event_mask;
			return 0;
		}
	}

	/* Add new entry in the first empty slot */
	for (i = 0; i < (uint8_t)TRAX_CFG_DIAG_MAX_SUBSCRIBERS; i++) {
		if (subscribers[i].callback == (trax_diag_callback_t)0) {
			subscribers[i].callback = callback;
			subscribers[i].mask = event_mask;
			return 0;
		}
	}

	return -1; /* Table full */
}

void trax_diag_unsubscribe(trax_diag_callback_t callback)
{
	uint8_t i;
	for (i = 0; i < (uint8_t)TRAX_CFG_DIAG_MAX_SUBSCRIBERS; i++) {
		if (subscribers[i].callback == callback) {
			subscribers[i].callback = (trax_diag_callback_t)0;
			subscribers[i].mask = 0;
			return;
		}
	}
}

void trax_diag_set_threshold(enum trax_diag_metric_t metric, uint32_t limit)
{
	if ((uint8_t)metric < (uint8_t)TRAX_DIAG_METRIC_COUNT) {
		thresholds[(uint8_t)metric].limit = limit;
		thresholds[(uint8_t)metric].active = true;
		thresholds[(uint8_t)metric].triggered = false;
	}
}

void trax_diag_clear_threshold(enum trax_diag_metric_t metric)
{
	if ((uint8_t)metric < (uint8_t)TRAX_DIAG_METRIC_COUNT) {
		thresholds[(uint8_t)metric].active    = false;
		thresholds[(uint8_t)metric].triggered = false;
	}
}

void trax_diag_rearm_threshold(enum trax_diag_metric_t metric)
{
	if ((uint8_t)metric < (uint8_t)TRAX_DIAG_METRIC_COUNT) {
		thresholds[(uint8_t)metric].triggered = false;
	}
}

void trax_diag_get_stats(struct trax_diag_stats_t *out)
{
	/* Copy under a critical section so the caller receives a consistent
	 * snapshot rather than a mix of old and new field values from a
	 * concurrent trax_diag_record_write() or trax_diag_process() call. */
	TRAX_PORT_ENTER_CRITICAL_SECTION
			{
				memcpy(out, &trax_diag_stats, sizeof(*out));
			}
			TRAX_PORT_EXIT_CRITICAL_SECTION
}

void trax_diag_build_report(struct trax_diag_report_t *p_out, uint32_t flags)
{
	/* Single source of truth for "stats -> wire layout".  Both the
	 * normal periodic / immediate emit path (trax_diag_send_report) and
	 * the embedded-autopsy path (trax_meta_tx_send_stop) call this so
	 * the wire shape can never drift between the two emitters.
	 *
	 * Critical section spans the whole copy AND the seq-counter bump:
	 *   - copy must be atomic vs concurrent record_write / diag_process
	 *     (otherwise the host sees a torn snapshot — half pre-event,
	 *     half post-event values).
	 *   - report_seq must be bumped exactly once per emit so the host's
	 *     gap-detection on snapshot_seq stays monotonic.  Doing the
	 *     bump under the same lock as the copy guarantees that two
	 *     callers racing to emit can never both observe seq=N. */
	TRAX_PORT_ENTER_CRITICAL_SECTION
		{
			p_out->snapshot_seq               = ++report_seq;
			p_out->flags                      = flags;
			p_out->total_bytes_written        = trax_diag_stats.total_bytes_written;
			p_out->total_bytes_requested      = trax_diag_stats.total_bytes_requested;
			p_out->total_frames               = trax_diag_stats.total_frames;
			p_out->write_fail_count           = trax_diag_stats.write_fail_count;
			p_out->backpressure_count         = trax_diag_stats.backpressure_count;
			p_out->peak_frame_size            = trax_diag_stats.peak_frame_size;
			p_out->peak_trax_buf_used         = trax_diag_stats.peak_trax_buf_used;
			p_out->peak_transport_buf_used    = trax_diag_stats.peak_transport_buf_used;
			p_out->peak_transport_write_size  = trax_diag_stats.peak_transport_write_size;
			p_out->at_peak_tick_overflow      = trax_diag_stats.at_peak_tick_overflow;
			p_out->trax_buf_used              = trax_diag_stats.trax_buf_used;
			p_out->transport_buf_used         = trax_diag_stats.transport_buf_used;
			p_out->write_rate_bps             = trax_diag_stats.write_rate_bps;
			p_out->frame_corrupt_count        = trax_diag_stats.frame_corrupt_count;
			p_out->ring_buf_overflow_count    = trax_diag_stats.ring_buf_overflow_count;
			p_out->last_event                 = trax_diag_stats.last_event;
			p_out->last_event_info            = trax_diag_stats.last_event_info;
			p_out->last_event_at_tick_overflow = trax_diag_stats.last_event_at_tick_overflow;
			p_out->trax_buf_size              = trax_diag_stats.trax_buf_size;
			p_out->transport_buf_size         = trax_diag_stats.transport_buf_size;
		}
	TRAX_PORT_EXIT_CRITICAL_SECTION
}

void trax_diag_report(enum trax_diag_event_t event, uint32_t info)
{
	uint32_t bit = (1u << (uint8_t)event);
	uint8_t i;

	/* Snapshot the latest event so it is always visible in the debugger
	 * via trax_diag_stats, even if no subscriber is currently registered. */
	trax_diag_stats.last_event                  = (uint32_t)event;
	trax_diag_stats.last_event_info             = info;
	trax_diag_stats.last_event_at_tick_overflow = trax_timebase.tick_overflow_cntr;

	/* Always-on per-event counters (mirror events that have no other
	 * dedicated counter in trax_diag_stats). */
	switch (event) {
	case TRAX_DIAG_FRAME_CORRUPT:
		trax_diag_stats.frame_corrupt_count++;
		break;
	case TRAX_DIAG_BUFFER_OVERFLOW:
		/* info = number of new alloc failures since last report.
		 * Accumulate so the running total is visible in trax_diag_stats. */
		trax_diag_stats.ring_buf_overflow_count += info;
		break;
	default:
		break;
	}

	/* Default logs (Layer 4). Emit *before* the subscriber loop so the
	 * line is visible even when nobody is subscribed. emit_default_log()
	 * carries its own re-entrancy guard. */
#if (TRAX_CFG_DIAG_DEFAULT_LOGS)
	emit_default_log(event, info);
#endif

	for (i = 0; i < (uint8_t)TRAX_CFG_DIAG_MAX_SUBSCRIBERS; i++) {
		if ((subscribers[i].callback != (trax_diag_callback_t)0) &&
			(subscribers[i].mask & bit)) {
			subscribers[i].callback(event, info);
		}
	}

	/* Stream-critical events bypass the periodic gate so the operator
	 * sees the snapshot within microseconds of the failure rather than
	 * up to TRAX_CFG_DIAG_REPORT_PERIOD_MS later. Soft events (peak
	 * updates, near-full warnings, periodic stats) ride along with the
	 * next periodic snapshot — they're not time-critical and shipping
	 * them inline would re-introduce the wire-flooding problem the
	 * cadence model is meant to solve.
	 *
	 * Note: stream-stop on fatal errors is owned by trax_session's
	 * pending-stop latch (see trax_session.h) — armed from the
	 * alloc-fail callback, validator branch in trax_send_frames, and
	 * transport hard-error branch, then drained by trax_process().
	 * The diag layer's job here is to ship the snapshot ASAP so the
	 * host has fresh autopsy material when the STOP frame lands. */
	if (is_stream_critical(event)) {
		trax_diag_send_report(TRAX_DIAG_RPT_FLAG_EVENT_FIRED,
		                      /*force=*/true);
	}
}

void trax_diag_record_write(size_t requested, size_t written,
	size_t transport_buf_before)
{
	/* Hot-path: just update counters. No frame allocation, no dirty
	 * tracking. The next periodic snapshot will carry whatever the
	 * counters look like at that moment. */
	trax_diag_stats.total_bytes_requested += (uint32_t)requested;
	trax_diag_stats.total_bytes_written += (uint32_t)written;
	trax_diag_stats.total_frames++;

	if (written < requested) {
		trax_diag_stats.write_fail_count++;
	}

	if ((uint32_t)requested > trax_diag_stats.peak_frame_size) {
		trax_diag_stats.peak_frame_size = (uint32_t)requested;
	}

	/* Transport buffer peak: bytes already queued + bytes just pushed */
	uint32_t projected = (uint32_t)transport_buf_before + (uint32_t)written;
	if (projected > trax_diag_stats.peak_transport_buf_used) {
		trax_diag_stats.peak_transport_buf_used = projected;
		trax_diag_stats.peak_transport_write_size = (uint32_t)requested;
		trax_diag_stats.at_peak_tick_overflow = trax_timebase.tick_overflow_cntr;
		/* TRANSPORT_BUF_PEAK is a *soft* event — fires often during
		 * bursts. Subscribers still get notified, but the snapshot
		 * waits for the next periodic tick (no immediate emit). */
		trax_diag_report(TRAX_DIAG_TRANSPORT_BUF_PEAK, projected);
	}
}

void trax_diag_record_backpressure(size_t deferred_bytes)
{
	trax_diag_stats.backpressure_count++;
	/* Backpressure IS stream-critical — handled by the immediate-emit
	 * path inside trax_diag_report() (no extra logic needed here). */
	trax_diag_report(TRAX_DIAG_TRANSPORT_BACKPRESSURE,
		(uint32_t)deferred_bytes);
}

void trax_diag_process(void)
{
	/* ── 1. Snapshot ring buffer usage (always, before frames are drained) ──
	 * Computed from pointer arithmetic — allocated but not yet drained. */
	const uint32_t *p_alloc = trax_buffer.p_alloc32;
	const uint32_t *p_rd = trax_buffer.p_rd32;
	uint32_t used32;
	if (p_alloc >= p_rd) {
		used32 = (uint32_t)(p_alloc - p_rd);
	} else {
		/* Wrapped: top segment [p_rd .. p_end32) + bottom segment [p_start32 .. p_alloc) */
		used32 = (uint32_t)(trax_buffer.p_end32  - p_rd)
		       + (uint32_t)(p_alloc - trax_buffer.p_start32);
	}
	uint32_t trax_now = used32 * (uint32_t)sizeof(uint32_t);
	trax_diag_stats.trax_buf_used = trax_now;
	if (trax_now > trax_diag_stats.peak_trax_buf_used) {
		trax_diag_stats.peak_trax_buf_used = trax_now;
	}

	/* ── 2. Detect new ring buffer allocation failures (always) ── */
	uint32_t current = trax_buffer.alloc_fail_cntr;
	if (current != last_alloc_fail_cntr) {
		uint32_t new_failures = current - last_alloc_fail_cntr;
		last_alloc_fail_cntr = current;
		trax_diag_report(TRAX_DIAG_BUFFER_OVERFLOW, new_failures);
	}

	/* ── 3. Evaluate thresholds (always — responsive, cheap comparison) ── */
	check_thresholds();

	/* ── 4. Rate-gated section (~1 Hz): throughput + transport stats ──
	 * Refactored from an early-return into a conditional block so the
	 * on-change report dispatch in section 5 always runs — otherwise
	 * snapshots queued by sections 1-3 would only ship on tick boundaries.
	 *
	 * Use tick_overflow_cntr as a coarse clock. Each overflow represents
	 * 2^32 / TIMER_FREQ seconds. For typical configs (168MHz, 8-bit tick),
	 * each overflow ≈ 25.6s. For 1kHz SysTick with 8-bit tick, each
	 * overflow ≈ 0.256s, so ~4 overflows ≈ 1 second.
	 * We approximate 1s using the TRAX_CFG_TICK_OVERFLOW_PERIOD. */
	uint32_t curr_overflow = trax_timebase.tick_overflow_cntr;
	if (curr_overflow != prev_tick_overflow) {
		uint32_t overflow_delta = curr_overflow - prev_tick_overflow;
		uint32_t curr_bytes = trax_diag_stats.total_bytes_written;
		uint32_t delta_bytes = curr_bytes - prev_bytes;

		/* bps = (delta_bytes * 8) / (overflow_delta * TICK_OVERFLOW_PERIOD / TICK_RATE_HZ)
		 *     = (delta_bytes * 8 * TICK_RATE_HZ) / (overflow_delta * TICK_OVERFLOW_PERIOD) */
#if defined(TRAX_CFG_TICK_RATE_HZ) && (TRAX_CFG_TICK_RATE_HZ > 0)
		if (overflow_delta > 0u) {
			trax_diag_stats.write_rate_bps =
				(delta_bytes * 8u * (uint32_t)TRAX_CFG_TICK_RATE_HZ)
				/ (overflow_delta * (uint32_t)TRAX_CFG_TICK_OVERFLOW_PERIOD);
		}
#else
		/* Fallback: TRAX_CFG_TICK_RATE_HZ could not be derived — this
		 * normally means TRAX_CFG_TIMER_FREQ_HZ is not defined in
		 * trax_config.h.  The formula degenerates to "1 overflow ≈ 1 s",
		 * which is only accurate when TICK_OVERFLOW_PERIOD equals the
		 * tick rate in Hz — an unlikely coincidence.  Define
		 * TRAX_CFG_TIMER_FREQ_HZ in trax_config.h to get a correct rate. */
		if (overflow_delta > 0u) {
			trax_diag_stats.write_rate_bps = (delta_bytes * 8u) / overflow_delta;
		}
#endif

		prev_tick_overflow = curr_overflow;
		prev_bytes = curr_bytes;

		trax_diag_stats.transport_buf_used =
			(uint32_t)TRAX_TRANSPORT_BYTES_USED();

		/* TRANSPORT_STATS is a *soft* event — we ship it to subscribers
		 * but rely on the periodic snapshot below (not an immediate
		 * emit) to push the new rate to the host. */
		trax_diag_report(TRAX_DIAG_TRANSPORT_STATS,
			trax_diag_stats.write_rate_bps);
	}

	/* ── 5. Periodic snapshot dispatch (rate-limited by period gate) ──
	 * Ships at most once per TRAX_CFG_DIAG_REPORT_PERIOD_MS regardless
	 * of how often trax_diag_process() is called. Carries the full
	 * trax_diag_stats payload — the host's "live device state" panel
	 * is updated from this stream. flags=0 (or EVENT_FIRED if a
	 * critical event in this same pass already advanced the gate). */
	trax_diag_send_report(/*flags=*/0u, /*force=*/false);
}

/*=============================================================================
 ====================LOCAL FUNCTION IMPLEMANTATION=============================
 ============================================================================*/

static uint32_t get_metric(enum trax_diag_metric_t m)
{
	switch (m) {
	case TRAX_DIAG_METRIC_RING_BUF_USED:
		return trax_diag_stats.trax_buf_used;
	case TRAX_DIAG_METRIC_TRANSPORT_BUF_USED:
		return trax_diag_stats.transport_buf_used;
		case TRAX_DIAG_METRIC_WRITE_FAIL_COUNT:
			return trax_diag_stats.write_fail_count;
		default:
		return 0;
	}
}

static void check_thresholds(void)
{
	uint8_t i;
	for (i = 0; i < (uint8_t)TRAX_DIAG_METRIC_COUNT; i++) {
		if (!thresholds[i].active) {
			continue;
		}

		uint32_t val = get_metric((enum trax_diag_metric_t)i);

		if (val >= thresholds[i].limit) {
			if (!thresholds[i].triggered) {
				thresholds[i].triggered = true;
				trax_diag_report(TRAX_DIAG_THRESHOLD_CROSSED,
					(uint32_t)i);
			}
		} else {
			thresholds[i].triggered = false;
		}
	}
}

/**
 * @brief Identify the curated "stream-critical" subset of diag events.
 *
 * Critical events bypass the periodic gate in trax_diag_send_report()
 * because their value to the operator decays rapidly with latency:
 * a buffer-overflow notification arriving 1 second late is much less
 * useful than one arriving immediately. Soft events ride the next
 * periodic snapshot instead.
 */
static bool is_stream_critical(enum trax_diag_event_t event)
{
	switch (event) {
	case TRAX_DIAG_BUFFER_OVERFLOW:
	case TRAX_DIAG_FRAME_CORRUPT:
	case TRAX_DIAG_THRESHOLD_CROSSED:
	case TRAX_DIAG_TRANSPORT_BACKPRESSURE:
		return true;
	default:
		return false;
	}
}

/**
 * @brief Push a TRAX_TID_DIAG_REPORT frame to the host.
 *
 * Two callers:
 *
 *   - trax_diag_process() with force=false  → periodic 1 Hz snapshot.
 *     Gated on (now - last_emit) >= DIAG_PERIOD_OVERFLOW_DELTA. Skipped
 *     silently if the period hasn't elapsed.
 *
 *   - trax_diag_report() with force=true    → immediate event report,
 *     fired only for stream-critical events. Bypasses the period gate.
 *     Advances last_emit so the next periodic call won't double-send
 *     within the same period window.
 *
 * Allocation may fail if the ring buffer is full — that is fine, and in
 * fact reporting "I am too busy to even send my own diagnostics" is
 * itself diagnostic: the host sees no new snapshot, the *next* call to
 * trax_diag_process() will see that BUFFER_OVERFLOW counter has ticked
 * (via the alloc-fail-cntr delta in section 2), and the *following*
 * snapshot — once buffer pressure eases — will carry the bumped count.
 *
 * Re-entrancy: a critical event fired from inside this function (e.g.
 * BUFFER_OVERFLOW reported by the allocator's failure path) would call
 * trax_diag_report() → trax_diag_send_report(force=true) → recurse.
 * The static `in_send_report` flag breaks the cycle.
 */
static void trax_diag_send_report(uint32_t flags, bool force)
{
	static bool in_send_report;

	if (in_send_report) {
		return;          /* recursive call from within our own emit path */
	}

	/* Period gate (only honored on non-forced calls). Use unsigned
	 * subtraction so it works correctly across tick_overflow_cntr
	 * wraparound. */
	uint32_t now_overflow = trax_timebase.tick_overflow_cntr;
	if (!force) {
		if ((now_overflow - last_emit_overflow) <
		    DIAG_PERIOD_OVERFLOW_DELTA) {
			return;      /* still inside the current period window */
		}
	}

	/* Don't bother building a frame when streaming is paused / stopped —
	 * the allocator would accept the frame, but no host would consume
	 * it and we'd just bloat the ring. */
	if (!TRAX_IS_SESSION_ACTIVE()) {
		return;
	}

	in_send_report = true;

	/* Frame layout (matches trax_meta_tx_send_start):
	 *   Word 0       : trans_cntr + frame_size32 (written last as commit)
	 *   Word 1       : timestamp (TIMEPACKED)
	 *   Word 2       : TRAX_TID_DIAG_REPORT
	 *   Words 3..22  : sizeof(trax_diag_report_t) / 4 = 20 words */
	const uint16_t payload_words = (uint16_t)(
		(sizeof(struct trax_diag_report_t) + 3u) / 4u);
	const uint16_t frame_size32  = payload_words + TRAX_CFG_FRAME_MIN_SIZE32;

	uint32_t *p_frame_start = NULL;
	uint32_t *p_wr;
	uint16_t  saved_trans_cntr;

	TRAX_PORT_ENTER_CRITICAL_SECTION
		{
			TRAX_BUFF_ALLOC(p_wr, frame_size32);
			if (p_wr != NULL) {
				p_frame_start = p_wr;
				saved_trans_cntr = trax_trans_cntr;
				trax_trans_cntr++;
				p_wr++; /* Word 0 written during commit */
				TRAX_FRAME_TIMEPACKED_PUT(p_wr);
				TRAX_PUT32(p_wr, TRAX_TID_DIAG_REPORT);
			}
		}
	TRAX_PORT_EXIT_CRITICAL_SECTION

	if (p_frame_start == NULL) {
		/* Allocation failed. Don't advance last_emit so the next
		 * trax_diag_process() call re-attempts immediately (still
		 * subject to the period gate, which has *already* elapsed for
		 * us to even reach this point). */
		in_send_report = false;
		return;
	}

	/* Build the payload on the stack then memcpy into the reserved frame
	 * slot. Stack version means we can populate fields in any order
	 * without worrying about cache-line / write-buffer interaction with
	 * the ring buffer. 80 bytes — fits fine on any target stack.
	 *
	 * trax_diag_build_report() is the single source of truth for the
	 * stats -> wire-layout copy + seq bump; the autopsy path in
	 * trax_meta_tx_send_stop() calls the same helper so wire shape and
	 * seq monotonicity stay identical between the two emitters. */
	struct trax_diag_report_t payload;
	trax_diag_build_report(&payload, flags);

	memcpy(p_wr, &payload, sizeof(payload));

	/* Commit the frame — Word 0 written last as the atomic visibility
	 * marker for the consumer. Same pattern as trax_meta_tx_send_start. */
	TRAX_PORT_ENTER_CRITICAL_SECTION
		{
			TRAX_PORT_COMMIT_BARRIER(); /* SMP: body visible before marker */
			*p_frame_start = ((uint32_t)frame_size32 << 16U) | saved_trans_cntr;
		}
	TRAX_PORT_EXIT_CRITICAL_SECTION

	/* Successful emit — advance the period gate so the next periodic
	 * call waits a full period from THIS point (not from the previous
	 * scheduled tick). Especially important for force=true paths so a
	 * critical event doesn't get immediately re-shadowed by a periodic
	 * snapshot showing the same data. */
	last_emit_overflow = now_overflow;
	in_send_report     = false;
}

#if (TRAX_CFG_DIAG_DEFAULT_LOGS)
/**
 * @brief Emit a TRAX_LOG line for the curated default-log event set.
 *
 * Called from trax_diag_report() before the subscriber loop. Maps each
 * critical event to a dedicated TRAX_TID_LOG_DIAG_* with a tailored format
 * string and severity baked into the per-TID metadata.
 *
 * Re-entrancy guard:
 *   TRAX_LOG_BASE allocates a frame from trax_buffer. If the ring is
 *   full, that allocation fails and trax_diag_process() will report
 *   TRAX_DIAG_BUFFER_OVERFLOW on the next tick — which would call us
 *   again. The static in_default_log flag breaks the cycle: a nested
 *   call sees the flag set and returns immediately. The outer call
 *   was already counted in trax_diag_stats.ring_buf_overflow_count by
 *   trax_diag_report(), so no information is lost — only the
 *   amplifying log frame is suppressed.
 *
 * Single-threaded only: dispatch happens from trax_process() context
 * (never ISR) so a plain static bool is sufficient — no atomic ops
 * or critical section required.
 *
 * Events not in the curated set silently fall through (default branch).
 * Subscribe via trax_diag_subscribe() to receive them.
 */
static void emit_default_log(enum trax_diag_event_t event, uint32_t info)
{
	static bool in_default_log;

	if (in_default_log) {
		return;
	}
	in_default_log = true;

	switch (event) {
	case TRAX_DIAG_BUFFER_OVERFLOW:
		TRAX_LOG_ERROR(TRAX_TID_LOG_DIAG_OVERFLOW, "diag",
			"Ring buffer overflow: %u new alloc failures (total %u)",
			(unsigned)info,
			(unsigned)trax_diag_stats.ring_buf_overflow_count);
		break;

	case TRAX_DIAG_FRAME_CORRUPT:
		TRAX_LOG_ERROR(TRAX_TID_LOG_DIAG_FRAME_CORRUPT, "diag",
			"Frame validation failed (total corrupt frames: %u)",
			(unsigned)trax_diag_stats.frame_corrupt_count);
		break;

	case TRAX_DIAG_THRESHOLD_CROSSED:
		/* info = trax_diag_metric_t id that crossed its limit */
		TRAX_LOG_WARNING(TRAX_TID_LOG_DIAG_THRESHOLD, "diag",
			"Threshold crossed: metric=%u value=%u limit=%u",
			(unsigned)info,
			(unsigned)get_metric((enum trax_diag_metric_t)info),
			(unsigned)((info < (uint32_t)TRAX_DIAG_METRIC_COUNT)
				   ? thresholds[info].limit : 0u));
		break;

	case TRAX_DIAG_TRANSPORT_BACKPRESSURE:
		TRAX_LOG_WARNING(TRAX_TID_LOG_DIAG_BACKPRESSURE, "diag",
			"Transport backpressure: %u bytes deferred (total events: %u)",
			(unsigned)info,
			(unsigned)trax_diag_stats.backpressure_count);
		break;

	default:
		/* Verbose / informational events — opt-in via subscription only */
		break;
	}

	in_default_log = false;
}
#endif /* TRAX_CFG_DIAG_DEFAULT_LOGS */

#endif /* TRAX_ENABLE */
