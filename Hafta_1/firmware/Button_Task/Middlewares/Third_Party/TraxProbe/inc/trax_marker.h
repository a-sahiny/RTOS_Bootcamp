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
 * @file           : trax_marker.h
 * @brief          : TraxProbe Marker Interval API for Real-Time Performance
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 *
 * This module provides lightweight start/stop marker instrumentation for
 * measuring elapsed time of code sections. All statistics computation
 * happens on the host (Traxcope) -- the embedded side only emits minimal
 * timestamped frames (12 bytes per event).
 *
 * TID RANGE:
 *   Uses the USER range 0x5000-0x5FFF (start events).
 *   Stop events use the same TID with bit 15 set: 0xC000-0xCFFF.
 *
 * FRAME FORMAT (minimal -- 3 words = 12 bytes):
 *   Word 0: [frame_size32:16][trans_counter:16]  (standard header)
 *   Word 1: [tick_cntr:8][timer_val:24]          (standard timestamp)
 *   Word 2: [marker_tid:32]                      (TID, bit 15 = stop flag)
 *
 * USAGE:
 *   // 1. Define metadata (optional, stored in ELF only)
 *   //    Args: tid, name, color, warn_upper_us, deadline_upper_us
 *   TRAX_MARKER_DEFINE(0x5001, "ControlLoop", TRAX_COLOR_ORANGE, 400.0f, 600.0f);
 *   TRAX_MARKER_DEFINE(0x5002, "SensorRead",  TRAX_COLOR_TEAL,   150.0f, 200.0f);
 *
 *   // 2. Instrument code
 *   void controlLoopTask(void) {
 *       TRAX_MARKER_START(0x5001);
 *       TRAX_MARKER_START(0x5002);
 *       readSensors();
 *       TRAX_MARKER_STOP(0x5002);
 *       computeControl();
 *       TRAX_MARKER_STOP(0x5001);
 *   }
 *
 * DESIGN:
 *   - Zero extra RAM on device (no on-device statistics)
 *   - Minimal CPU overhead (single atomic frame write per event)
 *   - Host pairs start/stop events and computes all statistics
 *   - Nesting supported (same TID can nest; host uses stack-based pairing)
 *
 ******************************************************************************
 */

#ifndef TRAX_MARKER_H_
#define TRAX_MARKER_H_

#include <stdint.h>
#include "trax_frame.h"
#include "trax_data_types.h"    /* TRAX_SECTION, TRAX_STATIC_ASSERT */
#include "trax_tid.h"            /* TRAX_TID_RANGE_MARKER_START / _END for range checks */
#include "trax_meta_section.h"   /* TRAX_META_SECTION_MARKER */
#include "trax_config_default.h"
#include "trax_color.h"

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 ====================MARKER TID ENCODING=======================================
 ============================================================================*/

/**
 * @brief Stop-flag bit position in the TID word
 *
 * Start events use the raw TID (0x4xxx).
 * Stop events set bit 15: TID | 0x8000 (0xDxxx).
 * The host strips bit 15 to recover the marker ID.
 */
#define TRAX_MARKER_STOP_FLAG       0x8000U

/**
 * @brief Encode a marker TID for a STOP event
 */
#define TRAX_MARKER_TID_STOP(_tid)  ((_tid) | TRAX_MARKER_STOP_FLAG)

/**
 * @brief Check if a TID is a marker start (USER range, bit 15 clear)
 */
#define TRAX_IS_MARKER_START_TID(tid) \
    (((tid) >= TRAX_TID_RANGE_USER_START) && ((tid) <= TRAX_TID_RANGE_USER_END))

/**
 * @brief Check if a TID is a marker stop (USER range with bit 15 set)
 */
#define TRAX_IS_MARKER_STOP_TID(tid) \
    (((tid) >= (TRAX_TID_RANGE_USER_START | TRAX_MARKER_STOP_FLAG)) && \
     ((tid) <= (TRAX_TID_RANGE_USER_END   | TRAX_MARKER_STOP_FLAG)))

/**
 * @brief Check if a TID is any marker event (start or stop)
 */
#define TRAX_IS_MARKER_TID(tid) \
    (TRAX_IS_MARKER_START_TID(tid) || TRAX_IS_MARKER_STOP_TID(tid))

/**
 * @brief Extract the base marker ID (strip stop flag)
 */
#define TRAX_MARKER_BASE_TID(tid)   ((tid) & ~TRAX_MARKER_STOP_FLAG)

/*=============================================================================
 ====================RUNTIME MARKER MACROS=====================================
 ============================================================================*/

/**
 * @brief Record marker START (call at the beginning of the measured section)
 *
 * Emits a minimal frame (12 bytes): header + timestamp + TID.
 * No parameters needed — the timestamp IS the measurement data.
 *
 * Uses TRAX_FRAME_NOARGS_ATOMIC which keeps alloc + timestamp capture +
 * commit in one critical section, guaranteeing cycle-accurate timestamps.
 *
 * @param _tid Marker Trace ID (must be in range 0x5000-0x5FFF)
 */
#if TRAX_ENABLE
#define TRAX_MARKER_START(_tid) \
    do { \
        TRAX_STATIC_ASSERT( \
            ((_tid) & ~TRAX_MARKER_STOP_FLAG) >= TRAX_TID_RANGE_MARKER_START && \
            ((_tid) & ~TRAX_MARKER_STOP_FLAG) <= TRAX_TID_RANGE_MARKER_END, \
            "TRAX_MARKER: TID is outside the MARKER range (0x5000-0x5FFF)"); \
        TRAX_FRAME_NOARGS_ATOMIC(_tid) \
    } while(0)

/**
 * @brief Record marker STOP (call at the end of the measured section)
 *
 * Same as TRAX_MARKER_START but sets bit 15 of the TID to mark it as STOP.
 *
 * @param _tid Marker Trace ID (must be in range 0x5000-0x5FFF)
 */
#define TRAX_MARKER_STOP(_tid) \
    TRAX_MARKER_START(TRAX_MARKER_TID_STOP(_tid))
#else
#define TRAX_MARKER_START(_tid) ((void)0)
#define TRAX_MARKER_STOP(_tid)  ((void)0)
#endif

/*=============================================================================
 ====================METADATA (ELF ONLY)=======================================
 ============================================================================*/

/**
 * @brief Marker metadata structure (stored in ELF .trax_marker section)
 *
 * Extracted by Traxcope from the ELF file. Contains the marker name,
 * display color, and optional deadline/warning thresholds.
 * Threshold values of 0 mean "not set" -- Traxcope UI can override them
 * and persists overrides in the session schema.
 */
struct trax_marker_meta_t {
    uint16_t id;                                /**< Marker TID (0x5000-0x5FFF) */
    uint16_t reserved;                          /**< Reserved for alignment */
    uint32_t color;                             /**< 0x00RRGGBB display color (0 = auto-assign) */
    char     p_name[TRAX_CFG_META_NAME_LEN];   /**< Human-readable marker name */
    float    deadline_upper_us;                 /**< Upper deadline in µs (0 = none) */
    float    warn_upper_us;                     /**< Upper warning in µs (0 = none) */
} TRAX_PACKED;

/**
 * @brief Define marker metadata (stored in ELF .trax_marker section)
 *
 * THIS MACRO IS OPTIONAL. TRAX_MARKER_START/STOP work without it.
 *
 * If defined: Traxcope uses name, color, and thresholds for visualization.
 * If not defined: Traxcope auto-generates name ("MARKER_0xXXXX").
 *
 * @param _tid       Marker Trace ID (0x5000-0x5FFF)
 * @param _name      Human-readable name (max TRAX_CFG_META_NAME_LEN chars)
 * @param _color     Display color: TRAX_COLOR_RGB(r,g,b), TRAX_COLOR_xxx, or TRAX_COLOR_NONE
 * @param _wn_up_us  Upper warning in µs (0 = none)
 * @param _dl_up_us  Upper deadline in µs (0 = none)
 */
#if TRAX_ENABLE
#define TRAX_MARKER_DEFINE(_tid, _name, _color, _wn_up_us, _dl_up_us) \
    TRAX_STATIC_ASSERT(sizeof(_name) <= TRAX_CFG_META_NAME_LEN, \
        "Marker name exceeds TRAX_CFG_META_NAME_LEN"); \
    static const struct trax_marker_meta_t __trax_marker_meta_##_tid \
        TRAX_SECTION(TRAX_META_SECTION_MARKER) = { \
            .id = (_tid), \
            .reserved = 0, \
            .color = TRAX_COLOR_TO_RGB_(_color), \
            .p_name = _name, \
            .deadline_upper_us = (_dl_up_us), \
            .warn_upper_us     = (_wn_up_us) \
        }
#else
/* TraxProbe disabled: no marker metadata. Benign repeatable struct decl. */
#define TRAX_MARKER_DEFINE(_tid, _name, _color, _wn_up_us, _dl_up_us) struct trax_marker_meta_t
#endif

/*=============================================================================
 ====================VALIDATION================================================
 ============================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* TRAX_MARKER_H_ */
