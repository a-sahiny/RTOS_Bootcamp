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
 * @file           : trax_trigger.h
 * @brief          : TraxProbe Trigger API — pause/resume on rare conditions
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 *
 * Probe-side triggers exist for HIGH stream rates + RARE conditions on
 * band-limited links.  When bandwidth is plentiful you do not need them:
 * stream everything and analyze offline in Traxcope's Trigger Studio.
 * When the link cannot carry the full rate, pause the stream and let the
 * firmware resume it at the moment of interest:
 *
 *   1. Pause:   trax_session_pause() (or a host command).  Producers are
 *               gated at the existing one-load-one-branch streaming check;
 *               everything captured BEFORE the pause still drains to the
 *               host.  Suppressed frames are counted while paused.
 *   2. Fire:    TRAX_TRIGGER_FIRE(tid, ctx) from any context — e.g. an
 *               analog-comparator over-voltage ISR.  O(1): a first-wins
 *               latch of (tid, ctx, exact fire time) plus the pause
 *               release, inside one short critical section.
 *   3. Resume:  the next trax_process() pass emits the SESSION_GAP resync
 *               frame — full dynamic snapshot (stream runtime, RTOS
 *               tables, SM states) PLUS the trigger attribution (TID,
 *               context word, exact fire-time anchor) — and streaming
 *               continues live from the trigger onward.
 *
 * TID RANGE: 0x7200-0x72FF (TRAX_TID_RANGE_TRIGGER_*).
 *
 * FRAME FORMAT (fired while streaming live — a marker-style event):
 *   Word 0: [frame_size32:16][trans_counter:16]  (standard header)
 *   Word 1: [tick_cntr:8][timer_val:24]          (standard timestamp)
 *   Word 2: [trigger_tid:32]
 *   Word 3: [ctx:32]                             (user context word)
 *
 * USAGE:
 *   TRAX_TRIGGER_DEFINE(TID_TRG_OVERVOLT, "Over-voltage", TRAX_COLOR_RED);
 *
 *   void COMP1_IRQHandler(void) {
 *       TRAX_TRIGGER_FIRE(TID_TRG_OVERVOLT, adc_last_sample);
 *       ...
 *   }
 *
 * REAL-TIME COST:
 *   - Fire: one early-exit load+branch when not paused / already fired,
 *     else ~6 stores in a critical section, plus the normal gated event
 *     frame.  Same masking class as any TRAX_LOG.
 *   - Paused stream: producers pay the existing streaming-gate branch —
 *     identical to an overflow gate.  Nothing new on any hot path.
 *
 ******************************************************************************
 */

#ifndef TRAX_TRIGGER_H_
#define TRAX_TRIGGER_H_

#include <stdint.h>
#include "trax_frame.h"
#include "trax_data_types.h"     /* TRAX_SECTION, TRAX_STATIC_ASSERT, TRAX_PACKED */
#include "trax_tid.h"            /* TRAX_TID_RANGE_TRIGGER_* */
#include "trax_meta_section.h"   /* TRAX_META_SECTION_TRIGGER */
#include "trax_config_default.h"
#include "trax_color.h"
#include "trax_session.h"        /* trax_session_latch_trigger() */

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 ====================RUNTIME TRIGGER MACRO=====================================
 ============================================================================*/

/**
 * @brief Fire a trigger: release a paused stream + emit the event frame.
 *
 * From any context (task / ISR).  If the stream is paused, the first fire
 * of the episode latches (tid, ctx, exact fire time) for the resync
 * frame's attribution and releases the pause; the event frame itself is
 * gated while paused — the resync carries the information instead.  If
 * the stream is live, this is simply a timestamped trigger event frame.
 *
 * @param _tid Trigger Trace ID (0x7200-0x72FF, see TRAX_TRIGGER_DEFINE)
 * @param _ctx User context word shipped with the event (e.g. the measured
 *             fault value). Pass 0 if not needed.
 */
#if TRAX_ENABLE
#define TRAX_TRIGGER_FIRE(_tid, _ctx) \
    do { \
        TRAX_STATIC_ASSERT( \
            (_tid) >= TRAX_TID_RANGE_TRIGGER_START && \
            (_tid) <= TRAX_TID_RANGE_TRIGGER_END, \
            "TRAX_TRIGGER: TID is outside the TRIGGER range (0x7200-0x72FF)"); \
        trax_session_latch_trigger((uint16_t)(_tid), (uint32_t)(_ctx)); \
        TRAX_FRAME_ARGS_ATOMIC((_tid), (uint32_t)(_ctx)) \
    } while (0)
#else
#define TRAX_TRIGGER_FIRE(_tid, _ctx) ((void)0)
#endif

/*=============================================================================
 ====================METADATA==================================================
 ============================================================================*/

/**
 * @brief Trigger metadata structure (ELF/flash .trax_trigger section)
 *
 * Extracted by Traxcope from the ELF (or sent inline in SESSION_START
 * when TRAX_CFG_META_STORAGE == TRAX_META_IN_FLASH).  40 bytes packed —
 * host WireTriggerMeta must match in lockstep.
 */
struct trax_trigger_meta_t {
    uint16_t id;                              /**< Trigger TID (0x7200-0x72FF) */
    uint16_t reserved;                        /**< Reserved for alignment */
    uint32_t color;                           /**< 0x00RRGGBB display color (0 = auto-assign) */
    char     p_name[TRAX_CFG_META_NAME_LEN];  /**< Human-readable trigger name */
} TRAX_PACKED;

/**
 * @brief Define trigger metadata (stored in the .trax_trigger section)
 *
 * THIS MACRO IS OPTIONAL. TRAX_TRIGGER_FIRE works without it; Traxcope
 * then auto-generates the name ("TRIGGER_0xXXXX").
 *
 * @param _tid   Trigger Trace ID (0x7200-0x72FF)
 * @param _name  Human-readable name (max TRAX_CFG_META_NAME_LEN chars)
 * @param _color Display color: TRAX_COLOR_RGB(r,g,b), TRAX_COLOR_xxx, or TRAX_COLOR_NONE
 */
#if TRAX_ENABLE
#define TRAX_TRIGGER_DEFINE(_tid, _name, _color) \
    TRAX_STATIC_ASSERT(sizeof(_name) <= TRAX_CFG_META_NAME_LEN, \
        "Trigger name exceeds TRAX_CFG_META_NAME_LEN"); \
    TRAX_STATIC_ASSERT((_tid) >= TRAX_TID_RANGE_TRIGGER_START && \
                       (_tid) <= TRAX_TID_RANGE_TRIGGER_END, \
        "TRAX_TRIGGER_DEFINE: TID is outside the TRIGGER range (0x7200-0x72FF)"); \
    static const struct trax_trigger_meta_t __trax_trigger_meta_##_tid \
        TRAX_SECTION(TRAX_META_SECTION_TRIGGER) = { \
            .id = (_tid), \
            .reserved = 0, \
            .color = TRAX_COLOR_TO_RGB_(_color), \
            .p_name = _name \
        }
#else
/* TraxProbe disabled: no trigger metadata. Benign repeatable struct decl. */
#define TRAX_TRIGGER_DEFINE(_tid, _name, _color) struct trax_trigger_meta_t
#endif

#ifdef __cplusplus
}
#endif

#endif /* TRAX_TRIGGER_H_ */
