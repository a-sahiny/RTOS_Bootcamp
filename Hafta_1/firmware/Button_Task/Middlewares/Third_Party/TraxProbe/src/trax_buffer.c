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
 * @file           : traxBuffer.c
 * @brief          : TraxProbe Ring Buffer Implementation
 * @version        : 3.0.0
 ******************************************************************************
 * @attention
 * 
 * Single global buffer instance - no pointer indirection.
 * 
 ******************************************************************************
 */

#include "trax_buffer.h"
#include "trax_session.h"        /* trax_session.streaming, gap latch */
#include <string.h>

/* TraxProbe disabled (TRAX_ENABLE==0): whole unit compiles out. The buffer
 * entry points are not referenced once the public API is no-op'd. */
#if TRAX_ENABLE

/*=============================================================================
 ====================GLOBAL VARIABLES==========================================
 ============================================================================*/

/**
 * @brief Global buffer instance
 * 
 * Aligned to 8 bytes for optimal DMA and memory access performance.
 */
struct trax_buffer_t trax_buffer TRAX_ALIGNED(8);

/*=============================================================================
 ====================GLOBAL FUNCTION IMPLEMANTATION============================
 ============================================================================*/

void trax_buffer_init(void)
{
	/* Zero-initialize buffer memory for clean state */
	memset(trax_buffer.p_mem32, 0, sizeof(trax_buffer.p_mem32));
	
	/* Initialize pointers */
	trax_buffer.p_start32 = &trax_buffer.p_mem32[0U];
	trax_buffer.p_end32 = &trax_buffer.p_mem32[TRAX_CFG_OUT_BUFFER_SIZE32];
	trax_buffer.p_safe32 = trax_buffer.p_end32 - 1;
	trax_buffer.p_alloc32 = trax_buffer.p_start32;
	trax_buffer.p_rd32 = trax_buffer.p_start32;
	
	/* Initialize counters */
	trax_buffer.alloc_fail_cntr = 0;
	
	/* No callbacks by default */
	trax_buffer.trax_buffer_alloc_fail_callback = NULL;
}

void trax_buffer_set_alloc_fail_callback(trax_buffer_alloc_fail_callback_t callback)
{
	trax_buffer.trax_buffer_alloc_fail_callback = callback;
}

void trax_buffer_clear(void)
{
	/* Invalidate any stale committed Word 0 left in memory from the previous
	 * session.  The sentinel chain (each allocation writes *p_alloc = 0 at
	 * the slot that follows it) guarantees that every frame position is
	 * already zero when it is next allocated — EXCEPT p_start32, which is
	 * never the successor of another frame and therefore never receives a
	 * sentinel write.  Without this store the reader could see the old
	 * committed header and process garbage metadata on the first read of the
	 * new session. */
	*trax_buffer.p_start32 = 0;

	/* Reset buffer pointers to start */
	trax_buffer.p_alloc32 = trax_buffer.p_start32;
	trax_buffer.p_rd32 = trax_buffer.p_start32;

	/* Note: alloc_fail_cntr is NOT reset - tracks total overflows across sessions */
}

void trax_buffer_reset(void)
{
	/* Clear buffer contents, preserving callback and cumulative counters */
	trax_buffer_clear();
}

void trax_buffer_on_alloc_fail(uint32_t requested_size32)
{
	/* Cumulative across overflows / sessions. Reported in every diag
	 * snapshot and used by the gap resume path to compute the per-
	 * episode dropped-frame count. */
	trax_buffer.alloc_fail_cntr++;

	/* Gate new producers immediately. We are inside the caller's
	 * TRAX_BUFF_ALLOC critical section, so this store is atomic with
	 * respect to every other TRAX_FRAME_ALLOC site — no log frame can
	 * land in the ring after this point. */
	trax_session.streaming = 0;

	/* Arm the gap latch: the ring keeps draining via
	 * trax_send_frames(); once quiescent, trax_process() emits a
	 * TRAX_TID_SESSION_GAP resync frame and re-enables streaming.
	 * First-wins inside arm_gap: only the first failed alloc of the
	 * episode captures the gap start anchor. Fatal errors
	 * (FRAME_CORRUPT / TRANSPORT_FAIL) still arm the pending-stop
	 * latch from their own paths and override a pending gap resume
	 * in trax_process(). */
	trax_session_arm_gap();

	/* Optional user hook (diag stats collector, application-level
	 * watchdog kick, etc.). Called LAST so the streaming-gate flip
	 * and the pending-stop arm are guaranteed to have happened by
	 * the time user code runs — a misbehaving callback cannot skip
	 * the autopsy. */
	if (trax_buffer.trax_buffer_alloc_fail_callback) {
		trax_buffer.trax_buffer_alloc_fail_callback(requested_size32);
	}
}

#endif /* TRAX_ENABLE */
