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
 * @file           : trax_timestamp.h
 * @brief          : TraxProbe Timestamp Module Interface
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 *
 * This module handles time synchronization between TraxProbe and Traxcope.
 * Traxcope performs data-driven wrap detection from the timestamp stream,
 * so periodic sync/timestamp frames are no longer required.
 *
 * Key functions:
 *   - trax_timestamp_init(): Initialize timestamp module
 *   - trax_timestamp_poll(): FREERUN — poll for 32-bit counter wrap (any context)
 *   - trax_timestamp_tick(): Periodic ISR entry point (any timestamp mode)
 *
 * Time-to-seconds conversion is NOT done on the MCU — the PC (Traxcope)
 * reconstructs wall-clock time from raw timestamps + metadata using double
 * precision. This avoids float precision loss on MCU.
 ******************************************************************************
 */

#ifndef TRAX_TIMESTAMP_H_
#define TRAX_TIMESTAMP_H_

#include "trax_config_default.h"
#include "trax_timebase.h"   /* struct trax_timebase_t + extern trax_timebase */
#include "trax_hw.h"
#include "trax_utility.h"


#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 ====================GLOBAL MACRO DEFINITIONS==================================
 ============================================================================*/

/* Note: TRAX_TIMESTAMP_VAL_MASK, TRAX_TIMESTAMP_TICK_MASK,
 * TRAX_TIMESTAMP_TICK_SHIFT are defined in trax_config_hw_port.h */

/** @brief Write packed timestamp into frame buffer (platform-specific) */
#define TRAX_FRAME_TIMEPACKED_PUT(p_wr) TRAX_HW_PORT_TIMEPACKED_PUT32(p_wr)

/** @brief Read packed timestamp from hardware (platform-specific) */
#define TRAX_FRAME_TIMEPACKED_GET() TRAX_HW_PORT_TIMEPACKED_GET32()

/*=============================================================================
 ====================GLOBAL TYPEDEF============================================
 ============================================================================*/

/* struct trax_timebase_t and the extern trax_timebase declaration live in
 * trax_timebase.h (included above) so that hardware port headers — which are
 * processed before this file — can define inline timestamp helpers that read
 * trax_timebase.tick_cntr. */

/*=============================================================================
 ====================GLOBAL FUNCTION DECLARATIONS==============================
 ============================================================================*/

/**
 * @brief Initialize timestamp module
 *
 * Resets tick and tick overflow counters to 0.
 * Call this during system initialization.
 */
#if TRAX_ENABLE
void trax_timestamp_init(void);
#else
static inline void trax_timestamp_init(void) { }
#endif

#if (TRAX_CFG_TIMESTAMP_MODE == TRAX_TIMESTAMP_FREERUN)
/**
 * @brief Poll for 32-bit free-running counter wrap (FREERUN mode only)
 *
 * Reads the hardware counter and compares with the previous value to detect
 * rollover. Safe to call from any context (main loop, periodic task, RTOS
 * hook). Must be called faster than the 32-bit counter wraps.
 *
 * Must be called from exactly ONE context to avoid racing on trax_timebase.
 */
#if TRAX_ENABLE
void trax_timestamp_poll(void);
#else
static inline void trax_timestamp_poll(void) { }
#endif
#endif

/**
 * @brief Advance timebase from a periodic ISR (any timestamp mode)
 *
 * FREERUN mode:    Delegates to trax_timestamp_poll() for wrap detection.
 * TICK_TIMER mode: Increments the software tick counter. Each call IS one
 *                  tick — must be called exactly once per timer period.
 *
 * Call from a periodic ISR (e.g., SysTick_Handler). Do NOT call from the
 * main loop in TICK_TIMER mode.
 *
 * Must be called from exactly ONE context to avoid racing on trax_timebase.
 */
#if TRAX_ENABLE
void trax_timestamp_tick(void);
#else
static inline void trax_timestamp_tick(void) { }
#endif

/**
 * @brief Get monotonic elapsed time in microseconds since trax_timestamp_init()
 *
 * Combines the wrap counter, software tick counter (TICK_TIMER mode only)
 * and the fine timer register into a single 64-bit microsecond value, then
 * normalizes the result against TRAX_CFG_TIMER_FREQ_HZ / TRAX_CFG_TIMER_FREQ_DIV.
 * Works transparently for both UP and DOWN counter directions.
 *
 * Resolution:
 *   FREERUN    — one fine-timer cycle (1 / TRAX_CFG_TIMER_FREQ_HZ s).
 *   TICK_TIMER — same, modulo the tick-boundary race below.
 *
 * Call context:
 *   The reads of trax_timebase and the timer register are NOT atomic. If
 *   the timer wraps between them this function may return a value about
 *   one tick period in the past (TICK_TIMER) or one 2^32-cycle period in
 *   the past (FREERUN). Same trade-off as TRAX_HW_PORT_TIMEPACKED_GET32:
 *   fine for application timing (delays, profiling, watchdogs), not a
 *   hard monotonicity source.
 *
 *   FREERUN callers must also invoke trax_timestamp_poll() at least as
 *   often as the 32-bit counter wraps so the wrap count stays fresh.
 *
 * @return Elapsed microseconds since boot. Wraps after ~584,942 years.
 */
#if TRAX_ENABLE
uint64_t trax_timestamp_get_us(void);
#else
static inline uint64_t trax_timestamp_get_us(void) { return 0; }
#endif

#ifdef __cplusplus
}
#endif

#endif /* TRAX_TIMESTAMP_H_ */
