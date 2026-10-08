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
 * @file           : trax_sm.c
 * @brief          : TraxProbe State Machine Runtime State Implementation
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 *
 * Owns the per-SM current-state shadow table maintained by
 * TRAX_SM_SET_STATE and snapshotted into SESSION_START / SESSION_GAP
 * frames (late-join + gap-resync support).  Mirrors the var-stream
 * runtime-meta pattern in trax_stream.c.
 *
 ******************************************************************************
 */

#include "trax_sm.h"
#include <string.h>

/* TraxProbe disabled (TRAX_ENABLE==0): whole unit compiles out. */
#if TRAX_ENABLE

/*=============================================================================
 ====================GLOBAL SM STATE TABLE=====================================
 =============================================================================*/

#if (TRAX_CFG_SM_CNT > 0)
volatile uint8_t p_trax_sm_state_list[TRAX_CFG_SM_CNT];

/* Snapshot buffer owned by this module — no caller-side stack cost. */
static struct trax_sm_runtime_meta_t s_sm_runtime_snapshot[TRAX_CFG_SM_CNT];
#endif

/*=============================================================================
 ====================GLOBAL FUNCTION IMPLEMENTATION============================
 =============================================================================*/

void trax_sm_init(void)
{
#if (TRAX_CFG_SM_CNT > 0)
	/* Cast drops volatile: boot context, no concurrent producers yet. */
	memset((void *)p_trax_sm_state_list, (int)TRAX_SM_STATE_UNKNOWN,
	       sizeof(p_trax_sm_state_list));
#endif
}

const struct trax_sm_runtime_meta_t *
trax_sm_snapshot_runtime_meta(uint8_t *p_count)
{
#if (TRAX_CFG_SM_CNT > 0)
	uint8_t i;
	for (i = 0; i < TRAX_CFG_SM_CNT; i++) {
		s_sm_runtime_snapshot[i].sm_index      = (uint16_t)i;
		s_sm_runtime_snapshot[i].current_state = p_trax_sm_state_list[i];
		s_sm_runtime_snapshot[i].reserved      = 0u;
	}
	*p_count = (uint8_t)TRAX_CFG_SM_CNT;
	return s_sm_runtime_snapshot;
#else
	*p_count = 0u;
	return NULL;
#endif
}

#endif /* TRAX_ENABLE */
