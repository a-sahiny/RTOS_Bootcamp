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
 * @file           : trax_timebase.h
 * @brief          : TraxProbe timebase state (struct + extern declaration)
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 *
 * Deliberately minimal and dependency-free (stdint only) so that hardware
 * port headers can include it. The port timestamp helpers (e.g. the
 * TICK_TIMER trax_hw_port_timepacked_get32() inline) need to read
 * trax_timebase.tick_cntr, but the ports are included from trax_hw.h BEFORE
 * trax_timestamp.h — putting the type here breaks that cycle without pulling
 * timestamp API declarations into the ports.
 *
 * The full timestamp module interface lives in trax_timestamp.h, which
 * includes this header.
 ******************************************************************************
 */

#ifndef TRAX_TIMEBASE_H_
#define TRAX_TIMEBASE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Internal timebase state (tick counters for time synchronization)
 *
 * Not to be confused with the decoded wall-clock time computed by the
 * human-readable device time.
 *
 * FREERUN mode: tick_cntr is unused (reserved).
 *               tick_overflow_cntr tracks full 32-bit counter wraps.
 * TICK_TIMER mode: tick_cntr is a software counter 0 to (2^TICK_BITS - 1).
 *                  tick_overflow_cntr increments when tick_cntr wraps.
 */
/* All fields are volatile: they are written from the tick ISR
 * (trax_timestamp_tick / trax_timestamp_poll) and read from task context —
 * trax_timestamp_get_us() reads them OUTSIDE any critical section. Without
 * volatile, whole-program optimization (-flto) is allowed to cache the
 * fields in registers across calls (e.g. an application loop polling
 * trax_timestamp_get_us() against a deadline would never observe the ISR's
 * increments). Inside the frame path the reads sit behind critical-section
 * compiler barriers anyway, so the volatile costs nothing there. */
struct trax_timebase_t {
  volatile uint32_t tick_cntr; /**< TICK_TIMER: software tick 0..(2^TICK_BITS-1).
                         FREERUN: unused. */
  volatile uint32_t tick_overflow_cntr; /**< Increments when tick_cntr wraps (or
                                  freerun counter wraps) */
  volatile uint32_t last_timer_val;     /**< Previous timer value for wrap detection */
};

/** @brief Global timebase state instance (defined in trax_timestamp.c) */
extern struct trax_timebase_t trax_timebase;

#ifdef __cplusplus
}
#endif

#endif /* TRAX_TIMEBASE_H_ */
