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
 * @file           : trax_isr.h
 * @brief          : TraxProbe ISR Tracing API (Metadata + Runtime)
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 *
 * ISR tracing records interrupt handler entry/exit events for timeline
 * visualization in Traxcope. Each ISR gets a unique TID in the ISR range
 * (0x4000-0x4FFF).
 *
 * USAGE:
 *   // 1. Define ISR metadata (optional, for rich display in Traxcope)
 *   TRAX_ISR_DEFINE(TRAX_TID_TIMER_ISR, "Timer1_ISR", 3);
 *
 *   // 2. Instrument ISR handlers at runtime
 *   void Timer1_IRQHandler(void) {
 *       BaseType_t woken = pdFALSE;
 *       TRAX_ISR_ENTER(TRAX_TID_TIMER_ISR);
 *       // ... ISR code, maybe xSemaphoreGiveFromISR(..., &woken) ...
 *       portYIELD_FROM_ISR(woken);
 *       TRAX_ISR_END(TRAX_TID_TIMER_ISR, woken);  // EXIT, or yield-to-scheduler
 *   }
 *
 ******************************************************************************
 */

#ifndef TRAX_ISR_H_
#define TRAX_ISR_H_

#include <stdint.h>
#include "trax_data_types.h"      /* TRAX_SECTION, TRAX_STATIC_ASSERT, TRAX_PACKED */
#include "trax_tid.h"             /* TRAX_TID_RANGE_ISR_START / _END for range checks */
#include "trax_meta_type.h"       /* struct trax_isr_meta_t */
#include "trax_meta_section.h"    /* TRAX_META_SECTION_ISR */
#include "trax_config_default.h"  /* TRAX_CFG_META_NAME_LEN */
#include "trax_frame.h"           /* TRAX_FRAME_ARGS_ATOMIC */

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 ====================ISR METADATA==============================================
 ============================================================================*/

/**
 * @brief Define an ISR with metadata (stored in ELF .trax_isr section)
 *
 * THIS MACRO IS OPTIONAL. TRAX_ISR_ENTER/EXIT work without it.
 *
 * If defined: Traxcope uses name and priority for rich timeline display.
 * If not defined: Traxcope auto-generates a name ("ISR_0xXXXX", or "Tick"
 * for the library-reserved TRAX_TID_ISR_TICK).
 *
 * Usage:
 *   TRAX_ISR_DEFINE(TRAX_TID_TIMER_ISR, "Timer1_ISR", 3);
 *
 * @param _tid      Trace ID (unique identifier)
 * @param _name     ISR name (max TRAX_CFG_META_NAME_LEN chars)
 * @param _priority Raw NVIC priority value as you programmed it via
 *                  HAL_NVIC_SetPriority() / NVIC_SetPriority(). DO NOT
 *                  translate or invert. Traxcope sorts swimlanes correctly
 *                  using the PriorityOrder bits reported in START_TRACE
 *                  metadata — for Cortex-M (isrAscending=false) lower
 *                  values sort higher in the lane list, matching the
 *                  hardware convention "0 = most urgent, 15 = least
 *                  urgent". Examples: SysTick under HAL bare-metal is
 *                  typically 0 (highest); SysTick under FreeRTOS is the
 *                  lowest implemented NVIC priority — on Cortex-M0+/G0
 *                  (__NVIC_PRIO_BITS==2) that is 3; on classic 4-bit NVIC
 *                  it is often 15. Prefer reporting the value you passed
 *                  to HAL_NVIC_SetPriority() / the port's lowest priority,
 *                  not a hard-coded "15". User DMA / TIM ISRs use whatever
 *                  NVIC value bsp_*_init() programmed (often 5–10 on
 *                  chips with more priority bits).
 *
 * @note Compile error if name exceeds configured limit.
 */
#if TRAX_ENABLE
#define TRAX_ISR_DEFINE(_tid, _name, _priority) \
    TRAX_STATIC_ASSERT(sizeof(_name) <= TRAX_CFG_META_NAME_LEN, \
        "ISR name exceeds TRAX_CFG_META_NAME_LEN"); \
    static const struct trax_isr_meta_t __trax_isr_meta_##_tid \
        TRAX_SECTION(TRAX_META_SECTION_ISR) = { \
            .id = (_tid), \
            .priority = (_priority), \
            .p_name = _name \
        }
#else
/* TraxProbe disabled: no ISR metadata. Benign repeatable struct decl. */
#define TRAX_ISR_DEFINE(_tid, _name, _priority) struct trax_isr_meta_t
#endif

/*=============================================================================
 ====================ISR RUNTIME MACROS========================================
 ============================================================================*/

/** @brief ISR enter/exit action codes (param[0] in ISR frames) */
#define TRAX_ISR_ACTION_ENTER  0
#define TRAX_ISR_ACTION_EXIT   1

/**
 * @brief Record ISR entry — call at the start of an ISR handler
 *
 * Usage:
 *   void Timer1_IRQHandler(void) {
 *       TRAX_ISR_ENTER(TRAX_TID_TIMER_ISR);
 *       // ... ISR code ...
 *       TRAX_ISR_END(TRAX_TID_TIMER_ISR, woken);  // or TRAX_ISR_EXIT if no yield
 *   }
 */
#if TRAX_ENABLE
#define TRAX_ISR_ENTER(_tid) \
    do { \
        TRAX_STATIC_ASSERT( \
            (_tid) >= TRAX_TID_RANGE_ISR_START && (_tid) <= TRAX_TID_RANGE_ISR_END, \
            "TRAX_ISR_ENTER: TID is outside the ISR range (0x4000-0x4FFF)"); \
        TRAX_FRAME_ARGS_ATOMIC((_tid), TRAX_ISR_ACTION_ENTER) \
    } while(0)

/** @brief Record ISR exit — call at the end of an ISR handler */
#define TRAX_ISR_EXIT(_tid) \
    do { \
        TRAX_STATIC_ASSERT( \
            (_tid) >= TRAX_TID_RANGE_ISR_START && (_tid) <= TRAX_TID_RANGE_ISR_END, \
            "TRAX_ISR_EXIT: TID is outside the ISR range (0x4000-0x4FFF)"); \
        TRAX_FRAME_ARGS_ATOMIC((_tid), TRAX_ISR_ACTION_EXIT) \
    } while(0)

/**
 * @brief ISR exit when a context switch is already pending
 *
 * Same role as FreeRTOS `traceISR_EXIT_TO_SCHEDULER`.  When
 * `TRAX_CFG_ISR_YIELD_TO_SCHEDULER` is 1
 * (default) no frame is emitted: Traxcope keeps this ISR open until
 * SWITCH_OUT and draws ISR → Scheduler → new task, without a 2–5 µs
 * resume of the preempted task.  When the config is 0 this is identical
 * to TRAX_ISR_EXIT.
 *
 * Prefer TRAX_ISR_END() at the handler tail so the woken flag chooses
 * the path in one call.
 */
#if TRAX_CFG_ISR_YIELD_TO_SCHEDULER
#define TRAX_ISR_EXIT_TO_SCHEDULER(_tid) \
    do { \
        TRAX_STATIC_ASSERT( \
            (_tid) >= TRAX_TID_RANGE_ISR_START && (_tid) <= TRAX_TID_RANGE_ISR_END, \
            "TRAX_ISR_EXIT_TO_SCHEDULER: TID is outside the ISR range (0x4000-0x4FFF)"); \
        (void)(_tid); \
    } while(0)
#else
#define TRAX_ISR_EXIT_TO_SCHEDULER(_tid)  TRAX_ISR_EXIT(_tid)
#endif

/**
 * @brief Close an ISR, collapsing to the scheduler when a switch is pending
 *
 * Close the ISR with the same woken flag passed to portYIELD_FROM_ISR:
 *
 *   portYIELD_FROM_ISR(woken);
 *   TRAX_ISR_END(TRAX_TID_UART, woken);
 *
 * @param _tid              ISR trace ID (same as TRAX_ISR_ENTER)
 * @param _switch_required  Non-zero if portYIELD_FROM_ISR / equivalent pended
 */
#define TRAX_ISR_END(_tid, _switch_required) \
    do { \
        if ((_switch_required) != 0) { \
            TRAX_ISR_EXIT_TO_SCHEDULER(_tid); \
        } else { \
            TRAX_ISR_EXIT(_tid); \
        } \
    } while(0)
#else
#define TRAX_ISR_ENTER(_tid) ((void)0)
#define TRAX_ISR_EXIT(_tid)  ((void)0)
#define TRAX_ISR_EXIT_TO_SCHEDULER(_tid) ((void)0)
#define TRAX_ISR_END(_tid, _switch_required) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* TRAX_ISR_H_ */
