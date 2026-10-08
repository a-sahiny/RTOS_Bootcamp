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
 * @file           : trax_cmd_handler.c
 * @brief          : TraxProbe Command Handler Implementation
 * @version        : 3.0.0
 ******************************************************************************
 * @attention
 *
 * Minimal command set:
 *   - TRAX_CMD_SESSION_START: answered by the SESSION_START frame (implicit ACK)
 *   - TRAX_CMD_SESSION_STOP:  answered by the SESSION_STOP frame (implicit ACK)
 *   - PING: ACK response (utility)
 *
 * No explicit ACK/NAK for START/STOP — the protocol frames themselves
 * serve as acknowledgment. Host waits for SESSION_START / SESSION_STOP frames.
 *
 * All commands are idempotent (safe to retry).
 *
 ******************************************************************************
 */

#include "internal/trax_cmd_handler.h"
#include "trax_cmd_protocol.h"
#include "trax_config_default.h"
#include "trax_session.h"
#include "trax_meta_tx.h"
#include "trax_timestamp.h"
#include "trax_buffer.h"          /* For trax_buffer_reset() on restart */
#include "trax_frame.h"           /* For trax_frame_reset() on restart */
#include "trax_transport.h"       /* For TRAX_TRANSPORT_CLEAR_TX() on restart */
#include "../debug/trax_frame_validate.h"
#include "trax_diag.h"            /* For trax_diag_reset() on restart */

/* TraxProbe disabled (TRAX_ENABLE==0): whole unit compiles out. Command
 * handling is unreachable once trax_process() is a no-op stub. */
#if TRAX_ENABLE

/*=============================================================================
 ====================LOCAL VARIABLES===========================================
 ============================================================================*/

/** @brief Debug counter for valid commands handled */
static volatile uint32_t s_commands_handled = 0;

/** @brief Latched by CMD_SESSION_START; consumed (reset + SESSION_START)
 *         by trax_cmd_handler_process_pending_start() once no producer is
 *         inside a non-atomic ALLOC..COMMIT window. */
static volatile bool s_start_pending = false;

/** @brief Polls spent waiting for ring quiescence (p_rd32 == p_alloc32). */
static uint32_t s_start_rundown_polls = 0;

/*=============================================================================
 ====================LOCAL FUNCTION DECLARATION================================
 ============================================================================*/

static void trax_cmd_handle_session_start(enum trax_cmd_code_t cmd);
static void trax_cmd_handle_session_stop(enum trax_cmd_code_t cmd);
static void trax_cmd_handle_ping(enum trax_cmd_code_t cmd);
static void trax_cmd_handle_unknown(enum trax_cmd_code_t cmd);

/*=============================================================================
 ====================GLOBAL FUNCTION IMPLEMENTATION============================
 ============================================================================*/

int trax_cmd_handler_init(void)
{
	s_commands_handled = 0;
	s_start_pending = false;
	s_start_rundown_polls = 0;
	return 0;
}

void trax_cmd_handler_process_pending_start(void)
{
	if (!s_start_pending) {
		return;
	}

	/* Ring quiescence gate — uses only state the ring already maintains,
	 * so the producer hot path pays nothing for this feature.
	 *
	 * TRAX_BUFF_ALLOC advances p_alloc32 the moment a frame is reserved,
	 * but the frame's Word 0 stays 0 until TRAX_FRAME_COMMIT — and the
	 * reader never advances p_rd32 past a zero Word 0. Therefore
	 * p_rd32 == p_alloc32 proves BOTH that every committed frame has been
	 * drained AND that no producer is inside a non-atomic ALLOC..COMMIT
	 * window (an in-flight reservation would pin p_rd32 strictly behind
	 * p_alloc32). Only then is it safe to reset the ring: resetting under
	 * an in-flight producer would hand the same memory to the new session
	 * while the old producer keeps writing, and its late COMMIT would
	 * plant a stale old-epoch Word 0 mid-ring — killing the fresh session
	 * with FRAME_CORRUPT.
	 *
	 * Streaming was gated when the START was latched, so no new producer
	 * can start a frame: the ring monotonically drains toward equality.
	 * Deferring to the next trax_process() pass (rather than spinning)
	 * yields the CPU — we may be running above the in-flight producer's
	 * priority, and spinning would starve it forever on a single core. */
	/* TRAX_CFG_START_RUNDOWN_POLL_LIMIT (user-overridable in
	 * App/Config/trax_config.h — see config/trax_config_buffer.h) bounds
	 * the wait so a dead transport or a producer task suspended mid-frame
	 * cannot block the host's START forever; on expiry the reset proceeds
	 * with the historical (pre-quiescence) behavior. */
	if (trax_buffer.p_rd32 != trax_buffer.p_alloc32 &&
	    s_start_rundown_polls < TRAX_CFG_START_RUNDOWN_POLL_LIMIT) {
		s_start_rundown_polls++;
		return;
	}

	s_start_pending = false;
	s_start_rundown_polls = 0;

	/* Always reset buffer/frame state to ensure the start frame has space.
	 * Without this, undrained data from the previous session can prevent
	 * the START_TRACE frame from being allocated. */
	trax_buffer_reset();
	trax_frame_reset();
	trax_frame_validate_reset();
	trax_diag_reset();

	/* Discard stale bytes still parked in the transport TX buffer — e.g.
	 * frames from a boot-time trax_session_start() session the host never
	 * drained. The new session must begin with BOTH the application ring
	 * AND the transport TX ring empty; otherwise the SESSION_START and
	 * the first large stream frames land behind a near-full transport
	 * and hit instant backpressure, which can cascade into a ring
	 * overflow right after the host's START. Down-channel (commands) is
	 * deliberately untouched. */
	TRAX_TRANSPORT_CLEAR_TX();

	/* Send SESSION_START — this also enables streaming internally
	 * (after reserving buffer space, before filling dynamic metadata).
	 * Serves as implicit ACK to host. */
	if (trax_meta_tx_send_start() != 0) {
		trax_diag_report(TRAX_DIAG_BUFFER_OVERFLOW, 0);
	}
}

void trax_cmd_dispatch(enum trax_cmd_code_t cmd,
                       const uint8_t *params,
                       uint16_t params_len)
{
	(void)params;     /* Reserved for future commands with parameters */
	(void)params_len;

	switch (cmd)
	{
	case TRAX_CMD_SESSION_START:
		trax_cmd_handle_session_start(cmd);
		break;

	case TRAX_CMD_SESSION_STOP:
		trax_cmd_handle_session_stop(cmd);
		break;

	case TRAX_CMD_PING:
		trax_cmd_handle_ping(cmd);
		break;

	default:
		trax_cmd_handle_unknown(cmd);
		break;
	}
}

/*=============================================================================
 ====================LOCAL FUNCTION IMPLEMENTATION=============================
 ============================================================================*/

static void trax_cmd_handle_session_start(enum trax_cmd_code_t cmd)
{
	(void)cmd;  /* No ACK/NAK — SESSION_START frame is the implicit ACK */

	/* Gate new producers FIRST. From this store on, no frame macro can
	 * enter a new ALLOC..COMMIT window; only producers already past the
	 * TRAX_IS_SESSION_ACTIVE() gate remain in flight, and those drain within
	 * microseconds of getting CPU time. */
	if (trax_session.streaming) {
		trax_session_disable_streaming();
	}

	/* The actual reset + SESSION_START is deferred to
	 * trax_cmd_handler_process_pending_start(), called from
	 * trax_process() right after command processing. It executes once the
	 * ring is quiescent (p_rd32 == p_alloc32): with streaming gated the
	 * ring only drains, so that is typically the next trax_process() pass
	 * — one TRAX_CFG_CTRL_TASK_PERIOD_MS of extra latency on the host's
	 * SESSION_START ack, invisible at connect time. Deferring (instead of
	 * resetting inline as before) is what guarantees the ring is never
	 * reset under a producer still filling a reserved frame — see the
	 * quiescence gate comment in process_pending_start(). */
	s_start_pending = true;
	s_start_rundown_polls = 0;

	s_commands_handled++;
}

static void trax_cmd_handle_session_stop(enum trax_cmd_code_t cmd)
{
	(void)cmd;  /* No ACK/NAK — SESSION_STOP frame is the implicit ACK */

	/* A STOP overrides a START still waiting on producer rundown —
	 * last command wins, matching the host's view of the session. */
	s_start_pending = false;
	s_start_rundown_polls = 0;

	/* Act if streaming OR gap-gated (after a ring overflow streaming is
	 * temporarily 0 while a gap resume is queued — the host's STOP must
	 * cancel that resume and still receive its SESSION_STOP ack). */
	if (trax_session.streaming || trax_session.pending_gap_armed) {
		trax_session_clear_gap();
		/* Host-requested stop: NORMAL reason, no extra info needed.
		 * The host already knows it asked us to stop — the reason byte
		 * just confirms it and disambiguates from emergency stops. */
		trax_meta_tx_send_stop(TRAX_STOP_REASON_NORMAL, 0u);
		trax_session_disable_streaming(); /* SESSION_STOP will drain on next trax_send_frames() */
	}
	/* If not streaming, do nothing (idempotent) */

	s_commands_handled++;
}

static void trax_cmd_handle_ping(enum trax_cmd_code_t cmd)
{
	/* Utility command: Respond with ACK */
	trax_cmd_send_ack((uint8_t)cmd, TRAX_CMD_RESULT_SUCCESS);
	s_commands_handled++;
}

static void trax_cmd_handle_unknown(enum trax_cmd_code_t cmd)
{
	/* Unknown command - send NAK */
	trax_cmd_send_nak((uint8_t)cmd, 0xFF);
}

#endif /* TRAX_ENABLE */
