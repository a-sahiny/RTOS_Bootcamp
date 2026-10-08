/*
 * SPDX-License-Identifier: LicenseRef-TraxProbe-Commercial
 * Copyright (c) 2026 Embedya. All rights reserved.
 *
 * TraxProbe core engine. CONFIDENTIAL and proprietary to Embedya.
 * Licensed under the TraxProbe Commercial License (see LICENSE-COMMERCIAL.txt).
 * No use, copying, modification, redistribution, decompilation, or reverse
 * engineering is permitted except as expressly authorized by that agreement.
 */

#ifndef TRAX_BUFFER_H_
#define TRAX_BUFFER_H_

#include "trax_config_default.h"
#include "trax_compiler.h"
#include <stddef.h>

/*=============================================================================
 ====================GLOBAL MACRO FUNCTIONS====================================
 ============================================================================*/

/**
 * @brief Wrap marker written at end-of-buffer gap when allocator wraps to start.
 *
 * Value has frame_size32=1 in upper 16 bits (below TRAX_CFG_FRAME_MIN_SIZE32=3),
 * making it distinguishable from any valid committed frame (frame_size32 >= 3)
 * and from the uncommitted sentinel (0).
 */
#define TRAX_BUFF_WRAP_MARKER  0x00010000U

/* Ultra-fast contiguous memory allocator (no separate length header).
 *
 * Frame data is written directly at the returned p_wr.  Word 0 of the frame
 * is left as 0 (sentinel) by the allocator and must be written LAST by the
 * caller's commit step to serve as the atomic commit marker.
 *
 * On wrap: TRAX_BUFF_WRAP_MARKER is written at the gap so the reader can
 * detect the discontinuity and jump to p_start32.
 *
 * Returns p_wr = pointer to allocated memory, or NULL on failure.
 *
 * On failure both branches funnel into trax_buffer_on_alloc_fail(), which
 * bumps alloc_fail_cntr, flips trax_session.streaming = 0, arms the
 * gap latch (pause / drain / TRAX_TID_SESSION_GAP resume), and invokes
 * the optional user callback. The macro is invoked from inside the
 * caller's TRAX_PORT_ENTER_CRITICAL_SECTION (see TRAX_FRAME_ALLOC), so
 * the streaming-flag flip and latch-arm are atomic with respect to all
 * other producers. Centralising the failure handler keeps every
 * TRAX_LOG_x / TRAX_FRAME_ARGS expansion site to a single function call
 * on the cold path. */
#define TRAX_BUFF_ALLOC(p_wr, size32) \
{ \
	const uint32_t* p_rd = trax_buffer.p_rd32; \
	uint32_t *p_alloc = trax_buffer.p_alloc32; \
	\
	/* Case 1: Allocation pointer >= Read pointer */ \
	if (p_alloc >= p_rd) { \
		size_t space_to_safe = (size_t)(trax_buffer.p_safe32 - p_alloc); \
		\
		/* If enough space at end, allocate there */ \
		if (space_to_safe > (size_t)(size32)) { \
			p_wr = p_alloc; \
			p_alloc += (size32); \
			*p_alloc = 0; \
			trax_buffer.p_alloc32 = p_alloc; \
		} \
		/* If not enough at end, wrap to start */ \
		else if ((size_t)(p_rd - trax_buffer.p_start32) > (size_t)(size32)) { \
			*p_alloc = TRAX_BUFF_WRAP_MARKER; \
			p_wr = trax_buffer.p_start32; \
			*p_wr = 0; \
			p_alloc = p_wr + (size32); \
			*p_alloc = 0; \
			trax_buffer.p_alloc32 = p_alloc; \
		} \
		else { \
			p_wr = NULL; \
			trax_buffer_on_alloc_fail(size32); \
		} \
	} \
	/* Case 2: Allocation pointer < Read pointer */ \
	else { \
		if ((size_t)(p_rd - p_alloc) > (size_t)(size32)) { \
			p_wr = p_alloc; \
			p_alloc += (size32); \
			*p_alloc = 0; \
			trax_buffer.p_alloc32 = p_alloc; \
		} \
		else { \
			p_wr = NULL; \
			trax_buffer_on_alloc_fail(size32); \
		} \
	} \
}

/*=============================================================================
 ====================GLOBAL TYPEDEF============================================
 ============================================================================*/
/**
 * Callback for when a buffer allocation fails (buffer full)
 *
 * @param requested_size32  The allocation size (in 32-bit words) that failed
 */
typedef void (*trax_buffer_alloc_fail_callback_t)(uint32_t requested_size32);

struct trax_buffer_t {
	uint32_t p_mem32[TRAX_CFG_OUT_BUFFER_SIZE32] TRAX_ALIGNED(8);
	uint32_t * volatile p_alloc32;   /* Modified by writer (ISR), read by writer */
	uint32_t * volatile p_rd32;      /* Modified by reader, read by writer (ISR) */
	uint32_t *p_start32;             /* Constant after init */
	uint32_t *p_end32;               /* Constant after init */
	uint32_t *p_safe32;              /* Constant after init */
	volatile uint32_t alloc_fail_cntr; /* Modified by writer (ISR), read by reader */
	trax_buffer_alloc_fail_callback_t trax_buffer_alloc_fail_callback;
};

/*=============================================================================
 ====================GLOBAL VARIABLES==========================================
 ============================================================================*/
extern struct trax_buffer_t trax_buffer;

/*=============================================================================
 ====================GLOBAL FUNCTION DECLERATION===============================
 ============================================================================*/
/**
 * @brief Initialize buffer module
 */
void trax_buffer_init(void);

/**
 * @brief Set callback for buffer allocation failure notification
 *
 * Called from inside TRAX_BUFF_ALLOC when the ring buffer is full.
 * WARNING: This fires in the context of the caller (possibly ISR).
 * Keep the callback short and non-blocking.
 *
 * @param callback Function to call on alloc failure (can be NULL to disable)
 */
void trax_buffer_set_alloc_fail_callback(trax_buffer_alloc_fail_callback_t callback);

/**
 * @brief Clear buffer (reset pointers)
 * 
 * Resets the buffer to empty state without clearing buffer memory.
 * Called on session start to discard any stale data from previous session.
 * 
 * Note: alloc_fail_cntr is NOT reset - it tracks total allocation failures across sessions.
 */
void trax_buffer_clear(void);

/**
 * @brief Reset buffer for trace restart
 * 
 * Clears buffer contents (pointers) while preserving the written
 * callback and cumulative alloc_fail_cntr.
 * Use on restart instead of trax_buffer_init().
 */
void trax_buffer_reset(void);

/**
 * @brief Centralised failure handler invoked from the TRAX_BUFF_ALLOC macro.
 *
 * Called from both failure branches of TRAX_BUFF_ALLOC (no-room-at-end
 * AND no-room-after-wrap) when the ring is full. Runs inside the
 * caller's TRAX_PORT_ENTER_CRITICAL_SECTION (see TRAX_FRAME_ALLOC), so
 * the streaming-flag flip below is atomic with respect to other
 * application threads / ISRs that might be racing to allocate frames —
 * once we return, no further producer can land a frame in the full
 * ring.
 *
 * Responsibilities:
 *   1. Bump trax_buffer.alloc_fail_cntr (cumulative across overflows).
 *   2. Flip trax_session.streaming = 0 to immediately gate new
 *      TRAX_LOG_x / TRAX_FRAME_ARGS sites.
 *   3. Arm the gap latch so trax_process() drains the ring and then
 *      resumes streaming with a TRAX_TID_SESSION_GAP resync frame.
 *   4. Fire the optional user-registered callback (e.g. diag stats
 *      collector) for additional bookkeeping.
 *
 * Keep work here minimal — this runs on the producer hot path with
 * interrupts masked. No logging, no allocations, no critical-section
 * nesting.
 *
 * @param requested_size32  The 32-bit-word size that failed to allocate;
 *                          forwarded to the user callback.
 */
void trax_buffer_on_alloc_fail(uint32_t requested_size32);

#endif /* TRAX_BUFFER_H_ */
