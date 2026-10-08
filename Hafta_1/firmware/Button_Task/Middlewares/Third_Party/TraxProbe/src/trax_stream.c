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
 * @file           : trax_stream.c
 * @brief          : TraxProbe Stream State Management Implementation
 * @version        : 1.0.0
 ******************************************************************************
 */

#include "trax_stream.h"
#include "trax_data_types.h"
#include "trax_meta_type.h"
#include <string.h>

/* TraxProbe disabled (TRAX_ENABLE==0): whole unit compiles out. The var-stream
 * runtime macros are no-op'd in trax_stream.h when disabled. */
#if TRAX_ENABLE

/*=============================================================================
 ====================GLOBAL STREAM STATE ARRAY=================================
 =============================================================================*/

/**
 * @brief Global stream state array
 *
 * Only instantiated when TRAX_CFG_STREAM_CNT > 0 (defined in trax_config.h).
 * Initialized to zero by C standard (static storage duration).
 */
#if (TRAX_CFG_STREAM_CNT > 0)
struct trax_stream_t p_trax_stream_list[TRAX_CFG_STREAM_CNT] = {0};
#endif

/*=============================================================================
 ====================META SECTION SYMBOLS======================================
 =============================================================================*/

/* Linker-provided bounds of the var-meta and stream-meta sections.  Walked
 * once at init to pre-compute per-stream sequence_size_bytes. */
#if (TRAX_CFG_STREAM_CNT > 0)
extern const struct trax_var_meta_t              __trax_var_start[];
extern const struct trax_var_meta_t              __trax_var_end[];
extern const struct trax_stream_adc_group_meta_t __trax_stream_adc_start[];
extern const struct trax_stream_adc_group_meta_t __trax_stream_adc_end[];
extern const struct trax_stream_rotor_meta_t     __trax_stream_rotor_start[];
extern const struct trax_stream_rotor_meta_t     __trax_stream_rotor_end[];

/**
 * @brief Look up a VAR's data type by TID via the .trax_var meta section.
 *
 * Linear scan — only called from init, never from hot paths.
 * Returns TRAX_DATA_TYPE_UINT16 as a safe default if the TID is not
 * registered in the var meta (matches the host-side fallback).
 */
static uint8_t lookup_var_data_type(uint16_t var_tid)
{
	const struct trax_var_meta_t *p_meta;
	for (p_meta = __trax_var_start; p_meta < __trax_var_end; p_meta++) {
		if (p_meta->id == var_tid) {
			return p_meta->data_type;
		}
	}
	return TRAX_DATA_TYPE_UINT16;
}

/**
 * @brief Pre-compute the power-of-two shift for a sequence size.
 *
 * TRAX_STREAM_UPDATE runs in the ISR that feeds the stream, where a uint64_t
 * division costs a ~40-100 cycle __aeabi_uldivmod call on Cortex-M.  Nearly
 * every real schema has a power-of-two sequence size, so resolve log2 once
 * here and let the macro shift.
 *
 * Size 0 (a TID with no schema in the meta section) maps to shift 0 rather
 * than the divider sentinel, so a stray UPDATE on an unresolved stream
 * produces a wrong index instead of a divide-by-zero inside an ISR.  Such a
 * stream can never transmit: trax_stream_activate() refuses to activate it.
 */
static uint8_t sequence_size_to_shift(uint16_t size_bytes)
{
	uint8_t shift;

	if (size_bytes == 0u) {
		return 0u;
	}
	if ((size_bytes & (uint16_t)(size_bytes - 1u)) != 0u) {
		return TRAX_STREAM_SHIFT_NONE;
	}
	for (shift = 0u; ((uint16_t)1u << shift) != size_bytes; shift++) {
	}
	return shift;
}
#endif

/*=============================================================================
 ====================STREAM INITIALIZATION=====================================
 =============================================================================*/

/**
 * @brief Initialize all stream states
 *
 * Zeroes all stream state structures, then walks the .trax_stream_adc
 * meta section and computes each stream's sequence_size_bytes by summing
 * TRAX_DATA_TYPE_SIZE() over its channel-bound vars (resolved via the
 * .trax_var meta section).  Because sequence size is a stream-lifetime
 * constant, doing this once at init means TRAX_STREAM_START stays a
 * cheap critical-section path with no meta walk.
 *
 * Also resolves sequence_size_shift so TRAX_STREAM_UPDATE can shift rather
 * than call the 64-bit divider from the stream's ISR.
 *
 * Called from trax_init() during system initialization.
 */
void trax_stream_init(void)
{
#if (TRAX_CFG_STREAM_CNT > 0)
	memset(p_trax_stream_list, 0, sizeof(p_trax_stream_list));

	const struct trax_stream_adc_group_meta_t *p_stream;
	for (p_stream = __trax_stream_adc_start;
	     p_stream < __trax_stream_adc_end;
	     p_stream++) {
		uint16_t total = 0;
		uint8_t i;
		for (i = 0; i < p_stream->channel_count; i++) {
			uint16_t var_id = p_stream->p_channels[i].var_id;
			if (var_id == 0u) {
				continue;
			}
			uint8_t dtype = lookup_var_data_type(var_id);
			total += (uint16_t)TRAX_DATA_TYPE_SIZE(dtype);
		}

		uint32_t idx = TRAX_STREAM_TID_INDEX(p_stream->id);
		if (idx < (uint32_t)TRAX_CFG_STREAM_CNT) {
			p_trax_stream_list[idx].sequence_size_bytes = total;
			p_trax_stream_list[idx].sequence_size_shift =
				sequence_size_to_shift(total);
		}
	}

	/* ROTOR streams: single position channel — one sequence is one sample
	 * of the bound VAR's data type. */
	const struct trax_stream_rotor_meta_t *p_rotor;
	for (p_rotor = __trax_stream_rotor_start;
	     p_rotor < __trax_stream_rotor_end;
	     p_rotor++) {
		uint8_t dtype = lookup_var_data_type(p_rotor->p_channels[0].var_id);

		uint32_t idx = TRAX_STREAM_TID_INDEX(p_rotor->id);
		if (idx < (uint32_t)TRAX_CFG_STREAM_CNT) {
			uint16_t seq_size = (uint16_t)TRAX_DATA_TYPE_SIZE(dtype);

			p_trax_stream_list[idx].sequence_size_bytes = seq_size;
			p_trax_stream_list[idx].sequence_size_shift =
				sequence_size_to_shift(seq_size);
		}
	}
#endif
}

#if (TRAX_CFG_STREAM_CNT > 0)
/* Snapshot buffer owned by this module — no caller-side stack cost. */
static struct trax_stream_runtime_meta_t s_runtime_snapshot[TRAX_CFG_STREAM_CNT];
#endif

const struct trax_stream_runtime_meta_t *
trax_stream_snapshot_runtime_meta(uint8_t *p_count)
{
#if (TRAX_CFG_STREAM_CNT > 0)
	uint8_t i;
	for (i = 0; i < TRAX_CFG_STREAM_CNT; i++) {
		s_runtime_snapshot[i].stream_id                = (uint16_t)(TRAX_TID_RANGE_STREAM_START + i);
		s_runtime_snapshot[i].is_active                = p_trax_stream_list[i].is_active ? 1u : 0u;
		s_runtime_snapshot[i].reserved                 = 0;
		s_runtime_snapshot[i].start_tick_overflow_cntr = p_trax_stream_list[i].start_tick_overflow_cntr;
		s_runtime_snapshot[i].start_timepacked         = p_trax_stream_list[i].start_timepacked;
	}
	*p_count = (uint8_t)TRAX_CFG_STREAM_CNT;
	return s_runtime_snapshot;
#else
	*p_count = 0u;
	return NULL;
#endif
}

#endif /* TRAX_ENABLE */
