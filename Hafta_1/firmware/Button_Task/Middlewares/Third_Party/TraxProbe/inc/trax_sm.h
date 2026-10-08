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
 * @file           : trax_sm.h
 * @brief          : TraxProbe State Machine Tracing API
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 *
 * State machine tracing allows visualizing state transitions in Traxcope.
 * Each state machine gets a unique TID in the 0x7000-0x70FF range.
 * State changes emit a single-param frame with the new state index.
 *
 * FRAME FORMAT (16 bytes = 4 words):
 *   Word 0: [frame_size32:16][trans_counter:16]  (standard header)
 *   Word 1: [tick_cntr:8][timer_val:24]          (standard timestamp)
 *   Word 2: [TID:32]                             (TRAX_TID_RANGE_SM_START + sm_index)
 *   Word 3: [state_index:32]                     (new state, 0-based)
 *
 * USAGE:
 *   // 1. Define SM metadata (TIDs in trax_config.h, metadata in your .c file)
 *   TRAX_SM_DEFINE(TRAX_TID_SM_SYSTEM, "System", SM_STATE_INIT,
 *       TRAX_SM_STATE(SM_STATE_INIT, "INIT",  TRAX_COLOR_BLUE),
 *       TRAX_SM_STATE(SM_STATE_IDLE, "IDLE",  TRAX_COLOR_GREEN),
 *       TRAX_SM_STATE(SM_STATE_RUN,  "RUN",   TRAX_COLOR_NONE));
 *
 *   // 2. Emit state transitions at runtime
 *   TRAX_SM_SET_STATE(TRAX_TID_SM_SYSTEM, SM_STATE_IDLE);
 *
 ******************************************************************************
 */

#ifndef TRAX_SM_H_
#define TRAX_SM_H_

#include "trax_tid.h"
#include "trax_frame.h"
#include "trax_data_types.h"
#include "trax_meta_section.h"   /* TRAX_META_SECTION_SM */
#include "trax_config_default.h"
#include "trax_meta_tx.h"

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 ====================TRAX_CFG_SM_CNT DEFAULT===================================
 ============================================================================*/

/**
 * @brief Default TRAX_CFG_SM_CNT to 0 if the application did not declare it.
 *
 * TRAX_CFG_SM_CNT is the compile-time count of state machines the firmware
 * instantiates — same contract as TRAX_CFG_STREAM_CNT for var-streams.
 * The application declares it in trax_config.h:
 *
 *     #define TRAX_CFG_SM_CNT  2
 *
 * It sizes the current-state shadow table below, which is what lets the
 * SESSION_START / SESSION_GAP frames carry each SM's live state (late-join
 * support + gap resync).  Falling back to 0 keeps SM-less projects free
 * of the table; the static assert inside TRAX_SM_DEFINE fires with a
 * clear hint when an SM is defined without (or beyond) TRAX_CFG_SM_CNT.
 */
#ifndef TRAX_CFG_SM_CNT
#define TRAX_CFG_SM_CNT 0
#endif

/**
 * @brief Shadow-table sentinel: no TRAX_SM_SET_STATE since boot.
 *
 * The host falls back to the metadata-declared initial state (or its own
 * last observed state) when it sees this value.  State indexes must
 * therefore stay below 0xFF — enforced loosely by TRAX_SM_MAX_STATES.
 */
#define TRAX_SM_STATE_UNKNOWN  0xFFu

/*=============================================================================
 ====================CURRENT-STATE SHADOW TABLE================================
 ============================================================================*/

/**
 * @brief Per-SM current-state shadow table, indexed by TRAX_SM_TID_INDEX().
 *
 * Maintained unconditionally by TRAX_SM_SET_STATE (regardless of the
 * streaming gate) — same "tables are always maintained" contract as the
 * RTOS task/object tables — so a state change during a stream gate is
 * still reflected in the SESSION_GAP resync snapshot even though its
 * transition frame was suppressed.
 *
 * Plain byte stores are atomic on all supported targets; no critical
 * section is taken on the hot path.  Snapshot reads (cold path) may race
 * a concurrent transition — last writer wins, which is exactly the
 * "current state" semantic.
 */
#if TRAX_ENABLE && (TRAX_CFG_SM_CNT > 0)
extern volatile uint8_t p_trax_sm_state_list[TRAX_CFG_SM_CNT];
#endif

/*=============================================================================
 ====================RUNTIME MACRO=============================================
 ============================================================================*/

/**
 * @brief Record a state machine state transition
 *
 * Updates the current-state shadow table (one byte store — O(1), no
 * lookup, no critical section), then emits a TRAX frame with TID = tid
 * and a single parameter containing the new state index. Same wire cost
 * as TRAX_VAR_SET().
 *
 * The shadow store happens BEFORE the streaming gate on purpose: while
 * the stream is gated (overflow gap) the frame is suppressed, but the
 * table keeps tracking reality so the SESSION_GAP resync frame reports
 * the true current state.
 *
 * @param tid        State machine TID (e.g., TRAX_TID_SM_SYSTEM from enum)
 * @param state_idx  New state index (0-based within the SM)
 */
#if TRAX_ENABLE
#if (TRAX_CFG_SM_CNT > 0)
#define TRAX_SM_SET_STATE(tid, state_idx) \
    do { \
        p_trax_sm_state_list[TRAX_SM_TID_INDEX(tid)] = (uint8_t)(state_idx); \
        TRAX_FRAME_ARGS_ATOMIC((tid), (uint32_t)(state_idx)) \
    } while (0)
#else
/* No shadow table configured (TRAX_CFG_SM_CNT == 0): frame-only legacy
 * behaviour.  Any TRAX_SM_DEFINE in the project trips the static assert
 * below, pointing the user at TRAX_CFG_SM_CNT. */
#define TRAX_SM_SET_STATE(tid, state_idx) \
    TRAX_FRAME_ARGS_ATOMIC((tid), (uint32_t)(state_idx))
#endif
#else
#define TRAX_SM_SET_STATE(tid, state_idx) ((void)0)
#endif

/*=============================================================================
 ====================RUNTIME STATE API=========================================
 ============================================================================*/

/**
 * @brief Initialize the SM current-state shadow table (called from trax_init)
 *
 * Fills the table with TRAX_SM_STATE_UNKNOWN.  The initial state is NOT
 * seeded from the meta section here — metadata may live only in the ELF
 * (TRAX_META_IN_ELF), where it is not addressable at runtime.  The host
 * owns the "unknown → initial state" fallback instead.
 *
 * Always declared — body is a no-op when TRAX_CFG_SM_CNT == 0.
 */
void trax_sm_init(void);

/**
 * @brief Take a snapshot of current SM runtime state.
 *
 * Refreshes an internal wire table (sm_index + current_state per SM) and
 * returns a read-only pointer to it.  Same ownership rules as
 * trax_stream_snapshot_runtime_meta(): the buffer is owned by this
 * module and must be consumed before the next call.  Returns NULL (and
 * *p_count = 0) when TRAX_CFG_SM_CNT == 0.
 *
 * @param[out] p_count  Number of entries in the returned table.
 * @return Pointer to the snapshot table, or NULL when no SMs are configured.
 */
const struct trax_sm_runtime_meta_t *
trax_sm_snapshot_runtime_meta(uint8_t *p_count);

/*=============================================================================
 ====================CONSTANTS=================================================
 ============================================================================*/

#define TRAX_SM_MAX_COUNT       TRAX_TID_SM_RANGE_SIZE

/*=============================================================================
 ====================METADATA (sent in START_TRACE)============================
 ============================================================================*/

/**
 * @brief Define a single state entry for use inside TRAX_SM_DEFINE()
 *
 * @param _idx    State index (enum value, 0-based)
 * @param _name   Human-readable state name (max 32 chars)
 * @param _color  TRAX_COLOR_RGB(r,g,b), TRAX_COLOR_xxx, or TRAX_COLOR_NONE (auto)
 */
#define TRAX_SM_STATE(_idx, _name, _color) \
    { (_idx), {0, 0, 0}, TRAX_COLOR_TO_RGB_(_color), _name }

/**
 * @brief Define a state machine with inline state entries
 *
 * Creates a single unified struct in the .trax_sm linker section.
 * State count is computed automatically at compile time.
 *
 * @param _tid      State machine TID (e.g., TRAX_TID_SM_SYSTEM)
 * @param _name     Human-readable SM name (max 32 chars)
 * @param _initial  Initial state index (0-based)
 * @param ...       TRAX_SM_STATE() entries (variadic)
 *
 * Example:
 * @code
 *   TRAX_SM_DEFINE(TRAX_TID_SM_SYSTEM, "System", SYS_INIT,
 *       TRAX_SM_STATE(SYS_INIT,  "INIT",  TRAX_COLOR_BLUE),
 *       TRAX_SM_STATE(SYS_IDLE,  "IDLE",  TRAX_COLOR_GREEN),
 *       TRAX_SM_STATE(SYS_RUN,   "RUN",   TRAX_COLOR_RGB(255, 165, 0)),
 *       TRAX_SM_STATE(SYS_ERROR, "ERROR", TRAX_COLOR_RED));
 * @endcode
 */
#if TRAX_ENABLE
#define TRAX_SM_DEFINE(_tid, _name, _initial, ...) \
    TRAX_STATIC_ASSERT( \
        sizeof((struct trax_sm_state_entry_t[]){ __VA_ARGS__ }) / \
            sizeof(struct trax_sm_state_entry_t) <= TRAX_SM_MAX_STATES, \
        "State count exceeds TRAX_SM_MAX_STATES"); \
    TRAX_STATIC_ASSERT((uint32_t)TRAX_SM_TID_INDEX(_tid) < (uint32_t)TRAX_CFG_SM_CNT, \
        "SM TID index >= TRAX_CFG_SM_CNT — increase TRAX_CFG_SM_CNT in trax_config.h to cover this state machine"); \
    static const struct trax_sm_wire_t __trax_sm_##_tid \
        TRAX_SECTION(TRAX_META_SECTION_SM) = { \
            .sm_index = (_tid) - TRAX_TID_RANGE_SM_START, \
            .state_count = (uint8_t)(sizeof((struct trax_sm_state_entry_t[]){ __VA_ARGS__ }) / \
                                     sizeof(struct trax_sm_state_entry_t)), \
            .initial_state_index = (_initial), \
            .p_name = _name, \
            .p_states = { __VA_ARGS__ } \
        }
#else
/* TraxProbe disabled: no SM metadata. Benign repeatable struct decl. */
#define TRAX_SM_DEFINE(_tid, _name, _initial, ...) struct trax_sm_wire_t
#endif

#ifdef __cplusplus
}
#endif

#endif /* TRAX_SM_H_ */
