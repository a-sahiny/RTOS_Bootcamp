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
 * @file           : trax_main_loop.h
 * @brief          : TraxProbe Bare-Metal Main-Loop Boundary API
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 *
 * For bare-metal (NORTOS) firmware, TraceView has no natural slice
 * boundaries on the "main" lane — without ISRs or task switches, the
 * main entity would be one open-ended slice from boot to disconnect,
 * which renders as nothing useful.
 *
 * TRAX_MAIN_LOOP_BEGIN/END bracket each iteration of the main while-loop
 * so TraceView can:
 *   - Draw one slice per iteration on the "main" lane.
 *   - Show idle / sleep gaps (between END and the next BEGIN) as visible
 *     empty space — a free CPU-utilisation indicator for bare-metal.
 *   - Anchor LOG / VAR / MARKER events on a real slice (event markers
 *     need a slice to attach to).
 *
 * For RTOS builds these macros are no-ops on the host (the SliceTracker
 * sees that the base entity is IDLE not MAIN and silently ignores them).
 * Safe to leave in code that is later ported to RTOS.
 *
 * USAGE:
 *   int main(void) {
 *       bsp_init();
 *       trax_init();
 *       trax_wait_session_started();
 *
 *       for (;;) {
 *           TRAX_MAIN_LOOP_BEGIN();
 *           sense();
 *           compute();
 *           actuate();
 *           trax_process();
 *           TRAX_MAIN_LOOP_END();
 *           __WFI();   // sleep — visible as a gap on TraceView's main lane
 *       }
 *   }
 *
 ******************************************************************************
 */

#ifndef TRAX_MAIN_LOOP_H_
#define TRAX_MAIN_LOOP_H_

#include "trax_config_default.h" /* TRAX_ENABLE master switch */
#include "trax_tid.h"     /* TRAX_TID_MAIN_LOOP, TRAX_MAIN_LOOP_ACTION_BEGIN/END */
#include "trax_frame.h"   /* TRAX_FRAME_ARGS_ATOMIC */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Mark the start of one bare-metal main-loop iteration.
 *
 * Place at the top of the for(;;) / while(1) body. Closes any open
 * iteration (defensive — handles missing END) and opens a new "main"
 * slice on the host SliceTracker.
 *
 * Wire frame: 4 bytes (TRAX_TID_MAIN_LOOP, action=BEGIN). Same shape as
 * TRAX_ISR_ENTER, so cost is one atomic ring-buffer write per iteration.
 *
 * No-op on the host for RTOS builds (host detects MAIN-vs-IDLE base
 * entity and silently discards).
 */
#if TRAX_ENABLE
#define TRAX_MAIN_LOOP_BEGIN() \
    do { \
        TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_MAIN_LOOP, TRAX_MAIN_LOOP_ACTION_BEGIN) \
    } while (0)
#else
#define TRAX_MAIN_LOOP_BEGIN() ((void)0)
#endif

/**
 * @brief Mark the end of the current bare-metal main-loop iteration.
 *
 * Closes the slice opened by TRAX_MAIN_LOOP_BEGIN. Time between END and
 * the next BEGIN renders as empty space on the main lane, surfacing
 * sleep / __WFI / HAL_Delay periods as a visible idle gap.
 *
 * Calling END without a prior BEGIN is silently ignored by the host
 * (no slice is created), so misordered instrumentation is harmless.
 */
#if TRAX_ENABLE
#define TRAX_MAIN_LOOP_END() \
    do { \
        TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_MAIN_LOOP, TRAX_MAIN_LOOP_ACTION_END) \
    } while (0)
#else
#define TRAX_MAIN_LOOP_END() ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* TRAX_MAIN_LOOP_H_ */
