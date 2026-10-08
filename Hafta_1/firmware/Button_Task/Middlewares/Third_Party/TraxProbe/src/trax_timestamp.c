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
 * @file           : trax_timestamp.c
 * @brief          : TraxProbe Timestamp Module Implementation
 * @version        : 2.0.0
 ******************************************************************************
 * @attention
 *
 * Handles time tracking for TraxProbe. Supports two modes:
 *   - FREERUN: 32-bit free-running HW counter (e.g., DWT, mcycle)
 *   - TICK_TIMER: [tick:TICK_BITS][fine:(32-TICK_BITS)] with software tick
 * counter
 *
 * Configurable tick_bits split and timer direction. Traxcope detects
 * timestamp wraps from the data stream — periodic sync frames are not sent.
 *
 ******************************************************************************
 */

#include "trax_timestamp.h"
#include "trax_frame.h"
#include "internal/trax_timestamp_wrap.h"
#include "trax_hw.h"

/* TraxProbe disabled (TRAX_ENABLE==0): whole unit compiles out. The timestamp
 * entry points are provided as inline no-op stubs in trax_timestamp.h. */
#if TRAX_ENABLE

/*=============================================================================
 ====================GLOBAL VARIABLES==========================================
 ============================================================================*/

struct trax_timebase_t trax_timebase = {0};

/*=============================================================================
 ====================GLOBAL FUNCTION IMPLEMENTATION============================
 ============================================================================*/

void trax_timestamp_init(void) {
  trax_timebase.tick_cntr = 0;
  trax_timebase.tick_overflow_cntr = 0;
  trax_timebase.last_timer_val = 0;
}

#if (TRAX_CFG_TIMESTAMP_MODE == TRAX_TIMESTAMP_FREERUN)
void trax_timestamp_poll(void) {
  uint32_t current_val = TRAX_HW_PORT_FREERUN_COUNTER;

#if (TRAX_CFG_TIMESTAMP_TIMER_DIR == TRAX_TIMER_DIR_UP)
  if (current_val < trax_timebase.last_timer_val) {
    trax_timebase.tick_overflow_cntr++;
    trax_timestamp_wrap_emit(trax_timebase.tick_overflow_cntr);
  }
#else
  if (current_val > trax_timebase.last_timer_val) {
    trax_timebase.tick_overflow_cntr++;
    trax_timestamp_wrap_emit(trax_timebase.tick_overflow_cntr);
  }
#endif

  trax_timebase.last_timer_val = current_val;
}
#endif

void trax_timestamp_tick(void) {
#if (TRAX_CFG_TIMESTAMP_MODE == TRAX_TIMESTAMP_FREERUN)
  trax_timestamp_poll();
#else
  trax_timebase.tick_cntr++;

  if (trax_timebase.tick_cntr >= TRAX_CFG_TICK_OVERFLOW_PERIOD) {
    trax_timebase.tick_cntr = 0;
    trax_timebase.tick_overflow_cntr++;

    trax_timestamp_wrap_emit(trax_timebase.tick_overflow_cntr);
  }
#endif
}

uint64_t trax_timestamp_get_us(void) {
  uint64_t total_cycles;

#if (TRAX_CFG_TIMESTAMP_MODE == TRAX_TIMESTAMP_FREERUN)
  /* Snapshot the wrap count BEFORE the counter so a wrap that fires
   * between the two reads yields a stale-but-correct value (~one 2^32
   * cycle period in the past) rather than getting double-counted.
   * Callers wanting the freshest wrap count should invoke
   * trax_timestamp_poll() immediately before. */
  uint32_t wraps   = trax_timebase.tick_overflow_cntr;
  uint32_t counter = TRAX_HW_PORT_FREERUN_COUNTER;

#if (TRAX_CFG_TIMESTAMP_TIMER_DIR == TRAX_TIMER_DIR_DOWN)
  /* Down-counter: convert "cycles remaining" into "cycles elapsed within
   * the current 2^32 wrap". ~counter == (UINT32_MAX - counter). */
  uint32_t fine = ~counter;
#else
  uint32_t fine = counter;
#endif

  total_cycles = ((uint64_t)wraps << 32) | fine;

#else /* TRAX_TIMESTAMP_TICK_TIMER */
  /* Match the field order used by TRAX_HW_PORT_TIMEPACKED_GET32 so any
   * racing tick ISR shows up as a (slightly stale) value the host-side
   * MessageDecoder already knows how to correct. The intermediate
   * __DMB() is intentionally omitted — this is not the trace fast path
   * and the call already costs a function frame. */
  uint32_t wraps = trax_timebase.tick_overflow_cntr;
  uint32_t ticks = trax_timebase.tick_cntr;
  uint32_t timer = TRAX_CFG_TIMESTAMP_TIMER_VAL;

#if (TRAX_CFG_TIMESTAMP_TIMER_DIR == TRAX_TIMER_DIR_DOWN)
  /* Down-counter: TIMER_VAL is cycles REMAINING until the next reload. */
  uint32_t fine = (TRAX_CFG_TICK_COUNTER_PERIOD - 1U) - timer;
#else
  uint32_t fine = timer;
#endif

  uint64_t total_ticks =
      (uint64_t)wraps * (uint64_t)TRAX_CFG_TICK_OVERFLOW_PERIOD + ticks;
  total_cycles =
      total_ticks * (uint64_t)TRAX_CFG_TICK_COUNTER_PERIOD + fine;
#endif

  /* cycles -> microseconds, accounting for the optional rational divisor:
   *     us = total_cycles * 1e6 * FREQ_DIV / FREQ_HZ
   *
   * Decompose total_cycles = q * FREQ_HZ + r so the intermediate product
   * stays inside uint64_t even for multi-year up-times at high counter
   * rates (e.g., 480 MHz freerun on an STM32H7). */
  const uint64_t freq_hz  = (uint64_t)TRAX_CFG_TIMER_FREQ_HZ;
  const uint64_t freq_div = (uint64_t)TRAX_CFG_TIMER_FREQ_DIV;
  const uint64_t q        = total_cycles / freq_hz;
  const uint64_t r        = total_cycles % freq_hz;

  return q * 1000000ULL * freq_div + (r * 1000000ULL * freq_div) / freq_hz;
}

#endif /* TRAX_ENABLE */
