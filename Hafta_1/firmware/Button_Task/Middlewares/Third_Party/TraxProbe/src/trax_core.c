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
 * @file           : trax_core.c
 * @brief          : TraxProbe Core Implementation
 * @version        : 2.1.0
 ******************************************************************************
 * @attention
 *
 * Core initialization, frame processing, and consumption logic.
 *
 ******************************************************************************
 */

#include "trax.h"
#include <stdbool.h>
#include <stdint.h>

#include "../debug/trax_frame_validate.h"
#include "internal/trax_cmd_handler.h" /* For trax_cmd_handler_process_pending_start() */
#include "trax_buffer.h"  /* For trax_buffer_init() */
#include "trax_diag.h"    /* For diagnostics (events, stats, recording) */
#include "trax_fault.h"   /* For trax_fault_init() */
#include "trax_frame.h"   /* For trax_frame_init() */
#include "internal/trax_memory.h"  /* For trax_memory_monitor_process() */
#include "trax_meta_tx.h" /* For metadata transmission */
#include "trax_hw.h"
#include "trax_session.h" /* For trax_session_init() */
#include "trax_timestamp.h"
#include "trax_transport.h"  /* For transport initialization */
#include "trax_var.h"        /* For VAR initialization */
#include "trax_stream.h" /* For stream state */
#include "trax_sm.h"     /* For SM current-state shadow table */

#include "../os/common/trax_rtos_tables.h"

#if (TRAX_CFG_RTOS_TYPE == TRAX_RTOS_FREERTOS)
#include "internal/trax_task.h"
#endif

/* TraxProbe disabled (TRAX_ENABLE==0): this entire translation unit compiles
 * to nothing. The public entry points are provided as inline no-op stubs in
 * trax.h, so nothing here is defined or referenced and the object drops out
 * at link time. */
#if TRAX_ENABLE

/*=============================================================================
 ====================LOCAL MACRO FUNCTIONS=====================================
 ============================================================================*/

/*=============================================================================
 ====================LOCAL MACRO DEFINITIONS===================================
 ============================================================================*/

/*=============================================================================
 ====================GLOBAL VARIABLES==========================================
 ============================================================================*/

/*=============================================================================
 ====================LOCAL STUCTURES===========================================
 ============================================================================*/

/*=============================================================================
 ====================LOCAL VARIABLES===========================================
 ============================================================================*/

/*=============================================================================
 ====================LOCAL FUNCTION DECLARATION================================
 ============================================================================*/

static void   trax_send_frames           (uint32_t frame_limit);
static void   trax_collect_iov           (uint32_t frame_limit,
                                          size_t   byte_budget,
                                          struct trax_iov_t *iov,
                                          uint8_t  *p_seg_count,
                                          uint32_t **pp_rd_local,
                                          uint16_t *p_corrupt_skip32,
                                          uint32_t *p_frames_collected);
static void   trax_iov_push              (struct trax_iov_t *iov,
                                          uint8_t *p_seg_count,
                                          const void *p_base,
                                          size_t len);
static size_t trax_iov_total             (const struct trax_iov_t *iov,
                                          uint8_t seg_count);
static void   trax_arm_stop_corrupt      (void);
static void   trax_arm_stop_transport_fail(size_t requested, size_t written);
static void   trax_commit_read           (uint32_t *p_rd_local,
                                          uint16_t corrupt_skip32,
                                          uint32_t frames_consumed);

/*=============================================================================
 ====================GLOBAL FUNCTION IMPLEMANTATION============================
 ============================================================================*/

int trax_init(void)
{
	int ret = 0;

	/* Initialize modules in dependency order */
	trax_buffer_init(); /* Buffer module FIRST: sets up p_start32 et al. that
	                       trax_session_init()->trax_buffer_clear() dereferences.
	                       (On Cortex-M a premature *p_start32 write to NULL is
	                       silently absorbed by flash-aliased address 0; on the
	                       ESP32-S3 address 0 is unmapped and faults.) */
	trax_session_init(); /* Session state (clears the now-initialized buffer) */
	trax_timestamp_init(); /* Time tracking module (self-contained) */
	trax_frame_init(); /* Frame module (transaction counter) */
	trax_frame_validate_init(); /* Frame validation module */
	trax_stream_init(); /* Stream state management */
	trax_sm_init(); /* SM current-state shadow table */
	trax_var_init(); /* VAR subsystem */
	trax_meta_tx_init(); /* Metadata transmission */
	trax_cmd_protocol_init();

	/* Transport (RTT, UART, USB, etc.). A failure here aborts trax_init:
	 * without a transport binding there is no up-channel to stream on and
	 * no down-channel to accept START from, so bringing up the diag,
	 * fault and OS layers on top of it would only hide the real cause. */
	ret = TRAX_TRANSPORT_INIT();
	if (ret != 0) {
		return ret;
	}

	trax_diag_init(); /* Diagnostic module (events + stream stats) */
	trax_fault_init(); /* TraxFault: recover crash record from previous run,
	                      latch reset reason (no-op stub when
	                      TRAX_CFG_FAULT_ENABLE == 0) */
	trax_os_init();
#if (TRAX_CFG_RTOS_TYPE == TRAX_RTOS_FREERTOS)
  trax_task_init();
#endif

	return ret;
}

int trax_process(void)
{
	/* 1. Process incoming commands from Traxcope (START/STOP/PING) */
	int cmds_processed = trax_cmd_protocol_process();

	/* 1b. Execute a latched START once the ring is quiescent
	 *     (p_rd32 == p_alloc32 — fully drained, no reservation
	 *     outstanding; see the gate comment in trax_cmd_handler.c).
	 *     Until then the ring keeps draining via step 3 below, so the
	 *     gate typically opens on the next pass and the ring is never
	 *     reset under an in-flight writer. */
	trax_cmd_handler_process_pending_start();

	/* 1c. Flight-recorder replay pacer: after a crash, emit at most one
	 *     pre-crash history chunk per tick, gated on ring headroom.
	 *     No-op (single test-and-return) unless a replay is armed. */
	trax_fault_flight_pump();

	/* 2. Diagnostic snapshot + event check BEFORE draining — reads buffer
	 *    pointers, transport buf, and alloc_fail_cntr while they reflect actual
	 * load. */
	trax_diag_process();

	/* 3. Transmit pending trace frames to host. Drains anything written
	 *    *before* a fatal stop was armed — the user must see the
	 *    pre-error frames in chronological order, not after the STOP. */
	trax_send_frames(TRAX_CONSUME_ALL_FRAMES);

	/* 4. Phase 2 of the fatal-stop lifecycle (see trax_session.h):
	 *    if the hot path armed a pending stop (frame corruption,
	 *    transport hard-error) emit a single TRAX_TID_SESSION_STOP
	 *    on the normal pipeline. trax_meta_tx_send_stop uses
	 *    TRAX_FRAME_ARGS_PROTOCOL which bypasses the TRAX_IS_SESSION_ACTIVE
	 *    gate, so the frame lands at p_wr32 even though streaming is
	 *    already off. The next trax_process() drains it.
	 *
	 *    Doing this AFTER trax_send_frames guarantees that:
	 *      - all pre-error frames are flushed first (preserves order),
	 *      - the STOP is the unambiguous last frame on the wire,
	 *      - the trans counter sequence stays contiguous (no gap
	 *        warning on the host),
	 *      - if backpressure delays the STOP, we just retry it via
	 *        the latch on the next tick rather than losing it.
	 *
	 *    The STOP frame body always carries an 80-byte diag autopsy
	 *    snapshot (built by trax_diag_build_report inside
	 *    trax_meta_tx_send_stop) — atomic with the reason word. */
	uint8_t  reason = 0;
	uint32_t info   = 0;
	if (trax_session_check_stop(&reason, &info)) {
		(void)trax_meta_tx_send_stop(
			(enum trax_stop_reason_t)reason, info);
		/* A fatal stop overrides a pending gap resume: the session
		 * is dead, resuming after the STOP frame would contradict
		 * the autopsy the host just received. */
		trax_session_clear_gap();
	}

	/* 4b. Overflow-gap resume: once the overflow gate has fully drained
	 *     the ring, emit the TRAX_TID_SESSION_GAP resync frame and
	 *     re-enable streaming.
	 *
	 *     Quiescence gate is the same proof the pending-start path
	 *     uses: p_rd32 == p_alloc32 means every committed frame is on
	 *     the wire AND no producer is inside an ALLOC..COMMIT window
	 *     (an in-flight reservation pins p_rd32 strictly behind
	 *     p_alloc32). Streaming is gated, so the ring monotonically
	 *     drains toward equality — typically a handful of passes,
	 *     bounded by ring size / transport rate.
	 *
	 *     Draining to EMPTY (not "some space freed") is deliberate:
	 *     it prevents gate/resync thrash under sustained overload and
	 *     guarantees the resync frame always has room. If send_gap
	 *     fails anyway (dynamic tables larger than the ring —
	 *     pathological config), the latch stays armed and we retry
	 *     next tick.
	 *
	 *     Resume is the same on every transport: the application ring
	 *     empty is the only gate. A still-full TX buffer is handled by
	 *     the transport write contract (tx_free / all-or-nothing
	 *     backpressure), not by a second accordion pause. Waiting for
	 *     the UART/RTT ring to drain while producers stay gated idles
	 *     the wire and invents extra SESSION_GAP episodes. */
	/*     PAUSE HOLD: a paused stream (trax_session_pause / trigger
	 *     arming) keeps the gap latch armed but blocks the automatic
	 *     resume — the ring drains fully (everything captured before
	 *     the pause reaches the host) and then waits. The hold is
	 *     released by trax_session_resume() or TRAX_TRIGGER_FIRE(). */
	if (trax_session.pending_gap_armed &&
	    !trax_session.pause_hold &&
	    (trax_buffer.p_rd32 == trax_buffer.p_alloc32)) {
		(void)trax_meta_tx_send_gap();
	}

	/* 5. Periodic memory monitoring (stack watermark polling) */
	trax_memory_monitor_process();

	return cmds_processed;
}

int trax_session_start(void)
{
	/* Send metadata + enable streaming (streaming is enabled internally
	 * after buffer space is reserved, before dynamic metadata is filled). */
	return (trax_meta_tx_send_start() == 0) ? 0 : -1;
}

int trax_session_stop(void)
{
	/* Disable streaming — probe continues tracing.
	 *
	 * pending_gap_armed: after a ring overflow the stream may
	 * currently be GATED (streaming already 0, resume queued). The
	 * user's stop must win over the pending resume — clear the latch
	 * and still ship the STOP frame so the host learns the session
	 * ended rather than waiting for a resync that never comes. */
	if (TRAX_IS_SESSION_ACTIVE() || trax_session.pending_gap_armed) {
		trax_session_clear_gap();
		/* Application-initiated stop via trax_session_stop(). The host
		 * distinguishes this from CMD_SESSION_STOP (which uses
		 * REASON_NORMAL) so it can render "stopped by firmware" vs
		 * "stopped by user" in the autopsy banner. */
		trax_meta_tx_send_stop(TRAX_STOP_REASON_USER, 0u);
		trax_session_disable_streaming(); /* SESSION_STOP will drain on next
		 trax_process() */
	}
	return 0;
}

int trax_session_pause(void)
{
	/* Gate producers and hold the gap resume. Everything captured
	 * before this call still drains to the host; suppressed frames are
	 * counted while paused and reported in the resume resync frame.
	 * No-op unless currently live-streaming (an overflow-gated stream
	 * keeps its automatic resume; a stopped session has nothing to
	 * pause). */
	trax_session_arm_pause();
	return 0;
}

int trax_session_resume(void)
{
	/* Release a held pause; the next trax_process() pass emits the
	 * SESSION_GAP resync (reason = USER) and streaming continues. */
	trax_session_release_pause((uint8_t)TRAX_RESUME_REASON_USER);
	return 0;
}

int trax_set_buffer_alloc_fail_callback(
	trax_buffer_alloc_fail_callback_t callback)
{
	trax_buffer_set_alloc_fail_callback(callback);
	return 0;
}

/*=============================================================================
 ====================LOCAL FUNCTION IMPLEMANTATION=============================
 ============================================================================*/

/**
 * @brief Drain up to @p frame_limit frames from the ring buffer to the
 *        transport, in one all-or-nothing batch.
 *
 * No TRAX_IS_SESSION_ACTIVE() gate here on purpose. After a fatal error arms
 * the pending-stop latch (alloc-fail callback, validator, transport
 * hard-error), trax_session.streaming is already 0 and new producers
 * are locked out at TRAX_FRAME_ALLOC. But anything already buffered
 * before the flip — plus the TRAX_TID_SESSION_STOP frame trax_process()
 * enqueues right after we return — must keep draining until the ring
 * is empty, so the host always sees the pre-error frames followed by
 * the autopsy. Gating here would orphan the autopsy.
 *
 * The shared trax_buffer.p_rd32 is only advanced after the transport
 * accepts the data, so the writer cannot reclaim and overwrite frame
 * data that is still waiting to be DMA'd.
 *
 * BACKPRESSURE (absorbed at collection time): the batch is a single
 * contiguous segment (the collector ends the batch at a wrap marker and
 * caps it at TRAX_CFG_TX_BATCH_MAX_BYTES), and it is additionally sized
 * to TRAX_TRANSPORT_TX_FREE() BEFORE collection — the core never
 * assembles a batch the transport cannot accept, so a saturated
 * transport costs a cheap short walk per pass instead of a collect/
 * reject/retry cycle over the full backlog. Frames that do not fit stay
 * in the ring; the validate-once watermark in trax_frame_validate
 * guarantees they are not re-validated when they are walked again.
 *
 * For transports that cannot report free space (tx_free == SIZE_MAX)
 * the write may still return 0 — soft backpressure: NOTHING is on the
 * wire, keep p_rd32 where it is and retry the batch next pass (again
 * without re-validation). The ring itself bounds a dead transport — if
 * the host never drains, the ring fills and the alloc-fail path gates
 * the stream (pause / drain / TRAX_TID_SESSION_GAP resume).
 *
 * A PARTIAL write (0 < written < requested) violates the all-or-nothing
 * contract and leaves half a frame on the wire — that remains a hard
 * TRANSPORT_FAIL teardown.
 */
static void trax_send_frames(uint32_t frame_limit)
{
	/* One backpressure diag event per episode, not per retry pass —
	 * a full transport is re-tested every trax_process() tick and the
	 * WARNING log + immediate diag snapshot would spam the stream. */
	static bool backpressure_episode = false;

	struct trax_iov_t iov[TRAX_IOV_MAX];
	uint8_t           seg_count        = 0;
	uint32_t         *p_rd_local       = (uint32_t *)0;
	uint16_t          corrupt_skip32   = 0;
	uint32_t          frames_collected = 0;

	/* 1. Size the batch to what the transport can accept RIGHT NOW, then
	 *    walk the ring. Collecting more than the transport's free space
	 *    would only manufacture an all-or-nothing rejection and a retry
	 *    next pass. Transports that cannot report free space — including
	 *    every custom transport that does not opt in via
	 *    TRAX_CFG_TRANSPORT_TX_FREE, where this folds to a compile-time
	 *    constant — yield SIZE_MAX: no pre-cap, write() may reject, we
	 *    retry. */
	size_t tx_free = TRAX_TRANSPORT_TX_FREE();

	trax_collect_iov(frame_limit, tx_free, iov, &seg_count,
	                 &p_rd_local, &corrupt_skip32, &frames_collected);

	/* 2. Arm the FRAME_CORRUPT stop NOW (before the segment writes). The latch
	 *    must be set even if the pre-corrupt batch turns out empty.
	 *    First-wins keeps an earlier armed reason intact. */
	if (corrupt_skip32 != 0) {
		trax_arm_stop_corrupt();
	}

	/* 3. Nothing on the wire — just bookkeeping (skip wrap markers
	 *    and any leading corrupt frame). */
	if (seg_count == 0) {
		trax_commit_read(p_rd_local, corrupt_skip32, frames_collected);
		return;
	}

	/* 4. Hand the batch to the transport. The write contract is
	 *    all-or-nothing per call — see
	 *    .cursor/rules/trax-transport-transmission.mdc. */
	size_t total_bytes = trax_iov_total(iov, seg_count);
	size_t tbefore     = TRAX_TRANSPORT_BYTES_USED();

	size_t written = 0;
	for (uint8_t i = 0; i < seg_count; i++) {
		if (iov[i].p_base == (void *)0 || iov[i].len == 0) {
			continue;
		}
		size_t seg_written = TRAX_TRANSPORT_WRITE(iov[i].p_base, iov[i].len);
		written += seg_written;
		if (seg_written != iov[i].len) {
			break;
		}
	}

	if (written == 0) {
		/* Backpressure — transport buffer temporarily full, nothing on
		 * the wire (only reachable when tx_free reported SIZE_MAX).
		 * Keep p_rd32: the frames stay queued in the ring, and the
		 * validate-once watermark remembers they are already validated,
		 * so the retry walk next pass is cheap. */
		if (!backpressure_episode) {
			backpressure_episode = true;
			trax_diag_record_backpressure(total_bytes);
		}
		return;
	}

	trax_diag_record_write(total_bytes, written, tbefore);

	if (written != total_bytes) {
		/* Partial write — the transport violated the all-or-nothing
		 * contract and a torn frame is on the wire. Do NOT advance
		 * p_rd32 (the post-stop drain re-walks these frames — the
		 * watermark skips their re-validation) and tear the stream
		 * down. */
		trax_arm_stop_transport_fail(total_bytes, written);
		return;
	}

	backpressure_episode = false;

	/* 5. Batch on the wire — commit and (if armed) skip the corrupt frame. */
	trax_commit_read(p_rd_local, corrupt_skip32, frames_collected);
}

/**
 * @brief Walk the ring from p_rd32, collecting ONE contiguous batch of
 *        validated, committed frames.
 *
 * Stops at the first uncommitted frame (Word 0 == 0), at the first
 * corrupt frame (validator reject), after @p frame_limit frames, when
 * the batch reaches @p byte_budget or TRAX_CFG_TX_BATCH_MAX_BYTES, or
 * at a wrap marker.
 *
 * The batch is intentionally a SINGLE contiguous segment:
 *   - A wrap marker ENDS the batch (the post-wrap frames go out on the
 *     next pass). If pre- and post-wrap runs were sent as two transport
 *     writes and the second one back-pressured, the first would already
 *     be on the wire — the retry would duplicate it. One segment per
 *     batch keeps the whole batch all-or-nothing.
 *   - @p byte_budget is the transport's CURRENT free space (or SIZE_MAX
 *     when unknown): a batch that exceeds it would be rejected wholesale
 *     by the all-or-nothing write, so collection stops there and ships
 *     what fits. It applies to the FIRST frame too — if that frame does
 *     not fit, nothing can go out this pass and the walk ends cheaply.
 *   - The TX batch cap bounds per-pass latency (memcpy time in the
 *     transport). A single frame larger than the cap is still collected
 *     — alone in its batch — provided the budget can absorb it.
 *
 * VALIDATE-ONCE: frames below the validator watermark were already
 * validated on an earlier pass over the same ring span (the read pointer
 * has not moved past them since) and are NOT re-validated. This keeps a
 * retry / partial-drain walk O(frames collected), not O(backlog).
 *
 * @param[in]  frame_limit        Max committed frames to walk.
 * @param[in]  byte_budget        Transport TX free space in bytes
 *                                (SIZE_MAX = unknown, no pre-cap).
 * @param[out] iov                Caller-owned, length TRAX_IOV_MAX.
 * @param[out] p_seg_count        Number of segments populated in iov (0 or 1).
 * @param[out] pp_rd_local        Read pointer one past the last collected
 *                                frame (at p_start32 after a wrap marker),
 *                                or AT the corrupt frame if any.
 * @param[out] p_corrupt_skip32   Corrupt frame size in 32-bit words, or 0.
 * @param[out] p_frames_collected Number of frames in the batch.
 */
static void trax_collect_iov(uint32_t           frame_limit,
                             size_t             byte_budget,
                             struct trax_iov_t *iov,
                             uint8_t           *p_seg_count,
                             uint32_t         **pp_rd_local,
                             uint16_t          *p_corrupt_skip32,
                             uint32_t          *p_frames_collected)
{
	uint32_t       *p_rd        = trax_buffer.p_rd32;
	const uint32_t *p_seg_start = p_rd;
	uint8_t         seg_count   = 0;
	uint16_t        corrupt32   = 0;
	size_t          batch_bytes = 0;
	uint32_t        frame_cnt   = 0;

	/* Frames below the watermark passed validation on an earlier walk of
	 * this same ring span — skip re-validating them (see doc block). */
	const uint32_t prevalidated = trax_frame_validate_prevalidated();

	while (frame_limit > 0) {
		/* Volatile load: Word 0 is the commit marker, written
		 * asynchronously by producers in ISR/task context. A plain load
		 * is a data race in the C memory model — under LTO the compiler
		 * may prove "nobody writes this between iterations" and cache
		 * it. The volatile qualifier forces a fresh read each pass;
		 * cost is identical (one LDR either way). */
		uint32_t word0 = *(volatile const uint32_t *)p_rd;

		/* Uncommitted frame (Word 0 still zero) — stop */
		if (word0 == 0) {
			break;
		}

		/* Wrap marker — jump to buffer start. If frames were already
		 * collected, END the batch here (single-segment contract, see
		 * doc block); the post-wrap run goes out on the next pass. */
		if (word0 == TRAX_BUFF_WRAP_MARKER) {
			if (batch_bytes != 0) {
				p_rd = trax_buffer.p_start32; /* resume point */
				break;
			}
			p_rd        = trax_buffer.p_start32;
			p_seg_start = trax_buffer.p_start32;
			continue;
		}

		uint16_t frame_size32 = (uint16_t)(word0 >> 16);
		size_t   frame_bytes  = (size_t)frame_size32 * 4U;

		/* Budget / cap checks — leave the frame for a later pass.
		 * Checked BEFORE validation so a deferred frame is not consumed
		 * by the validator state.
		 *   - Transport budget: applies to the whole batch INCLUDING
		 *     the first frame (a batch over the free space would be
		 *     rejected wholesale by the all-or-nothing write).
		 *   - TX batch cap: bounds per-pass latency. A frame bigger
		 *     than the cap is only taken when the batch is empty
		 *     (alone in its batch). */
		if (batch_bytes + frame_bytes > byte_budget) {
			break;
		}
		if ((batch_bytes != 0) &&
		    (batch_bytes + frame_bytes > (size_t)TRAX_CFG_TX_BATCH_MAX_BYTES)) {
			break;
		}

		/* Validator reject — keep the corrupt frame OUT of the iov.
		 * Caller arms the FRAME_CORRUPT stop and skips past it
		 * after the pre-corrupt batch is safely on the wire. */
		if (frame_cnt >= prevalidated) {
			if (!trax_frame_validate(p_rd, frame_size32)) {
				corrupt32 = frame_size32;
				break;
			}
		}

		p_rd += frame_size32;
		batch_bytes += frame_bytes;
		frame_cnt++;
		frame_limit--;
	}

	/* Watermark: every frame counted in frame_cnt has now passed
	 * validation (either this pass or an earlier one). */
	trax_frame_validate_mark_walked(frame_cnt);

	/* Finalize the batch segment. batch_bytes is the walked span except
	 * after a wrap-break, where p_rd already moved to p_start32 — use
	 * the byte count accumulated frame by frame, not pointer math. */
	trax_iov_push(iov, &seg_count, p_seg_start, batch_bytes);

	*pp_rd_local        = p_rd;
	*p_seg_count        = seg_count;
	*p_corrupt_skip32   = corrupt32;
	*p_frames_collected = frame_cnt;
}

/**
 * @brief Append a segment to the iov array. No-op if @p len is 0 or
 *        the array is already full.
 */
static void trax_iov_push(struct trax_iov_t *iov,
                          uint8_t           *p_seg_count,
                          const void        *p_base,
                          size_t             len)
{
	if (len == 0 || *p_seg_count >= TRAX_IOV_MAX) {
		return;
	}
	iov[*p_seg_count].p_base = p_base;
	iov[*p_seg_count].len    = len;
	(*p_seg_count)++;
}

/**
 * @brief Sum the byte counts of every populated segment.
 */
static size_t trax_iov_total(const struct trax_iov_t *iov, uint8_t seg_count)
{
	size_t total = 0;
	for (uint8_t i = 0; i < seg_count; i++) {
		total += iov[i].len;
	}
	return total;
}

/**
 * @brief Phase 1 of the fatal-stop lifecycle for a frame-corruption event.
 *
 * Records the diag event, gates new producers, and arms the pending-stop
 * latch with FRAME_CORRUPT. trax_process() emits the single
 * TRAX_TID_SESSION_STOP via the normal pipeline after we return — keeping the
 * STOP frame out of the still-being-walked iov and guaranteeing one
 * STOP per session even if the validator hits again next tick.
 *
 * trax_session_arm_stop() is first-wins, so a reason armed earlier by
 * another fault path keeps priority — the host always sees the *first*
 * cause.
 */
static void trax_arm_stop_corrupt(void)
{
	trax_diag_report(TRAX_DIAG_FRAME_CORRUPT, trax_validate_err_total);
	trax_session_disable_streaming();
	trax_session_arm_stop((uint8_t)TRAX_STOP_REASON_FRAME_CORRUPT,
	                      trax_validate_err_total);
}

/**
 * @brief Phase 1 of the fatal-stop lifecycle for a PARTIAL transport write.
 *
 * The transport contract is all-or-nothing (see
 * .cursor/rules/trax-transport-transmission.mdc). A 0 return is soft
 * backpressure handled by the retry path in trax_send_frames; reaching
 * here means the transport committed only PART of the batch — a torn
 * frame is on the wire and recovery is impossible: report the diag
 * event, gate new producers, and arm the pending-stop latch.
 * trax_process() ships a single TRAX_TID_SESSION_STOP
 * via the normal pipeline on its way out — best-effort on a broken
 * transport. If the STOP itself is dropped the host has no automatic
 * liveness fallback (intentional, see Probe.cpp), so the operator must
 * spot the dead stream from the stats panel / absence of frames.
 */
static void trax_arm_stop_transport_fail(size_t requested, size_t written)
{
	uint32_t lost = (uint32_t)(requested - written);
	trax_diag_report(TRAX_DIAG_TRANSPORT_WRITE_FAIL, lost);
	trax_session_disable_streaming();
	trax_session_arm_stop((uint8_t)TRAX_STOP_REASON_TRANSPORT_FAIL, lost);
}

/**
 * @brief Commit the read pointer after a successful flush, optionally
 *        skipping a corrupt frame and re-baselining the validator.
 *
 * Single point of advance for trax_buffer.p_rd32 — the producer side
 * may now reclaim everything we walked over. The validate-once watermark
 * is lowered by the frames consumed so it keeps counting from the new
 * read pointer. If a corrupt frame was deferred, step past it here so
 * the next tick resumes at the frame after it (typically the
 * TRAX_TID_SESSION_STOP that trax_process is about to enqueue at p_wr32);
 * the validator reset also zeroes the watermark.
 */
static void trax_commit_read(uint32_t *p_rd_local, uint16_t corrupt_skip32,
                             uint32_t frames_consumed)
{
	trax_buffer.p_rd32 = p_rd_local;
	trax_frame_validate_consume(frames_consumed);
	if (corrupt_skip32 != 0) {
		trax_buffer.p_rd32 += corrupt_skip32;
		trax_frame_validate_reset();
	}
}

#endif /* TRAX_ENABLE */
