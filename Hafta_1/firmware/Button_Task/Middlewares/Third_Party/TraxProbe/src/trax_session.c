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
 * @file           : trax_session.c
 * @brief          : TraxProbe Session State Management
 * @version        : 6.0.0
 ******************************************************************************
 * @attention
 *
 * Boolean flag model: the probe is always tracing after init.
 * Only the stream to the host is toggled via streaming flag.
 *
 ******************************************************************************
 */

#include "trax_session.h"
#include "trax_buffer.h"     /* For trax_buffer_clear(), alloc_fail_cntr */
#include "trax_timestamp.h"  /* For TRAX_FRAME_TIMEPACKED_GET(), trax_timebase */
#include "trax_hw.h"         /* For TRAX_PORT_ENTER/EXIT_CRITICAL_SECTION */

/* TraxProbe disabled (TRAX_ENABLE==0): whole unit compiles out. Session entry
 * points are not referenced once the public API is no-op'd. */
#if TRAX_ENABLE

/*=============================================================================
 ====================GLOBAL VARIABLES==========================================
 ============================================================================*/

/**
 * @brief Global session state instance
 */
struct trax_session_t trax_session = {0};

/*=============================================================================
 ====================GLOBAL FUNCTION IMPLEMENTATION============================
 ============================================================================*/

void trax_session_init(void)
{
	/* New session: increment ID and clear buffer */
	trax_session.session_id++;
	trax_session.frame_count = 0;
	trax_buffer_clear();

	/* Probe is always tracing after init — streaming is off by default */
	trax_session.streaming = 0;

	/* Drop any pending-stop latch left armed from a previous session.
	 * trax_process() in the new session must not re-emit a STOP frame
	 * for an error that belonged to the previous trax_init() run. */
	trax_session.pending_stop_armed  = 0;
	trax_session.pending_stop_reason = 0;
	trax_session.pending_stop_info   = 0;

	/* Same for a gap latch from the previous run (GAP overflow policy). */
	trax_session.pending_gap_armed = 0;

	/* And for a pause / trigger latch from the previous run. */
	trax_session.pause_hold      = 0;
	trax_session.trigger_latched = 0;
	trax_session.resume_reason   = (uint8_t)TRAX_RESUME_REASON_OVERFLOW;

	/* Mark as initialized */
	trax_session.initialized = 1;
}

void trax_session_enable_streaming(void)
{
	/* Enable stream to host */
	if (!trax_session.streaming) {
		/* Fresh start — make sure no stale fatal-stop reason from a
		 * previous run is still armed. Without this clear,
		 * trax_process() in the new session could fire a phantom
		 * STOP frame for the previous session's overflow. */
		trax_session.pending_stop_armed  = 0;
		trax_session.pending_stop_reason = 0;
		trax_session.pending_stop_info   = 0;

		/* A pending gap resume is consumed (gap-resume path) or
		 * superseded (fresh session start) by streaming coming up —
		 * either way the latch must not survive into the new epoch. */
		trax_session.pending_gap_armed = 0;

		trax_session.streaming = 1;
	}
}

void trax_session_disable_streaming(void)
{
	/* Disable stream to host — probe keeps tracing */
	if (trax_session.streaming) {
		trax_session.streaming = 0;
	}
}

void trax_session_arm_stop(uint8_t reason, uint32_t info)
{
	/* First-wins latch: an already-armed slot keeps the original
	 * cause. Cheap unsynchronised early-exit — see header for the
	 * benign-race rationale. */
	if (trax_session.pending_stop_armed) {
		return;
	}

	/* Order matters: payload first, then the armed flag. The consumer
	 * reads armed first; until that byte is set, it never observes
	 * reason / info, so a half-built record cannot leak out. */
	trax_session.pending_stop_info   = info;
	trax_session.pending_stop_reason = reason;
	trax_session.pending_stop_armed  = 1;
}

bool trax_session_check_stop(uint8_t *p_reason, uint32_t *p_info)
{
	if (!trax_session.pending_stop_armed) {
		return false;
	}

	*p_reason = trax_session.pending_stop_reason;
	*p_info   = trax_session.pending_stop_info;

	/* Clear the armed flag last so a producer racing in cannot see
	 * "armed but cleared payload" — though in practice the only
	 * caller is trax_process() and the only producers are hot-path
	 * sites that re-check armed before writing, so this race is
	 * mostly cosmetic. */
	trax_session.pending_stop_armed = 0;
	return true;
}

void trax_session_arm_gap(void)
{
	/* First-wins: only the FIRST failed alloc of an episode captures
	 * the gap start anchor. Later rejected allocs during the same gate
	 * exit here — one volatile load + branch on an already-cold path. */
	if (trax_session.pending_gap_armed) {
		return;
	}

	/* Baseline BEFORE this failure: the caller (trax_buffer_on_alloc_fail)
	 * has already bumped alloc_fail_cntr for the frame that just died,
	 * and that frame IS part of the gap — so back the snapshot off by
	 * one. Resume reports (current - baseline) dropped frames. */
	trax_session.gap_start_fail_cntr = trax_buffer.alloc_fail_cntr - 1u;

	/* Skip-counter baseline: no adjustment needed — the frame that just
	 * failed is counted via alloc_fail_cntr above, and gate skips only
	 * start once streaming==0 (already flipped by our caller, inside
	 * this same critical section, so no skip can land before this
	 * snapshot on a single core). */
	trax_session.gap_start_skip_cntr = trax_session.gap_skip_cntr;

	/* Time anchor for the host's gap rendering. We run inside the
	 * failing producer's critical section, so reading the timebase +
	 * timer register here is as coherent as any frame timestamp. */
	trax_session.gap_start_tick_overflow = trax_timebase.tick_overflow_cntr;
	trax_session.gap_start_timepacked    = TRAX_FRAME_TIMEPACKED_GET();

	/* Armed byte last — the consumer (trax_process) reads it first,
	 * so it can never observe a half-built record. Same ordering
	 * contract as trax_session_arm_stop(). */
	trax_session.pending_gap_armed = 1;
}

void trax_session_clear_gap(void)
{
	trax_session.pending_gap_armed = 0;
	/* A stop / fresh start wins over a held pause and any latched
	 * trigger — the resync they were waiting for will never be sent. */
	trax_session.pause_hold      = 0;
	trax_session.trigger_latched = 0;
	trax_session.resume_reason   = (uint8_t)TRAX_RESUME_REASON_OVERFLOW;
}

void trax_session_arm_pause(void)
{
	TRAX_PORT_ENTER_CRITICAL_SECTION {
		/* Only a live stream can be paused: a stream already gated by
		 * an overflow episode keeps its automatic resume, and a
		 * stopped session has nothing to pause. */
		if (trax_session.streaming) {
			trax_session.streaming = 0;

			/* Same anchor capture as trax_session_arm_gap(), but the
			 * baseline is NOT backed off by one: no allocation failed
			 * here, so the episode starts with zero dropped frames. */
			trax_session.gap_start_fail_cntr     = trax_buffer.alloc_fail_cntr;
			trax_session.gap_start_skip_cntr     = trax_session.gap_skip_cntr;
			trax_session.gap_start_tick_overflow = trax_timebase.tick_overflow_cntr;
			trax_session.gap_start_timepacked    = TRAX_FRAME_TIMEPACKED_GET();

			trax_session.resume_reason   = (uint8_t)TRAX_RESUME_REASON_USER;
			trax_session.trigger_latched = 0;
			trax_session.pending_gap_armed = 1;
			/* Hold byte last: trax_process() tests it after the armed
			 * byte, so a half-built pause can never auto-resume. */
			trax_session.pause_hold = 1;
		}
	}
	TRAX_PORT_EXIT_CRITICAL_SECTION
}

void trax_session_release_pause(uint8_t reason)
{
	if (!trax_session.pause_hold) {
		return;
	}
	trax_session.resume_reason = reason;
	trax_session.pause_hold    = 0;
}

void trax_session_latch_trigger(uint16_t tid, uint32_t ctx)
{
	/* Cheap unsynchronised early-exit: not paused (trigger while live
	 * is just its event frame) or a trigger already won this episode. */
	if (!trax_session.pause_hold || trax_session.trigger_latched) {
		return;
	}

	TRAX_PORT_ENTER_CRITICAL_SECTION {
		if (trax_session.pause_hold && !trax_session.trigger_latched) {
			trax_session.trigger_tid           = tid;
			trax_session.trigger_ctx           = ctx;
			trax_session.trigger_tick_overflow = trax_timebase.tick_overflow_cntr;
			trax_session.trigger_timepacked    = TRAX_FRAME_TIMEPACKED_GET();
			trax_session.trigger_latched       = 1;
			trax_session.resume_reason = (uint8_t)TRAX_RESUME_REASON_TRIGGER;
			/* Release last — once the hold drops, the next
			 * trax_process() pass may consume the latch. */
			trax_session.pause_hold = 0;
		}
	}
	TRAX_PORT_EXIT_CRITICAL_SECTION
}

#endif /* TRAX_ENABLE */
