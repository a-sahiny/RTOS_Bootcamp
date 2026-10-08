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
 * @file           : trax_timestamp_wrap.h
 * @brief          : TraxProbe Inline Timestamp Wrap Module
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 *
 * This module encapsulates the logic for emitting a timestamp resync frame
 * (TRAX_TID_TIMESTAMP) whenever the timebase wrap counter advances — i.e.
 * a tick-period rollover (TICK_TIMER mode) or a 32-bit counter wrap (FREERUN
 * mode). These frames are the host's ground-truth resync points; they are
 * event-driven on wrap, not a periodic keep-alive. By keeping this inline,
 * we decouple protocol structures from the timestamp module without sacrificing
 * ISR execution speed.
 *
 ******************************************************************************
 */

#ifndef TRAX_TIMESTAMP_WRAP_H_
#define TRAX_TIMESTAMP_WRAP_H_

#include "trax_frame.h"
#include "trax_session.h"
#include "trax_tid.h"
#include "trax_hw.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Emits a timestamp resync frame on a timebase wrap.
 *        This function is inline to guarantee zero-overhead ISR execution.
 *
 * @param wrap_cntr The current timebase wrap (overflow) count.
 */
static inline void trax_timestamp_wrap_emit(uint32_t wrap_cntr)
{
    if (TRAX_IS_SESSION_ACTIVE()) {
        uint32_t ts = TRAX_HW_PORT_TIMEPACKED_GET32();
        TRAX_FRAME_ARGS_PROTOCOL(TRAX_TID_TIMESTAMP, ts, wrap_cntr);
    }
}

#ifdef __cplusplus
}
#endif

#endif /* TRAX_TIMESTAMP_WRAP_H_ */
