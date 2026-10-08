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
 * @file           : trax_frame.c
 * @brief          : TraxProbe Frame State Implementation
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 * 
 * Federated architecture: Frame module owns transaction counter state.
 * 
 ******************************************************************************
 */

#include "trax_frame.h"

/* TraxProbe disabled (TRAX_ENABLE==0): whole unit compiles out. The frame state
 * is only touched by the TRAX_FRAME_* macros, which are no-op'd when disabled. */
#if TRAX_ENABLE

/*=============================================================================
 ====================GLOBAL VARIABLES==========================================
 ============================================================================*/

/**
 * @brief Transaction counter for frame sequencing
 * 
 * Incremented for each frame created. Used for:
 * - Frame ordering verification
 * - Lost frame detection
 * - Wraparound is handled automatically (uint16_t)
 * 
 * Accessed directly by TRAX_FRAME_ALLOC macros for performance.
 */
uint16_t trax_trans_cntr = 0;

/*=============================================================================
 ====================GLOBAL FUNCTION IMPLEMANTATION============================
 ============================================================================*/

/**
 * @brief Initialize frame module
 * 
 * Resets transaction counter to 0.
 */
void trax_frame_init(void)
{
    trax_trans_cntr = 0;
}

/**
 * @brief Reset frame module for trace restart
 * 
 * Resets transaction counter to 0.
 * Equivalent to trax_frame_init() today, but kept separate
 * so the restart path uses a consistent _reset() convention.
 */
void trax_frame_reset(void)
{
    trax_trans_cntr = 0;
}

#endif /* TRAX_ENABLE */
