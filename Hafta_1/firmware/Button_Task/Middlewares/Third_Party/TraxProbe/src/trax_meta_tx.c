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
 * @file           : trax_meta_tx_new.c
 * @date        	  : Jan 18, 2026
 * @author         : bemre
 * @version        : TODO
 *
 * @brief          : TODO
 ******************************************************************************
 * @attention
 * 	TODO
 ******************************************************************************
 */

/* Standard C headers */
#include <stdint.h>
#include <string.h>

/* TraxProbe headers */
#include "trax_meta_tx.h"
#include "trax_frame.h"          /* TRAX_FRAME_ARGS, TRAX_FRAME_RAW_ARGS */
#include "trax_config_default.h" /* TRAX_CFG_META_STORAGE, etc. */
#include "trax_meta_type.h"      /* Metadata structures */
#include "trax_session.h"        /* trax_session, TRAX_IS_SESSION_ACTIVE() */
#include "trax_timestamp.h"     /* trax_timebase, TRAX_FRAME_TIMEPACKED_GET/PUT */
#include "trax_tid.h"            /* TRAX_TID_xxx constants */
#include "trax_buffer.h"         /* trax_buffer, TRAX_BUFF_ALLOC */
#include "trax_utility.h"        /* TRAX_PUT32 */
#include "trax_hw.h"  /* TRAX_PORT_ENTER/EXIT_CRITICAL_SECTION,
                                TRAX_CFG_ISR_PRIO_ASCENDING (per-platform) */
#include "trax_rtos_port.h"  /* TRAX_CFG_TASK_PRIO_ASCENDING (per-RTOS) */
#include "trax_marker.h"        /* trax_marker_meta_t */
#include "internal/trax_memory.h"        /* TRAX_CFG_HEAP_WARN/CRITICAL, TRAX_CFG_STACK_* */
#include "trax_sync.h"          /* trax_sync_get_role(), TRAX_SYNC_ROLE_* */
#include "trax_diag.h"          /* trax_diag_build_report, trax_diag_report_t */
#include "trax_fault.h"         /* trax_fault_emit_pending() */

#include "../os/common/trax_rtos_tables.h"
#include "trax_stream.h"         /* p_trax_stream_list, TRAX_CFG_STREAM_CNT */
#include "trax_sm.h"             /* trax_sm_snapshot_runtime_meta, TRAX_CFG_SM_CNT */
#include "trax_trigger.h"        /* trax_trigger_meta_t (static trigger metadata) */

/* TraxProbe disabled (TRAX_ENABLE==0): whole unit compiles out. Metadata-TX
 * entry points are not referenced once the public API is no-op'd. */
#if TRAX_ENABLE


/*=============================================================================
 ====================LOCAL MACRO FUNCTIONS=====================================
 ============================================================================*/

/*=============================================================================
 ====================LOCAL MACRO DEFINITIONS===================================
 ============================================================================*/

/*=============================================================================
 ====================GLOBAL VARIABLES==========================================
 ============================================================================*/
#if (TRAX_CFG_META_STORAGE == TRAX_META_IN_FLASH)
/* Metadata sections (linker-provided symbols) - only when metadata is in Flash */
extern const struct trax_var_meta_t __trax_var_start[];
extern const struct trax_var_meta_t __trax_var_end[];
extern const struct trax_log_meta_t __trax_log_start[];
extern const struct trax_log_meta_t __trax_log_end[];
extern const struct trax_log_vars_meta_t __trax_log_vars_start[];
extern const struct trax_log_vars_meta_t __trax_log_vars_end[];
extern const struct trax_stream_adc_group_meta_t __trax_stream_adc_start[];
extern const struct trax_stream_adc_group_meta_t __trax_stream_adc_end[];
extern const struct trax_stream_rotor_meta_t __trax_stream_rotor_start[];
extern const struct trax_stream_rotor_meta_t __trax_stream_rotor_end[];
extern const struct trax_isr_meta_t __trax_isr_start[];
extern const struct trax_isr_meta_t __trax_isr_end[];
extern const struct trax_marker_meta_t __trax_marker_start[];
extern const struct trax_marker_meta_t __trax_marker_end[];
extern const struct trax_sm_wire_t __trax_sm_start[];
extern const struct trax_sm_wire_t __trax_sm_end[];
extern const struct trax_trigger_meta_t __trax_trigger_start[];
extern const struct trax_trigger_meta_t __trax_trigger_end[];
extern const struct trax_var_formula_meta_t __trax_var_formula_start[];
extern const struct trax_var_formula_meta_t __trax_var_formula_end[];
extern const struct trax_var_filter_meta_t __trax_var_filter_start[];
extern const struct trax_var_filter_meta_t __trax_var_filter_end[];
#endif /* TRAX_CFG_META_STORAGE == TRAX_META_IN_FLASH */



/*=============================================================================
 ====================LOCAL STUCTURES===========================================
 ============================================================================*/

/**
 * @brief SESSION_START frame header — private to this translation unit.
 *
 * Not exposed in the header because no other module needs to construct or
 * interpret this struct; only trax_meta_tx_send_start() builds and writes it.
 */
struct trax_session_header_t {
	uint32_t keyword;             /**< TRAX_SESSION_KEYWORD ("TRAX") - for sync detection */
	uint32_t tick_overflow_cntr;  /**< Tick overflow counter at start (adjacent to keyword for pattern) */
	uint32_t version;             /**< Protocol version */
	uint32_t timer_freq_hz;       /**< Timer frequency numerator in Hz */
	uint32_t timer_freq_div;      /**< Timer frequency divisor (actual_freq = freq_hz / div, 0 treated as 1) */
	uint32_t tick_counter_period; /**< Fine ticks per tick increment (DWT: 2^fine_bits, TICK_TIMER: user-defined) */
	uint32_t session_id;          /**< Session identifier (increments each start) */
	uint16_t var_meta_count;      /**< Number of VAR metadata entries in payload */
	uint16_t log_meta_count;      /**< Number of LOG metadata entries in payload */
	uint16_t log_vars_meta_count; /**< Number of log VAR bindings entries in payload */
	uint8_t  stream_adc_group_meta_count; /**< Number of ADC_GROUP stream entries in payload */
	uint8_t  priority_config;     /**< [0]=task_prio_ascending, [1]=isr_prio_ascending, [7:2]=reserved */
	uint16_t isr_meta_count;      /**< Number of ISR metadata entries in payload */
	uint8_t  stream_runtime_meta_count; /**< Number of stream runtime state entries in payload */
	uint8_t  rtos_task_meta_count;     /**< Number of RTOS task metadata entries in payload */
	uint8_t  rtos_object_meta_count;   /**< Number of RTOS object metadata entries in payload */
	uint8_t  marker_meta_count;        /**< Number of marker metadata entries in payload */
	uint8_t  sm_meta_count;            /**< Number of state machine metadata entries in payload */
	uint8_t  timestamp_config;   /**< [4:0]=tick_bits, [5]=direction(0=UP,1=DOWN), [7:6]=reserved */
	uint8_t  core_count;          /**< Number of CPU cores */
	/* OS Information */
	uint8_t  os_type;             /**< OS/RTOS type (TRAX_RTOS_NONE, TRAX_RTOS_FREERTOS, ...) */
	uint8_t  os_ver_major;        /**< OS/RTOS version major */
	uint8_t  os_ver_minor;        /**< OS/RTOS version minor */
	uint8_t  os_ver_patch;        /**< OS/RTOS version patch */
	char     project_name[TRAX_CFG_META_PROJECT_LEN]; /**< Project name (null-terminated if shorter) */
	char     build_date[12];          /**< Build date __DATE__ (e.g., "Jan 18 2026") */
	char     build_time[12];          /**< Build time __TIME__ (e.g., "14:30:00") */
	char     build_version[TRAX_CFG_META_VERSION_LEN]; /**< Application build version (TRAX_CFG_BUILD_VERSION).
	                                                        Free-form semver-style label, e.g. "1.4.2-rc3".
	                                                        Empty string = unversioned. */
	char     build_id[TRAX_CFG_META_BUILD_ID_LEN];     /**< Application build identifier
	                                                        (TRAX_CFG_BUILD_ID).  Vendor-neutral fingerprint
	                                                        — git short SHA, SVN rev, hash of .elf, etc.
	                                                        Defaults to __DATE__ "T" __TIME__ if unset. */
	uint8_t  var_formula_meta_count;  /**< Number of VAR formula metadata entries in payload */
	uint8_t  var_filter_meta_count;   /**< Number of VAR filter (transfer function) metadata entries in payload.
	                                       Filters are stream-vars-only on the host side; firmware emits whatever
	                                       TRAX_VAR_FILTER() declarations exist regardless of var kind, and
	                                       the host applies the stream-var gate at IDataSource::addVarFilter time. */
	/* Memory monitoring thresholds */
	uint32_t heap_total_size;         /**< configTOTAL_HEAP_SIZE in bytes (0 = heap monitoring disabled) */
	uint32_t heap_warn_bytes;         /**< Warning threshold: free < this triggers warning zone */
	uint32_t heap_critical_bytes;     /**< Critical threshold: free < this triggers danger zone */
	uint32_t current_heap_free;       /**< Initial heap free amount at stream start */
	uint8_t  stack_warn_percent;      /**< Warning % of stack remaining (0 = disabled) */
	uint8_t  stack_critical_percent;  /**< Critical % of stack remaining (0 = disabled) */
	uint8_t  reserved_diag;           /**< Reserved byte slot (was diag_enabled before
	                                       diagnostics became mandatory in v3.0.0).
	                                       Firmware always writes 1 so legacy hosts that
	                                       still read this field as a feature flag keep
	                                       seeing "diag is on".  Kept in the wire layout
	                                       to preserve the 160-byte header offset table. */
	uint8_t  sync_role;               /**< Multi-probe sync role (TRAX_SYNC_ROLE_NONE / MASTER /
	                                       SLAVE) baked in at compile time via TRAX_CFG_SYNC_ROLE.
	                                       Host auto-designates the MASTER probe as the workspace
	                                       reference clock — no user action required. Defaults to
	                                       NONE on existing single-probe builds (back-compat). */
	uint8_t  stream_rotor_meta_count; /**< Number of ROTOR stream entries in payload.  Rotor
	                                       metadata is appended LAST (after var_filter) so older
	                                       host parsers simply stop reading before it.  Stole the
	                                       final reserved_mem byte (was [3]→[2]→[1]→gone); the
	                                       header stays a multiple of 4 bytes, total stays 200. */
	/* Sync line characterisation (grouped with sync_role above).  Kept
	 * adjacent to the role byte so the entire "what the firmware says
	 * about the SYNC bus" block lives in one place — easier to extend
	 * (e.g. polarity flags) without scattering related fields across
	 * the header.  delay_seconds = num / den; den == 0 means "not
	 * reported" and the host falls back to the user override (or 0).
	 * Bytes are still stolen from snapshot_reserved[3] -> [1] below to
	 * keep the total header size at 160. */
	uint32_t sync_filter_delay_num;   /**< Sync input filter delay numerator (seconds).
	                                       Sourced from TRAX_CFG_SYNC_FILTER_DELAY_NUM. */
	uint32_t sync_filter_delay_den;   /**< Sync input filter delay denominator (seconds). */
	/* Memory snapshot (since-boot historical state — let the host
	 * present truthful "since boot" memory health on a mid-flight
	 * connect instead of restarting all counters at zero).  Each field
	 * is set to 0 by the port when its underlying RTOS / heap variant
	 * cannot supply the value; the host treats 0 as "unknown / not
	 * supported" and falls back to host-derived stats. */
	uint32_t heap_min_ever_free;      /**< Lowest-ever free heap (xPortGetMinimumEverFreeHeapSize) */
	uint32_t heap_largest_free_block; /**< Largest contig free block (vPortGetHeapStats — heap_4/5 only) */
	uint32_t heap_alloc_count;        /**< Successful pvPortMalloc() calls since boot */
	uint32_t heap_free_count;         /**< vPortFree() calls since boot */
	uint32_t heap_alloc_fail_count;   /**< traceMALLOC_FAILED counts since boot */
	uint8_t  sm_runtime_meta_count;   /**< Number of SM runtime state entries in payload
	                                       (trax_sm_runtime_meta_t, appended LAST — same
	                                       additive-extension rule as var_filter/rotor).
	                                       Stole snapshot_reserved byte 0; header stays 200. */
	uint8_t  trigger_meta_count;      /**< Number of trigger metadata entries in payload
	                                       (trax_trigger_meta_t, after stream_rotor / before
	                                       sm_runtime). Stole snapshot_reserved byte 1. */
	uint8_t  snapshot_reserved[2];    /**< Future growth (kept zero today). */
} TRAX_PACKED;

/**
 * @brief TRAX_TID_SESSION_GAP frame header — private to this translation unit.
 *
 * Emitted by trax_meta_tx_send_gap() when streaming resumes after a
 * ring-overflow gate. The host (Traxcope WireStreamGapHeader)
 * parses this in lockstep — bump both together on any layout change.
 *
 * Followed in the frame body by word-padded sections (same wire structs
 * as SESSION_START, in this order):
 *   1. stream_runtime_meta_count × struct trax_stream_runtime_meta_t
 *   2. rtos_task_meta_count      × struct trax_rtos_task_wire_t
 *   3. rtos_object_meta_count    × struct trax_rtos_object_wire_t
 *   4. sm_runtime_meta_count     × struct trax_sm_runtime_meta_t
 */
struct trax_gap_header_t {
	uint32_t gap_start_tick_overflow_cntr; /**< Timebase overflow counter at first failed alloc */
	uint32_t gap_start_timepacked;         /**< Packed timestamp at first failed alloc */
	uint32_t gap_end_tick_overflow_cntr;   /**< Timebase overflow counter at resume — the host MUST
	                                            re-anchor from this: timer wraps during the gate were
	                                            silent (wrap detection is data-driven on the host) */
	uint32_t gap_end_timepacked;           /**< Packed timestamp at resume */
	uint32_t dropped_frames;               /**< Failed frame allocations during this gap episode */
	uint32_t gap_seq;                      /**< Gap episode number (cumulative since boot, 1-based) */
	uint32_t skipped_events;               /**< Producer frames suppressed at the streaming gate while
	                                            this episode drained (approximate lower bound — see
	                                            trax_session.gap_skip_cntr). dropped_frames only counts
	                                            the failed ALLOCS; this counts everything silently
	                                            skipped after the gate flipped. */
	uint8_t  stream_runtime_meta_count;    /**< Entries in section 1 */
	uint8_t  rtos_task_meta_count;         /**< Entries in section 2 */
	uint8_t  rtos_object_meta_count;       /**< Entries in section 3 */
	uint8_t  sm_runtime_meta_count;        /**< Entries in section 4 (was reserved) */

	/* Resume attribution (pause / trigger extension — trax_trigger.h).
	 * reason == TRAX_RESUME_REASON_OVERFLOW for classic accordion gaps;
	 * the trigger_* fields are zero unless reason == _TRIGGER. */
	uint8_t  resume_reason;                /**< enum trax_resume_reason_t */
	uint8_t  reserved;                     /**< Zero — future flags */
	uint16_t trigger_tid;                  /**< Trigger TID that released the pause (0 = none) */
	uint32_t trigger_ctx;                  /**< User context word from TRAX_TRIGGER_FIRE */
	uint32_t trigger_tick_overflow_cntr;   /**< Timebase overflow counter at fire */
	uint32_t trigger_timepacked;           /**< Packed timestamp at fire — the EXACT trigger
	                                            time; this frame itself is emitted up to one
	                                            trax_process() pass later */
} TRAX_PACKED;

TRAX_STATIC_ASSERT(sizeof(struct trax_gap_header_t) == 48,
                   "trax_gap_header_t must be 48 bytes (bump host WireStreamGapHeader in lockstep)");

/* Header grew 160 -> 200 with the build_version[16] + build_id[24] block.
 * build_id is 24 instead of 16 to comfortably fit the unset-fallback
 * `__DATE__"T"__TIME__` ("May  7 2026T15:48:33" = 20 chars + NUL) plus
 * a few bytes of headroom for prefixes like "ci-" or "v".  Bumped in
 * lockstep with WireSessionHeader on the host; mismatch = silent parse
 * drift (every later metadata section starts at the wrong offset). */
TRAX_STATIC_ASSERT(sizeof(struct trax_session_header_t) == 200,
                   "trax_session_header_t must be 200 bytes (build_version[16] + build_id[24] appended after build_time; bump host WireSessionHeader in lockstep)");

/*=============================================================================
 ====================LOCAL VARIABLES===========================================
 ============================================================================*/

/*=============================================================================
 ====================LOCAL FUNCTION DECLARATION================================
 ============================================================================*/
/**
 * @brief Get metadata counts for each type
 */
static void get_meta_counts(uint16_t *p_var_meta_count, uint16_t *p_log_meta_count,
	uint16_t *p_log_vars_meta_count, uint8_t *p_stream_adc_meta_count,
	uint16_t *p_isr_meta_count, uint8_t *p_marker_meta_count, uint8_t *p_sm_meta_count,
	uint8_t *p_var_formula_meta_count, uint8_t *p_var_filter_meta_count,
	uint8_t *p_stream_rotor_meta_count, uint8_t *p_trigger_meta_count);

static uint32_t get_sm_wire_size(void);


/*=============================================================================
 ====================GLOBAL FUNCTION IMPLEMANTATION============================
 ============================================================================*/
void trax_meta_tx_init(void)
{
	/* Nothing to initialize */
}

int32_t trax_meta_tx_send_start(void)
{
	/* Send TRAX_TID_SESSION_START frame containing:
	 *   1. Header with sync data (magic, version, freq, session_id, tick_overflow_cntr, timestamp)
	 *   2. Metadata counts
	 *   3. All static metadata (vars, logs, streams, ISRs)
	 *   4. Stream runtime state
	 *   5. RTOS task metadata (from dynamic task table)
	 *   6. RTOS object metadata (from dynamic object table)
	 *
	 * Transaction counter is forced to 0 for frame synchronization.
	 * This single frame replaces the old multi-frame metadata sequence.
	 */

	/* Get metadata counts */
	uint16_t var_meta_count, log_meta_count, log_vars_meta_count, isr_meta_count;
	uint8_t stream_adc_meta_count, stream_rotor_meta_count;
	uint8_t marker_meta_count, sm_meta_count, var_formula_meta_count, var_filter_meta_count;
	uint8_t trigger_meta_count;
	get_meta_counts(&var_meta_count, &log_meta_count, &log_vars_meta_count,
	                &stream_adc_meta_count, &isr_meta_count, &marker_meta_count, &sm_meta_count,
	                &var_formula_meta_count, &var_filter_meta_count,
	                &stream_rotor_meta_count, &trigger_meta_count);

	uint8_t rtos_task_meta_count = trax_os_get_task_count();
	uint8_t rtos_object_meta_count = trax_os_get_object_count();

	/* Stream runtime metadata: snapshot pointer is owned by the stream
	 * module — no stack buffer here, no preprocessor guards, fully portable. */
	uint8_t stream_runtime_meta_count;
	const struct trax_stream_runtime_meta_t *stream_runtime =
		trax_stream_snapshot_runtime_meta(&stream_runtime_meta_count);

	/* SM runtime state: current state per SM (late-join support — the host
	 * seeds SM lanes from the live state instead of the declared initial). */
	uint8_t sm_runtime_meta_count;
	const struct trax_sm_runtime_meta_t *sm_runtime =
		trax_sm_snapshot_runtime_meta(&sm_runtime_meta_count);

	/* Calculate sizes in bytes */
	uint32_t header_size = sizeof(struct trax_session_header_t);
	uint32_t vars_size = var_meta_count * sizeof(struct trax_var_meta_t);
	uint32_t logs_size = log_meta_count * sizeof(struct trax_log_meta_t);
	uint32_t log_vars_size = log_vars_meta_count * sizeof(struct trax_log_vars_meta_t);
	uint32_t stream_adc_size = stream_adc_meta_count * sizeof(struct trax_stream_adc_group_meta_t);
	uint32_t isrs_size = isr_meta_count * sizeof(struct trax_isr_meta_t);
	uint32_t markers_size = marker_meta_count * sizeof(struct trax_marker_meta_t);
	uint32_t stream_runtime_size = stream_runtime_meta_count * sizeof(struct trax_stream_runtime_meta_t);
	uint32_t rtos_task_size = rtos_task_meta_count * sizeof(struct trax_rtos_task_wire_t);
	uint32_t rtos_object_size = rtos_object_meta_count * sizeof(struct trax_rtos_object_wire_t);
	uint32_t sm_wire_size = get_sm_wire_size();
	uint32_t var_formula_size = var_formula_meta_count * sizeof(struct trax_var_formula_meta_t);
	uint32_t var_filter_size = var_filter_meta_count * sizeof(struct trax_var_filter_meta_t);
	uint32_t stream_rotor_size = stream_rotor_meta_count * sizeof(struct trax_stream_rotor_meta_t);
	uint32_t trigger_meta_size = trigger_meta_count * sizeof(struct trax_trigger_meta_t);
	uint32_t sm_runtime_size = sm_runtime_meta_count * sizeof(struct trax_sm_runtime_meta_t);

	/* Convert each section to 32-bit words (rounded up individually) 
	 * Each section is written with word-aligned padding, so allocation 
	 * must account for padding per section, not just total bytes */
	uint16_t header_words = (uint16_t)((header_size + 3) / 4);
	uint16_t vars_words = (uint16_t)((vars_size + 3) / 4);
	uint16_t logs_words = (uint16_t)((logs_size + 3) / 4);
	uint16_t log_vars_words = (uint16_t)((log_vars_size + 3) / 4);
	uint16_t stream_adc_words = (uint16_t)((stream_adc_size + 3) / 4);
	uint16_t isrs_words = (uint16_t)((isrs_size + 3) / 4);
	uint16_t markers_words = (uint16_t)((markers_size + 3) / 4);
	uint16_t stream_runtime_words = (uint16_t)((stream_runtime_size + 3) / 4);
	uint16_t rtos_task_words = (uint16_t)((rtos_task_size + 3) / 4);
	uint16_t rtos_object_words = (uint16_t)((rtos_object_size + 3) / 4);
	uint16_t sm_wire_words = (uint16_t)((sm_wire_size + 3) / 4);
	uint16_t var_formula_words = (uint16_t)((var_formula_size + 3) / 4);
	uint16_t var_filter_words = (uint16_t)((var_filter_size + 3) / 4);
	uint16_t stream_rotor_words = (uint16_t)((stream_rotor_size + 3) / 4);
	uint16_t trigger_meta_words = (uint16_t)((trigger_meta_size + 3) / 4);
	uint16_t sm_runtime_words = (uint16_t)((sm_runtime_size + 3) / 4);
	uint16_t total_param_count = header_words + vars_words + logs_words + 
	                             log_vars_words + stream_adc_words +
	                             isrs_words + markers_words + stream_runtime_words +
	                             rtos_task_words + rtos_object_words + sm_wire_words +
	                             var_formula_words + var_filter_words +
	                             stream_rotor_words + trigger_meta_words +
	                             sm_runtime_words;

	/* Allocate frame with transaction counter = 0 for sync */
	uint32_t *p_frame_start = NULL;
	uint32_t *p_wr;
	uint16_t frame_size32 = total_param_count + TRAX_CFG_FRAME_MIN_SIZE32;
	uint16_t saved_trans_cntr;
	uint32_t tick_overflow_cntr_captured;

	TRAX_PORT_ENTER_CRITICAL_SECTION {
		TRAX_BUFF_ALLOC(p_wr, frame_size32);
		if (p_wr != NULL) {
			p_frame_start = p_wr;
			/* Capture tick overflow counter atomically with allocation */
			trax_trans_cntr = 0;
			saved_trans_cntr = trax_trans_cntr;
			trax_trans_cntr++;
			tick_overflow_cntr_captured = trax_timebase.tick_overflow_cntr;
			p_wr++; /* Skip Word 0 — written during commit */
			TRAX_FRAME_TIMEPACKED_PUT(p_wr);
			TRAX_PUT32(p_wr, TRAX_TID_SESSION_START);
		}
	}
	TRAX_PORT_EXIT_CRITICAL_SECTION

	if (p_frame_start == NULL) {
		return -1;  /* Allocation failed — streaming NOT enabled */
	}

	/* Buffer space reserved. Enable streaming so ISR/task trace frames
	 * are appended after the start frame, and dynamic RTOS metadata
	 * (tasks, objects) reflects the live state at this point. */
	trax_session_enable_streaming();

	/* Build and write header */
	static struct trax_session_header_t header;
	header.keyword = TRAX_SESSION_KEYWORD;  /* For sync detection and endianness */
	header.tick_overflow_cntr = tick_overflow_cntr_captured;  /* Captured in critical section */
	header.version = TRAX_VERSION;
	header.timer_freq_hz = TRAX_CFG_TIMER_FREQ_HZ;
	header.timer_freq_div = TRAX_CFG_TIMER_FREQ_DIV;
	header.tick_counter_period = TRAX_CFG_TICK_COUNTER_PERIOD;
	header.session_id = trax_session.session_id;
	header.var_meta_count = var_meta_count;
	header.log_meta_count = log_meta_count;
	header.log_vars_meta_count = log_vars_meta_count;
	header.stream_adc_group_meta_count = stream_adc_meta_count;
	header.priority_config = (uint8_t)((TRAX_CFG_TASK_PRIO_ASCENDING & 0x01U)
	                        | ((TRAX_CFG_ISR_PRIO_ASCENDING & 0x01U) << 1));
	header.isr_meta_count = isr_meta_count;
	header.stream_runtime_meta_count = stream_runtime_meta_count;
	header.rtos_task_meta_count = rtos_task_meta_count;
	header.rtos_object_meta_count = rtos_object_meta_count;
	header.marker_meta_count = marker_meta_count;
	header.sm_meta_count = sm_meta_count;
	header.timestamp_config = (uint8_t)((TRAX_CFG_TICK_BITS & 0x1FU)
	                        | ((TRAX_CFG_TIMESTAMP_TIMER_DIR & 0x01U) << 5));
	header.core_count = trax_os_get_core_count();
	/* OS/RTOS information (auto-detected or user-configured) */
	header.os_type = TRAX_CFG_RTOS_TYPE;
	header.os_ver_major = TRAX_CFG_OS_VER_MAJOR;
	header.os_ver_minor = TRAX_CFG_OS_VER_MINOR;
	header.os_ver_patch = TRAX_CFG_OS_VER_PATCH;
	trax_strncpy_pad(header.project_name, TRAX_CFG_PROJECT_NAME, TRAX_CFG_META_PROJECT_LEN);
	trax_strncpy_pad(header.build_date, __DATE__, sizeof(header.build_date));
	trax_strncpy_pad(header.build_time, __TIME__, sizeof(header.build_time));
	/* Application identity — opaque strings the host renders verbatim in
	 * compliance reports + the probe-config tab.  Padded with NULs so the
	 * host's QString::fromLatin1(strnlen(...)) always terminates cleanly. */
	trax_strncpy_pad(header.build_version, TRAX_CFG_BUILD_VERSION, TRAX_CFG_META_VERSION_LEN);
	trax_strncpy_pad(header.build_id,      TRAX_CFG_BUILD_ID,      TRAX_CFG_META_BUILD_ID_LEN);
	header.var_formula_meta_count = var_formula_meta_count;
	header.var_filter_meta_count = var_filter_meta_count;
	header.stream_rotor_meta_count = stream_rotor_meta_count;
	header.trigger_meta_count = trigger_meta_count;
	header.sm_runtime_meta_count = sm_runtime_meta_count;

	/* Memory monitoring thresholds */
	header.heap_total_size        = trax_os_get_total_heap_size();
	header.heap_warn_bytes        = (uint32_t)TRAX_CFG_HEAP_WARN_BYTES;
	header.heap_critical_bytes    = (uint32_t)TRAX_CFG_HEAP_CRITICAL_BYTES;
	header.current_heap_free      = trax_os_get_free_heap_size();
	header.stack_warn_percent     = (uint8_t)TRAX_CFG_STACK_WARN_PERCENT;
	header.stack_critical_percent = (uint8_t)TRAX_CFG_STACK_CRITICAL_PERCENT;
	/* Tell the host whether the firmware was built with the diagnostic
	 * module enabled — the UI uses this to show or hide the live
	 * Diagnostics panel and to decide whether the absence of
	 * TRAX_TID_DIAG_REPORT frames is a configuration choice (off) or a
	 * symptom (probe stuck / ring full). */
	header.reserved_diag          = 1u;  /* diag is always on (v3.0.0+) */
	/* Multi-probe sync role — compile-time constant from trax_sync.h. The
	 * host reads this byte to decide which probe (if any) to designate as
	 * the workspace reference clock. */
	header.sync_role              = trax_sync_get_role();
	/* Sync input filter delay (compile-time rational seconds = num / den).
	 * Filled here — right next to sync_role — to mirror the wire layout
	 * (the two fields sit side by side in trax_session_header_t).  The
	 * host caches the converted Core::Timestamp once at session-start in
	 * Session::m_firmwareSyncFilterDelay and applies it to every
	 * timestamp via IDataSource::toTime() so probe-local timelines align
	 * with the master's view of the SYNC edge. */
	header.sync_filter_delay_num   = TRAX_CFG_SYNC_FILTER_DELAY_NUM;
	header.sync_filter_delay_den   = TRAX_CFG_SYNC_FILTER_DELAY_DEN;

	/* Memory snapshot — since-boot historical state.  Each accessor
	 * returns 0 when its underlying heap variant cannot supply the
	 * value (e.g. heap_1/2/3 lacks vPortGetHeapStats); the host renders
	 * 0 as "unknown / n/a" and falls back to host-derived stats. */
	header.heap_min_ever_free      = trax_os_get_min_ever_free_heap_size();
	header.heap_largest_free_block = trax_os_get_largest_free_heap_block();
	header.heap_alloc_count        = trax_os_get_heap_alloc_count();
	header.heap_free_count         = trax_os_get_heap_free_count();
	header.heap_alloc_fail_count   = trax_os_get_heap_alloc_fail_count();
	memset(header.snapshot_reserved, 0, sizeof(header.snapshot_reserved));

	/* Write header to frame */
	memcpy(p_wr, &header, header_size);
	p_wr += header_words;

#if (TRAX_CFG_META_STORAGE == TRAX_META_IN_FLASH)
	/* Write VAR metadata array */
	if (var_meta_count > 0) {
		memcpy(p_wr, __trax_var_start, vars_size);
		p_wr += vars_words;
	}

	/* Write log metadata array */
	if (log_meta_count > 0) {
		memcpy(p_wr, __trax_log_start, logs_size);
		p_wr += logs_words;
	}

	/* Write log VAR bindings array */
	if (log_vars_meta_count > 0) {
		memcpy(p_wr, __trax_log_vars_start, log_vars_size);
		p_wr += log_vars_words;
	}

	/* Write ADC_GROUP stream metadata array */
	if (stream_adc_meta_count > 0) {
		memcpy(p_wr, __trax_stream_adc_start, stream_adc_size);
		p_wr += stream_adc_words;
	}

	/* Write ISR metadata array */
	if (isr_meta_count > 0) {
		memcpy(p_wr, __trax_isr_start, isrs_size);
		p_wr += isrs_words;
	}

	/* Write marker metadata array */
	if (marker_meta_count > 0) {
		memcpy(p_wr, __trax_marker_start, markers_size);
		p_wr += markers_words;
	}

	/* Write SM metadata: for each unified struct, send only the header (36 B)
	 * followed by state_count state entries (40 B each), not the full array. */
	if (sm_meta_count > 0) {
		uint8_t *p_sm_byte = (uint8_t *)p_wr;
		const struct trax_sm_wire_t *sm = __trax_sm_start;
		while (sm < __trax_sm_end) {
			memcpy(p_sm_byte, sm, sizeof(struct trax_sm_meta_wire_t));
			p_sm_byte += sizeof(struct trax_sm_meta_wire_t);

			uint32_t states_size = sm->state_count * sizeof(struct trax_sm_state_entry_t);
			memcpy(p_sm_byte, sm->p_states, states_size);
			p_sm_byte += states_size;

			sm++;
		}
		p_wr += sm_wire_words;
	}
#else
	/* ELF-only mode: no metadata to send, suppress unused warnings */
	(void)vars_size;
	(void)logs_size;
	(void)log_vars_size;
	(void)stream_adc_size;
	(void)isrs_size;
	(void)markers_size;
	(void)sm_wire_size;
	(void)vars_words;
	(void)logs_words;
	(void)log_vars_words;
	(void)stream_adc_words;
	(void)isrs_words;
	(void)markers_words;
	(void)sm_wire_words;
#endif /* TRAX_CFG_META_STORAGE == TRAX_META_IN_FLASH */

	/* Write stream runtime metadata (always sent, regardless of TRAX_CFG_META_STORAGE) */
	if (stream_runtime_meta_count > 0) {
		memcpy(p_wr, stream_runtime, stream_runtime_size);
		p_wr += stream_runtime_words;
	}

	if (rtos_task_meta_count > 0) {
		trax_os_write_task_meta((uint8_t *)p_wr, rtos_task_meta_count);
		p_wr += rtos_task_words;
	}

	if (rtos_object_meta_count > 0) {
		trax_os_write_object_meta((uint8_t *)p_wr, rtos_object_meta_count);
		p_wr += rtos_object_words;
	}

#if (TRAX_CFG_META_STORAGE == TRAX_META_IN_FLASH)
	/* Write VAR formula metadata array */
	if (var_formula_meta_count > 0) {
		memcpy(p_wr, __trax_var_formula_start, var_formula_size);
		p_wr += var_formula_words;
	}

	/* Write VAR filter metadata array (after formulas, so adding filters
	 * does NOT shift the offset of any earlier section — important for
	 * older host parsers that haven't been updated yet to read past it). */
	if (var_filter_meta_count > 0) {
		memcpy(p_wr, __trax_var_filter_start, var_filter_size);
		p_wr += var_filter_words;
	}

	/* Write ROTOR stream metadata array (same additive-extension rule
	 * as var_filter above: older hosts stop reading once their known
	 * section list is exhausted). */
	if (stream_rotor_meta_count > 0) {
		memcpy(p_wr, __trax_stream_rotor_start, stream_rotor_size);
		p_wr += stream_rotor_words;
	}

	/* Write trigger metadata array (after rotor, before the SM runtime
	 * tail — same additive rule). */
	if (trigger_meta_count > 0) {
		memcpy(p_wr, __trax_trigger_start, trigger_meta_size);
		p_wr += trigger_meta_words;
	}
#else
	(void)var_formula_size;
	(void)var_formula_words;
	(void)var_filter_size;
	(void)var_filter_words;
	(void)stream_rotor_size;
	(void)stream_rotor_words;
	(void)trigger_meta_size;
	(void)trigger_meta_words;
#endif

	/* Write SM runtime state (always sent, regardless of TRAX_CFG_META_STORAGE
	 * — appended LAST so the section walk stays additive for the host). */
	if (sm_runtime_meta_count > 0) {
		memcpy(p_wr, sm_runtime, sm_runtime_size);
		p_wr += sm_runtime_words;
	}

	/* Commit frame: write Word 0 last as atomic commit marker */
	if (p_frame_start != NULL) {
		TRAX_PORT_ENTER_CRITICAL_SECTION {
			TRAX_PORT_COMMIT_BARRIER(); /* SMP: body visible before marker */
			*p_frame_start = ((uint32_t)frame_size32 << 16U) | saved_trans_cntr;
		}
		TRAX_PORT_EXIT_CRITICAL_SECTION;
	}

	/* Post-mortem context rides directly behind the SESSION_START frame:
	 * reset reason (every start) + recovered fault record (first start
	 * after a crash). Both callers of send_start (trax_session_start and
	 * the host CMD_START handler) funnel through here, so this is the
	 * single hook point. No-op stub when TRAX_CFG_FAULT_ENABLE == 0. */
	trax_fault_emit_pending();

	return 0;
}

int32_t trax_meta_tx_send_stop(enum trax_stop_reason_t reason, uint32_t info)
{
	/* TRAX_TID_SESSION_STOP frame body — fixed 96-byte layout:
	 *
	 *   word 0   : (uint32_t)reason
	 *   word 1   : (uint32_t)info
	 *   word 2.. : trax_diag_report_t autopsy snapshot (sizeof = 88 B = 22 w)
	 *
	 * Diag is mandatory (v3.0.0); every STOP frame carries an autopsy.
	 * The host parser rejects any other size as a wire-format mismatch.
	 *
	 * Embedding the snapshot directly in the STOP frame body makes it
	 * atomic with the stop reason: the two pieces of information cannot
	 * reach the host out of order, and the snapshot cannot fail to be
	 * allocated separately because there is no separate frame.  Without
	 * the embedded autopsy, the host could only show whatever periodic
	 * diag snapshot happened to arrive last — which after an early
	 * fault is typically the seq=1 baseline emitted right after
	 * stream-start with mostly-zero counters.
	 *
	 * The stop frame uses TRAX_FRAME_ARGS_PROTOCOL semantics (bypasses
	 * TRAX_IS_SESSION_ACTIVE() — must land even after a fatal stop has flipped
	 * streaming = 0). We open-code that pattern here because we need to
	 * append a memory tail (trax_diag_build_report writes into the frame
	 * body), which the variadic-args macros don't support.
	 *
	 * trax_diag_build_report() takes its own critical section to copy
	 * trax_diag_stats coherently and bump report_seq exactly once — same
	 * helper the periodic / immediate emit path uses, so the host parses
	 * the autopsy with the existing WireDiagReport code unchanged. */

	const uint16_t param_words  = 2u + (uint16_t)(
		(sizeof(struct trax_diag_report_t) + 3u) / 4u);
	const uint16_t frame_size32 = param_words + TRAX_CFG_FRAME_MIN_SIZE32;

	uint32_t *p_frame_start = NULL;
	uint32_t *p_wr;
	uint16_t  saved_trans_cntr = 0;

	TRAX_PORT_ENTER_CRITICAL_SECTION
		{
			TRAX_BUFF_ALLOC(p_wr, frame_size32);
			if (p_wr != NULL) {
				p_frame_start    = p_wr;
				saved_trans_cntr = trax_trans_cntr;
				trax_trans_cntr++;
				p_wr++; /* Word 0 written during commit */
				TRAX_FRAME_TIMEPACKED_PUT(p_wr);
				TRAX_PUT32(p_wr, (uint32_t)TRAX_TID_SESSION_STOP
				                 | TRAX_FRAME_CORE_ID_BITS());
				TRAX_PUT32(p_wr, (uint32_t)reason);
				TRAX_PUT32(p_wr, info);
			}
		}
	TRAX_PORT_EXIT_CRITICAL_SECTION

	if (p_frame_start == NULL) {
		/* Allocation failed even for the STOP frame — pathological case
		 * (the stop tail of trax_process() runs AFTER trax_send_frames
		 * has just drained the ring, so this should be unreachable in
		 * practice).  Returning non-zero gives the caller a chance to
		 * retry on the next tick via the pending-stop latch. */
		return -1;
	}

	/* Build the autopsy snapshot directly into the frame slot.  Critical
	 * section is taken inside the helper so the copy is atomic vs any
	 * concurrent record_write / diag_process call. */
	trax_diag_build_report(
		(struct trax_diag_report_t *)p_wr,
		TRAX_DIAG_RPT_FLAG_EVENT_FIRED);

	/* Commit the frame — Word 0 written last as the atomic visibility
	 * marker for the consumer.  Same pattern as trax_meta_tx_send_start. */
	TRAX_PORT_ENTER_CRITICAL_SECTION
		{
			TRAX_PORT_COMMIT_BARRIER(); /* SMP: body visible before marker */
			*p_frame_start = ((uint32_t)frame_size32 << 16U) | saved_trans_cntr;
		}
	TRAX_PORT_EXIT_CRITICAL_SECTION

	return 0;
}

int32_t trax_meta_tx_send_gap(void)
{
	/* Caller contract (trax_process): gap latch armed, ring quiescent.
	 * Same reserve → enable-streaming → fill → commit choreography as
	 * trax_meta_tx_send_start(), so ISR/task frames resuming the moment
	 * streaming flips back on land BEHIND this frame on the wire and
	 * the dynamic tables written below reflect the live state. */

	uint8_t rtos_task_meta_count   = trax_os_get_task_count();
	uint8_t rtos_object_meta_count = trax_os_get_object_count();

	uint8_t stream_runtime_meta_count;
	const struct trax_stream_runtime_meta_t *stream_runtime =
		trax_stream_snapshot_runtime_meta(&stream_runtime_meta_count);

	/* SM current states: transitions during the gate were suppressed at
	 * the streaming gate, but the shadow table kept tracking — this
	 * snapshot lets the host correct each SM lane at the gap boundary. */
	uint8_t sm_runtime_meta_count;
	const struct trax_sm_runtime_meta_t *sm_runtime =
		trax_sm_snapshot_runtime_meta(&sm_runtime_meta_count);

	uint32_t header_size         = sizeof(struct trax_gap_header_t);
	uint32_t stream_runtime_size = stream_runtime_meta_count * sizeof(struct trax_stream_runtime_meta_t);
	uint32_t rtos_task_size      = rtos_task_meta_count * sizeof(struct trax_rtos_task_wire_t);
	uint32_t rtos_object_size    = rtos_object_meta_count * sizeof(struct trax_rtos_object_wire_t);
	uint32_t sm_runtime_size     = sm_runtime_meta_count * sizeof(struct trax_sm_runtime_meta_t);

	/* Word-align each section individually (same rule as send_start). */
	uint16_t header_words         = (uint16_t)((header_size + 3) / 4);
	uint16_t stream_runtime_words = (uint16_t)((stream_runtime_size + 3) / 4);
	uint16_t rtos_task_words      = (uint16_t)((rtos_task_size + 3) / 4);
	uint16_t rtos_object_words    = (uint16_t)((rtos_object_size + 3) / 4);
	uint16_t sm_runtime_words     = (uint16_t)((sm_runtime_size + 3) / 4);
	uint16_t total_param_count    = header_words + stream_runtime_words +
	                                rtos_task_words + rtos_object_words +
	                                sm_runtime_words;

	uint32_t *p_frame_start = NULL;
	uint32_t *p_wr;
	uint16_t  frame_size32 = total_param_count + TRAX_CFG_FRAME_MIN_SIZE32;
	uint16_t  saved_trans_cntr = 0;
	uint32_t  gap_end_tick_overflow = 0;
	uint32_t  gap_end_timepacked = 0;

	TRAX_PORT_ENTER_CRITICAL_SECTION
		{
			TRAX_BUFF_ALLOC(p_wr, frame_size32);
			if (p_wr != NULL) {
				p_frame_start    = p_wr;
				saved_trans_cntr = trax_trans_cntr;
				trax_trans_cntr++;
				/* Gap-end anchor captured atomically with the
				 * allocation so it matches the frame's own
				 * header timestamp. */
				gap_end_tick_overflow = trax_timebase.tick_overflow_cntr;
				gap_end_timepacked    = TRAX_FRAME_TIMEPACKED_GET();
				p_wr++; /* Word 0 written during commit */
				TRAX_FRAME_TIMEPACKED_PUT(p_wr);
				TRAX_PUT32(p_wr, (uint32_t)TRAX_TID_SESSION_GAP
				                 | TRAX_FRAME_CORE_ID_BITS());
			}
		}
	TRAX_PORT_EXIT_CRITICAL_SECTION

	if (p_frame_start == NULL) {
		/* Ring was just verified empty, so this can only mean the
		 * resync payload exceeds the ring itself (dynamic tables
		 * bigger than TRAX_CFG_OUT_BUFFER_SIZE32). The alloc-fail
		 * handler re-armed the latch (first-wins keeps the original
		 * gap start anchor); the caller retries next tick. */
		return -1;
	}

	/* Snapshot the latch fields BEFORE re-enabling streaming: the
	 * instant streaming is back on, a new overflow episode could
	 * re-arm the latch and bump alloc_fail_cntr. */
	struct trax_gap_header_t header;
	header.gap_start_tick_overflow_cntr = trax_session.gap_start_tick_overflow;
	header.gap_start_timepacked         = trax_session.gap_start_timepacked;
	header.gap_end_tick_overflow_cntr   = gap_end_tick_overflow;
	header.gap_end_timepacked           = gap_end_timepacked;
	header.dropped_frames               = trax_buffer.alloc_fail_cntr -
	                                      trax_session.gap_start_fail_cntr;
	header.skipped_events               = trax_session.gap_skip_cntr -
	                                      trax_session.gap_start_skip_cntr;
	trax_session.gap_cntr++;
	header.gap_seq                      = trax_session.gap_cntr;
	header.stream_runtime_meta_count    = stream_runtime_meta_count;
	header.rtos_task_meta_count         = rtos_task_meta_count;
	header.rtos_object_meta_count       = rtos_object_meta_count;
	header.sm_runtime_meta_count        = sm_runtime_meta_count;

	/* Resume attribution — also snapshotted BEFORE re-enabling
	 * streaming (a new pause/trigger episode could start the moment
	 * the gate reopens). */
	header.resume_reason              = trax_session.resume_reason;
	header.reserved                   = 0u;
	if (trax_session.trigger_latched) {
		header.trigger_tid                 = trax_session.trigger_tid;
		header.trigger_ctx                 = trax_session.trigger_ctx;
		header.trigger_tick_overflow_cntr  = trax_session.trigger_tick_overflow;
		header.trigger_timepacked          = trax_session.trigger_timepacked;
	} else {
		header.trigger_tid                 = 0u;
		header.trigger_ctx                 = 0u;
		header.trigger_tick_overflow_cntr  = 0u;
		header.trigger_timepacked          = 0u;
	}
	/* Latch consumed — reset attribution to the overflow default for
	 * the next (possibly classic accordion) episode. */
	trax_session.trigger_latched = 0u;
	trax_session.resume_reason   = (uint8_t)TRAX_RESUME_REASON_OVERFLOW;

	/* Frame slot reserved — resume streaming. Clears the gap latch
	 * (see trax_session_enable_streaming) and lets producer frames
	 * queue up behind this one. */
	trax_session_enable_streaming();

	memcpy(p_wr, &header, header_size);
	p_wr += header_words;

	if (stream_runtime_meta_count > 0) {
		memcpy(p_wr, stream_runtime, stream_runtime_size);
		p_wr += stream_runtime_words;
	}

	if (rtos_task_meta_count > 0) {
		trax_os_write_task_meta((uint8_t *)p_wr, rtos_task_meta_count);
		p_wr += rtos_task_words;
	}

	if (rtos_object_meta_count > 0) {
		trax_os_write_object_meta((uint8_t *)p_wr, rtos_object_meta_count);
		p_wr += rtos_object_words;
	}

	if (sm_runtime_meta_count > 0) {
		memcpy(p_wr, sm_runtime, sm_runtime_size);
		p_wr += sm_runtime_words;
	}

	/* Commit — Word 0 last as the atomic visibility marker. */
	TRAX_PORT_ENTER_CRITICAL_SECTION
		{
			TRAX_PORT_COMMIT_BARRIER(); /* SMP: body visible before marker */
			*p_frame_start = ((uint32_t)frame_size32 << 16U) | saved_trans_cntr;
		}
	TRAX_PORT_EXIT_CRITICAL_SECTION

	return 0;
}

/*=============================================================================
 ====================LOCAL FUNCTION IMPLEMANTATION=============================
 ============================================================================*/

static void get_meta_counts(uint16_t *p_var_meta_count, uint16_t *p_log_meta_count,
	uint16_t *p_log_vars_meta_count, uint8_t *p_stream_adc_meta_count,
	uint16_t *p_isr_meta_count, uint8_t *p_marker_meta_count, uint8_t *p_sm_meta_count,
	uint8_t *p_var_formula_meta_count, uint8_t *p_var_filter_meta_count,
	uint8_t *p_stream_rotor_meta_count, uint8_t *p_trigger_meta_count)
{
#if (TRAX_CFG_META_STORAGE == TRAX_META_IN_FLASH)
	*p_var_meta_count = (uint16_t)(__trax_var_end - __trax_var_start);
	*p_log_meta_count = (uint16_t)(__trax_log_end - __trax_log_start);
	*p_log_vars_meta_count = (uint16_t)(__trax_log_vars_end - __trax_log_vars_start);
	*p_stream_adc_meta_count = (uint8_t)(__trax_stream_adc_end - __trax_stream_adc_start);
	*p_isr_meta_count = (uint16_t)(__trax_isr_end - __trax_isr_start);
	*p_marker_meta_count = (uint8_t)(__trax_marker_end - __trax_marker_start);
	*p_sm_meta_count = (uint8_t)(__trax_sm_end - __trax_sm_start);
	*p_var_formula_meta_count = (uint8_t)(__trax_var_formula_end - __trax_var_formula_start);
	*p_var_filter_meta_count = (uint8_t)(__trax_var_filter_end - __trax_var_filter_start);
	*p_stream_rotor_meta_count = (uint8_t)(__trax_stream_rotor_end - __trax_stream_rotor_start);
	*p_trigger_meta_count = (uint8_t)(__trax_trigger_end - __trax_trigger_start);
#else
	*p_var_meta_count = 0;
	*p_log_meta_count = 0;
	*p_log_vars_meta_count = 0;
	*p_stream_adc_meta_count = 0;
	*p_isr_meta_count = 0;
	*p_marker_meta_count = 0;
	*p_sm_meta_count = 0;
	*p_var_formula_meta_count = 0;
	*p_var_filter_meta_count = 0;
	*p_stream_rotor_meta_count = 0;
	*p_trigger_meta_count = 0;
#endif /* TRAX_CFG_META_STORAGE */
}

static uint32_t get_sm_wire_size(void)
{
#if (TRAX_CFG_META_STORAGE == TRAX_META_IN_FLASH)
	uint32_t total = 0;
	const struct trax_sm_wire_t *sm = __trax_sm_start;
	while (sm < __trax_sm_end) {
		total += sizeof(struct trax_sm_meta_wire_t) +
		         sm->state_count * sizeof(struct trax_sm_state_entry_t);
		sm++;
	}
	return total;
#else
	return 0;
#endif
}

#endif /* TRAX_ENABLE */

