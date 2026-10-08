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
 * @file           : trax_meta_tx_new.h
 * @date        	  : Jan 18, 2026
 * @author         : bemre
 * @version        : TODO
 *
 * @brief          : TODO
 ******************************************************************************
 * @attention
 * 	TODO
 ******************************************************************************
 * @code {.c}
 *      ...
 * 	TODO
 *      ...
 * @endcode
 */

#ifndef TRAX_META_TX_NEW_H_
#define TRAX_META_TX_NEW_H_

#include <stdint.h>
#include "trax_meta_type.h"    /* Metadata struct definitions */
#include "trax_config_default.h"
#include "trax_tid.h"
#include "trax_data_types.h"    /* For TRAX_PACKED */
#include "trax_version.h"

/*=============================================================================
 ====================GLOBAL MACRO DEFINITIONS==================================
 ============================================================================*/
/**
 * @brief TRAX Session Keyword
 *
 * "TRAX" in ASCII: T=0x54 R=0x52 A=0x41 X=0x58
 * Used in SESSION_START frame for sync detection and endianness validation.
 */
#define TRAX_SESSION_KEYWORD  0x58415254U  /* LE: "TRAX" (wire: 54 52 41 58) */

/*=============================================================================
 ====================GLOBAL MACRO FUNCTIONS====================================
 ============================================================================*/

/*=============================================================================
 ====================GLOBAL TYPEDEF============================================
 ============================================================================*/


/*=============================================================================
 ====================GLOBAL VARIABLES==========================================
 ============================================================================*/

/*=============================================================================
 ====================GLOBAL FUNCTION DECLERATION===============================
 ============================================================================*/

/**
 * @brief Initialize metadata transmission module
 *
 * Call this during trax_init().
 */
void trax_meta_tx_init(void);

/**
 * @brief Send SESSION_START frame and enable streaming
 *
 * Allocates buffer space for the start frame, then enables streaming
 * (so ISR/task frames flow after the reserved space and dynamic RTOS
 * metadata reflects the live state), then fills and commits the frame.
 *
 * Frame contains:
 * - Header with sync data (magic, version, freq, session_id, tick_overflow_cntr, timestamp)
 * - Metadata counts
 * - All static metadata (signals, logs, streams, ISRs)
 * - RTOS task metadata (from dynamic task table)
 * - RTOS object metadata (from dynamic object table)
 *
 * The frame uses transaction counter = 0 for synchronization.
 *
 * @return 0 on success, -1 if buffer allocation failed (streaming NOT enabled)
 */
int32_t trax_meta_tx_send_start(void);


/**
 * @brief Send session stop marker.
 *
 * Sends a TRAX_TID_SESSION_STOP frame with a 2-word payload [reason, info]
 * so the host can render an autopsy banner explaining why streaming
 * stopped (host request, frame validator reject, transport error, …).
 *
 * Allocates through the normal ring buffer using TRAX_FRAME_ARGS_PROTOCOL,
 * which is NOT gated by TRAX_IS_SESSION_ACTIVE(). That gate-bypass is what
 * lets trax_process() emit a single STOP frame *after* the hot path has
 * already flipped trax_session.streaming = 0 — see the pending-stop
 * latch contract in trax_session.h.
 *
 * Callers:
 *   - trax_cmd_handle_session_stop()  → reason = TRAX_STOP_REASON_NORMAL
 *   - trax_session_stop()                → reason = TRAX_STOP_REASON_USER
 *   - trax_process() consume-stop tail  → reason = whatever the hot path
 *                                          armed via trax_session_arm_stop()
 *                                          (FRAME_CORRUPT, TRANSPORT_FAIL,
 *                                          HEAP_EXHAUSTED).
 *
 * @param reason  Why streaming stopped (see enum trax_stop_reason_t).
 * @param info    Reason-specific 32-bit context. Pass 0 if not applicable.
 *
 * @return 0 on success
 */
int32_t trax_meta_tx_send_stop(enum trax_stop_reason_t reason, uint32_t info);

/**
 * @brief Send a TRAX_TID_SESSION_GAP resync frame and re-enable streaming.
 *
 * Called from trax_process() when the gap latch is armed (a ring
 * overflow gated the stream) and the ring is quiescent
 * (p_rd32 == p_alloc32 — fully drained, no in-flight producer).
 *
 * Mirrors trax_meta_tx_send_start()'s reserve → enable-streaming → fill
 * → commit pattern: the frame slot is reserved first (bypassing the
 * streaming gate), streaming is re-enabled so new producer frames land
 * BEHIND the gap frame and the dynamic tables reflect live state, then
 * the payload is filled and Word 0 committed last.
 *
 * Payload: struct trax_gap_header_t (gap start/end time anchors, current
 * tick_overflow_cntr, dropped-frame count, gap sequence number, section
 * counts) followed by word-padded sections — stream runtime meta, RTOS
 * task wire structs, RTOS object wire structs, SM runtime meta (same
 * structs as SESSION_START, so the host reuses its section parsers).
 *
 * On success the gap latch is cleared (via trax_session_enable_streaming).
 * On allocation failure the latch stays armed and the caller retries on
 * the next trax_process() tick — with an empty ring this can only mean
 * the dynamic tables exceed the ring size (pathological configuration).
 *
 * @return 0 on success, -1 if frame allocation failed (latch stays armed)
 */
int32_t trax_meta_tx_send_gap(void);

#endif /* TRAX_META_TX_NEW_H_ */
