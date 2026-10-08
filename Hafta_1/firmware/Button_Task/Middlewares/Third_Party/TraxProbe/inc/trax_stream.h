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
 * @file           : trax_stream.h
 * @brief          : Variable-stream state management for TraxProbe
 * @version        : 2.0.0
 ******************************************************************************
 * @attention
 * 
 * This module manages per-variable stream state for TraxProbe.
 * 
 * FEATURES:
 * - Automatic array sizing based on user-defined stream IDs
 * - Fast inline functions (no bounds checking for performance)
 * - Stream timing and sequence index management
 * - Dedicated VAR_STREAM_START/VAR_STREAM_STOP control frames for timing and DSP coherence
 * - Stream runtime metadata sent on probe connection (late join support)
 * 
 * PROTOCOL (v2):
 * - TRAX_STREAM_START: Initializes state + sends VAR_STREAM_START control frame
 * - TRAX_STREAM_UPDATE + TRAX_STREAM_SEND_ALL: ship the full pending block
 * - TRAX_STREAM_UPDATE + TRAX_STREAM_SEND     : ship only a slice (max_bytes,
 *   floored internally to a whole-sequence multiple).
 *   Data frames carry only the sequence index and raw samples.
 * - TRAX_STREAM_STOP: Deactivates + sends VAR_STREAM_STOP control frame
 * 
 * TIMING:
 * - Start time captured once by TRAX_STREAM_START (stored in stream state)
 * - VAR_STREAM_START control frame carries start_tick_overflow_cntr + start_timepacked
 * - Data frames carry only sequence index; scope calculates sample times from
 *   start time + sequence_index * sample_period
 * - Uses same time path as VAR/LOG frames (frame header timestamp)
 * 
 * NOTE: Variable-stream macros are defined in this header. Always include
 *       <trax.h> or higher-level headers (like <trax_log.h>) to use
 *       var-stream functionality.
 * 
 ******************************************************************************
 */

#ifndef TRAX_STREAM_H_
#define TRAX_STREAM_H_

#include <stdint.h>
#include <stdbool.h>
#include "trax_config.h"       /* User configuration and TIDs */
#include "trax_tid.h"          /* TID range constants */
#include "trax_timestamp.h"    /* For trax_timebase (sync counter access) */
#include "trax_hw.h"     /* For timestamp format defines */
#include "trax_frame.h"        /* For TRAX_FRAME_RAW_ARGS */
#include "trax_data_types.h"   /* TRAX_SECTION, TRAX_STATIC_ASSERT */
#include "trax_meta_type.h"    /* struct trax_stream_adc_group_meta_t */
#include "trax_meta_section.h" /* TRAX_META_SECTION_STREAM_ADC */
#include "trax_config_default.h"  /* TRAX_STREAM_DESC_LEN, TRAX_STREAM_MAX_CHANNELS */

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 ====================TRAX_CFG_STREAM_CNT DEFAULT=======================================
 ============================================================================*/

/**
 * @brief Default TRAX_CFG_STREAM_CNT to 0 if the application did not declare it.
 *
 * TRAX_CFG_STREAM_CNT is the compile-time count of streams the firmware will
 * instantiate.  The application is expected to define it in trax_config.h:
 *
 *     #define TRAX_CFG_STREAM_CNT  2
 *
 * Falling back to 0 here lets a baseline / log-only project build cleanly
 * without ever knowing about the stream module — the `#if (TRAX_CFG_STREAM_CNT > 0)`
 * blocks below disable the static state array and all hot-path macros.
 *
 * If the user later defines a stream via TRAX_STREAM_ADC_DEFINE
 * but forgot to set TRAX_CFG_STREAM_CNT, the static assert inside that macro fires
 * with a clear hint to update trax_config.h.
 */
#ifndef TRAX_CFG_STREAM_CNT
#define TRAX_CFG_STREAM_CNT 0
#endif

/*=============================================================================
 ====================STREAM ADC METADATA=======================================
 ============================================================================*/

/**
 * @brief Bind a VAR to a stream channel, with an absolute sample-time
 *        offset from the sequence timestamp
 *
 * The offset is the **absolute physical time** at which this channel is
 * sampled, measured from the sequence's nominal timestamp
 * (start_time + k × sample_period).  It is expressed in
 * seconds.  Crucially, it is **independent of the stream sample rate**
 * @c sample_freq_hz — it is fixed by the acquisition hardware (ADC engine
 * clock, FPGA DDC pipeline, multiplexer settling, etc.) and stays valid
 * even if you later retune the stream rate.
 *
 * Each channel carries its absolute offset from the sequence timestamp —
 * NOT a delta from the previous channel.  Channel 0 is typically 0.0,
 * but may be non-zero: the stream start timestamp is captured when the
 * hardware is armed, and a real ADC adds trigger latency plus
 * sample-and-hold / conversion time before the first result exists.
 * Fold that acquisition-start latency into every channel's offset.
 *
 * Examples
 *   • Simultaneous-sample I/Q at any rate (FPGA DDC), negligible latency:
 *       TRAX_STREAM_VAR(VAR_TID_I, 0.0),
 *       TRAX_STREAM_VAR(VAR_TID_Q, 0.0)
 *
 *   • 6-channel sequential ADC scan starting T_START after the trigger,
 *     25 ADC-clock cycles per channel, ADC_CLK_HZ = 16 MHz (so each
 *     channel lags the previous by 1.5625 µs):
 *       #define CH_OFFSET_S(N)  (T_START_S + (double)(N) * 25.0 / (double)ADC_CLK_HZ)
 *       TRAX_STREAM_VAR(VAR_TID_A0, CH_OFFSET_S(0)),
 *       TRAX_STREAM_VAR(VAR_TID_A1, CH_OFFSET_S(1)),
 *       ...
 *       TRAX_STREAM_VAR(VAR_TID_A5, CH_OFFSET_S(5))
 *
 * The macro value is consumed by C99 designated-initialiser semantics
 * inside TRAX_STREAM_ADC_DEFINE, so @c _offset_seconds must be
 * a constant expression evaluable at file scope (literals, sizeof,
 * arithmetic on macros — anything the linker can fold).
 *
 * @param _var_tid          VAR TID to bind this channel to
 * @param _offset_seconds   Absolute time offset from the sequence timestamp,
 *                          in seconds (constant expression of type
 *                          @c double; 0.0 when there is no start latency)
 */
#define TRAX_STREAM_VAR(_var_tid, _offset_seconds) \
    { (_var_tid), (double)(_offset_seconds) }

/**
 * @brief Define an ADC stream with metadata (ELF .trax_stream_adc section)
 *
 * Channel count is auto-computed from the TRAX_STREAM_VAR() arguments.
 *
 * Sequence size is NOT a parameter. Each channel binding references an
 * existing VAR by TID; the VAR's data type (declared in TRAX_VAR_DEFINE)
 * fixes the per-channel byte size. The probe sums these sizes once at
 * trax_stream_init() and the host derives the same value from the
 * exported var meta — so there is nothing for the user to keep in sync.
 *
 * Compile-time guard: the macro statically asserts that the TID's index
 * fits within TRAX_CFG_STREAM_CNT (defined in trax_config.h).  If the assertion
 * fails, the error message points at TRAX_CFG_STREAM_CNT so the fix is obvious.
 *
 * Usage:
 *   #define ADC_OFFSET_S(N) ((double)(N) * 25.0 / 16.0e6) // 25 ADC clk cycles @ 16 MHz
 *   TRAX_STREAM_ADC_DEFINE(TRAX_TID_POWER_ADC, "Power ADC",
 *       10000, 1,                                                  // 10 kHz (num/denom)
 *       TRAX_BYTE_ORDER_LITTLE,                                    // little endian
 *       TRAX_STREAM_VAR(TRAX_TID_VAR_V_R, ADC_OFFSET_S(0)),        // Ch0: reference (0 s)
 *       TRAX_STREAM_VAR(TRAX_TID_VAR_V_S, ADC_OFFSET_S(1)),        // Ch1: +1.5625 µs
 *       TRAX_STREAM_VAR(TRAX_TID_VAR_V_T, ADC_OFFSET_S(2))         // Ch2: +3.1250 µs
 *   );
 *
 * Note that ADC_OFFSET_S(N) is a function of the ADC engine clock and is
 * NOT scaled by the stream sample rate — it stays correct if you later
 * change "10000, 1" to any other rate.
 *
 * @param _tid         Stream TID (unique identifier)
 * @param _desc        Description (max TRAX_STREAM_DESC_LEN chars)
 * @param _freq_num    Sample frequency numerator in Hz (e.g., 10000)
 * @param _freq_denom  Sample frequency denominator (e.g., 1 for integer Hz)
 * @param _byte_order  Byte order: TRAX_BYTE_ORDER_LITTLE or TRAX_BYTE_ORDER_BIG
 * @param ...          TRAX_STREAM_VAR(var_tid, offset_seconds) entries
 */
#if TRAX_ENABLE
#define TRAX_STREAM_ADC_DEFINE(_tid, _desc, _freq_num, _freq_denom, _byte_order, ...) \
    TRAX_STATIC_ASSERT(sizeof(_desc) <= TRAX_STREAM_DESC_LEN, "Stream description exceeds TRAX_STREAM_DESC_LEN"); \
    TRAX_STATIC_ASSERT( \
        sizeof((struct trax_stream_channel_binding_t[]){ __VA_ARGS__ }) / sizeof(struct trax_stream_channel_binding_t) \
            <= TRAX_STREAM_MAX_CHANNELS, \
        "channel_count exceeds TRAX_STREAM_MAX_CHANNELS"); \
    TRAX_STATIC_ASSERT((uint32_t)TRAX_STREAM_TID_INDEX(_tid) < (uint32_t)TRAX_CFG_STREAM_CNT, \
        "Stream TID index >= TRAX_CFG_STREAM_CNT — increase TRAX_CFG_STREAM_CNT in trax_config.h to cover this stream"); \
    static const struct trax_stream_adc_group_meta_t __trax_stream_adc_##_tid \
        TRAX_SECTION(TRAX_META_SECTION_STREAM_ADC) = { \
            .id = (_tid), \
            .channel_count = sizeof((struct trax_stream_channel_binding_t[]){ __VA_ARGS__ }) \
                / sizeof(struct trax_stream_channel_binding_t), \
            .byte_order = (_byte_order), \
            .sample_freq_hz = TRAX_RATIONAL(_freq_num, _freq_denom), \
            .p_channels = { __VA_ARGS__ }, \
            .p_desc = _desc \
        }
#else
/* TraxProbe disabled: no stream metadata. Benign repeatable struct decl. */
#define TRAX_STREAM_ADC_DEFINE(_tid, _desc, _freq_num, _freq_denom, _byte_order, ...) \
    struct trax_stream_adc_group_meta_t
#endif

/*=============================================================================
 ====================STREAM ROTOR METADATA=====================================
 ============================================================================*/

/**
 * @brief Define a ROTOR stream — rotary-encoder position with explicit geometry
 *
 * A ROTOR stream is a single-channel position stream that additionally
 * declares the encoder geometry: @p _counts_per_rev is the number of counts
 * (as they appear in the data, i.e. after any quadrature decoding) that make
 * up ONE mechanical shaft revolution.  The host's Rotary Motor Monitor uses
 * this contract to derive shaft angle (0–360°), speed (RPM) and rotation
 * direction — instead of inferring "one revolution" from the bound VAR's
 * min/max display range, which is a plotting hint, not a measurement contract.
 *
 * The runtime transport is IDENTICAL to the ADC group: the same
 * TRAX_STREAM_START / UPDATE / SEND_ALL / STOP macros operate on the
 * stream, blocks carry a sequence index, and sample times are derived from
 * the start anchor + index × sample period.  Only the metadata record (and
 * therefore how the host interprets the samples) differs.
 *
 * The position VAR must be defined separately with TRAX_VAR_DEFINE — its
 * data type fixes the per-sample byte size; its min/max remain purely a
 * plotting range.  Prefer an unwrapped total count (int32 odometer);
 * Traxcope folds shaft angle 0–360° with _counts_per_rev and leaves
 * Position as the running total so a host formula can scale to mm.
 *
 * Usage:
 *   TRAX_VAR_DEFINE(VAR_TID_ROTOR_POS, "Rotor Position", TRAX_DATA_TYPE_UINT16,
 *       0, 1000, "count", TRAX_COLOR_MAGENTA, TRAX_PLOT_LINE);
 *
 *   TRAX_STREAM_ROTOR_DEFINE(STREAM_TID_ROTOR, "Rotor Position 1 kHz",
 *       1000, 1,                     // 1 kHz sample rate (num/denom)
 *       TRAX_BYTE_ORDER_LITTLE,
 *       1000u,                       // 1000 counts = one shaft revolution
 *       VAR_TID_ROTOR_POS);
 *
 * @param _tid             Stream TID (unique identifier)
 * @param _desc            Description (max TRAX_STREAM_DESC_LEN chars)
 * @param _freq_num        Sample frequency numerator in Hz
 * @param _freq_denom      Sample frequency denominator (1 for integer Hz)
 * @param _byte_order      TRAX_BYTE_ORDER_LITTLE or TRAX_BYTE_ORDER_BIG
 * @param _counts_per_rev  Encoder counts per mechanical revolution (> 0)
 * @param _var_tid         VAR TID carrying the position counts
 */
#if TRAX_ENABLE
#define TRAX_STREAM_ROTOR_DEFINE(_tid, _desc, _freq_num, _freq_denom, _byte_order, _counts_per_rev, _var_tid) \
    TRAX_STATIC_ASSERT(sizeof(_desc) <= TRAX_STREAM_DESC_LEN, "Stream description exceeds TRAX_STREAM_DESC_LEN"); \
    TRAX_STATIC_ASSERT((_counts_per_rev) > 0u, "counts_per_rev must be > 0"); \
    TRAX_STATIC_ASSERT((uint32_t)TRAX_STREAM_TID_INDEX(_tid) < (uint32_t)TRAX_CFG_STREAM_CNT, \
        "Stream TID index >= TRAX_CFG_STREAM_CNT — increase TRAX_CFG_STREAM_CNT in trax_config.h to cover this stream"); \
    static const struct trax_stream_rotor_meta_t __trax_stream_rotor_##_tid \
        TRAX_SECTION(TRAX_META_SECTION_STREAM_ROTOR) TRAX_ALIGNED(4) = { \
            .id = (_tid), \
            .channel_count = 1u, \
            .byte_order = (_byte_order), \
            .sample_freq_hz = TRAX_RATIONAL(_freq_num, _freq_denom), \
            .counts_per_rev = (_counts_per_rev), \
            .p_channels = { { (_var_tid), 0.0 } }, \
            .p_desc = _desc, \
            .reserved = 0 \
        }
#else
/* TraxProbe disabled: no stream metadata. Benign repeatable struct decl. */
#define TRAX_STREAM_ROTOR_DEFINE(_tid, _desc, _freq_num, _freq_denom, _byte_order, _counts_per_rev, _var_tid) \
    struct trax_stream_rotor_meta_t
#endif

/*=============================================================================
 ====================STREAM STATE STRUCTURE====================================
 =============================================================================*/

/**
 * @brief Variable-stream state structure
 * 
 * Maintains state for each stream defined in trax_ids.h
 * 
 * LIFECYCLE:
 * - is_active: Stream running state (true after TRAX_STREAM_START, false after stop)
 * - Validation rejects frames from inactive streams
 * 
 * TIMING:
 * - start_tick_overflow_cntr: Tick overflow counter when stream started (CONSTANT for stream lifetime)
 * - start_timepacked: Packed raw timestamp [tick][timer] when stream started (CONSTANT)
 * - Sent once in VAR_STREAM_START control frame (NOT in every data frame)
 * - Also stored in runtime metadata for late-join support
 * 
 * PC RECONSTRUCTION:
 * - VAR_STREAM_START control frame provides start_tick_overflow_cntr + start_timepacked
 * - Data frames contain only sequence index + raw data
 * - Sample time: start_time + (sequence_index * sample_period)
 * 
 * SEQUENCE INDEX:
 * - Each frame carries the index of the first sequence in that frame
 * - Advanced after each data frame by the number of sequences sent
 * - Also represents the total sequences sent so far after the increment step
 * - Used for missing frame detection and timing calculation
 * 
 * SEQUENCE_SIZE_BYTES:
 * - Size of one complete channel sequence in bytes
 * - Used to automatically calculate sequence count from data block size
 * - Example: 4 channels × 2 bytes = 8 bytes per sequence
 *   Data: [ch0, ch1, ch2, ch3, ch0, ch1, ch2, ch3, ...]
 *         |<-- 8 bytes -->||<-- 8 bytes -->|
 */

/**
 * @brief sequence_size_shift value meaning "no power-of-two shift available"
 *
 * Sequence sizes are almost always a power of two (1/2/4/8 bytes per channel
 * times a channel count that is itself usually a power of two), so
 * trax_stream_init() pre-computes log2 and TRAX_STREAM_UPDATE shifts instead
 * of dividing.  Odd sizes fall back to the divider.
 */
#define TRAX_STREAM_SHIFT_NONE  0xFFu

struct trax_stream_t {
	uint32_t start_tick_overflow_cntr;  /**< Tick overflow counter at stream start (CONSTANT) */
	uint32_t start_timepacked;    /**< Packed timestamp [tick][timer] at stream start (CONSTANT) */
	uint64_t sequence_index;      /**< 64-bit sequence index — wraps after ~5845 years @100 MHz */
	uint16_t sequence_size_bytes; /**< Bytes per sequence for auto-increment (CONSTANT) */
	bool is_active;               /**< Stream is running (set by TRAX_STREAM_START, cleared by stop) */
	uint8_t sequence_size_shift;  /**< log2(sequence_size_bytes), or TRAX_STREAM_SHIFT_NONE when the
	                                *  size is not a power of two (CONSTANT).  Lands in the padding
	                                *  byte after is_active, so it costs no RAM. */
	const void *pending_data;     /**< Block pointer stored by TRAX_STREAM_UPDATE for deferred send */
	uint64_t pending_size;        /**< Block size (BYTES) stored by TRAX_STREAM_UPDATE for deferred send.
	                                *  64-bit so FPGA-rate streams can register a conceptual block
	                                *  exceeding 4 GB (e.g. 10 GHz × 200 ms × 4 B/seq = 8 GB) in one
	                                *  UPDATE call.  Must equal real pending_data buffer bytes for
	                                *  SEND_ALL to be safe; SEND(N) may operate on a smaller real
	                                *  buffer than this field claims (sliced FPGA pattern). */
};

/*=============================================================================
 ====================GLOBAL STREAM STATE ARRAY=================================
 =============================================================================*/

/**
 * @brief Global variable-stream state array.
 *
 * TRAX_CFG_STREAM_CNT defaults to 0 (see fallback above) when the application
 * does not declare any streams.  Declared only when TRAX_CFG_STREAM_CNT > 0.
 * Indexed via TRAX_STREAM_AT_TID().
 */
#if (TRAX_CFG_STREAM_CNT > 0)
extern struct trax_stream_t p_trax_stream_list[TRAX_CFG_STREAM_CNT];
#endif

/*=============================================================================
 ====================STREAM MANAGEMENT FUNCTIONS===============================
 =============================================================================*/

/**
 * @brief Initialize all variable-stream states (called from trax_init)
 *
 * Zeroes all stream state structures.
 * Always declared — called unconditionally from trax_core.c.
 * Body is a no-op when TRAX_CFG_STREAM_CNT == 0.
 */
void trax_stream_init(void);

/**
 * @brief Take a snapshot of current stream runtime metadata.
 *
 * Refreshes an internal table with is_active, start_tick_overflow_cntr, and
 * start_timepacked for every stream, then returns a read-only pointer to it.
 *
 * Ownership rules:
 *   - The returned buffer is owned by the stream module.
 *   - The caller MUST consume it before calling this function again.
 *   - Returns NULL when TRAX_CFG_STREAM_CNT == 0 (and *p_count is set to 0).
 *
 * This design avoids any caller-side stack array, so projects with
 * TRAX_CFG_STREAM_CNT == 0 pay zero memory cost and need no preprocessor guards.
 *
 * @param[out] p_count  Number of entries in the returned table (0..TRAX_CFG_STREAM_CNT).
 * @return Pointer to the snapshot table, or NULL when no streams are configured.
 */
const struct trax_stream_runtime_meta_t *
trax_stream_snapshot_runtime_meta(uint8_t *p_count);

/*=============================================================================
 ====================STREAM ACCESS MACRO=======================================
 =============================================================================*/

#if (TRAX_CFG_STREAM_CNT > 0)

/**
 * @brief Access variable-stream state by TID
 * @param tid Stream Trace ID (TRAX_TID_STREAM_xxx)
 * @return Reference to stream state structure
 *
 * Example:
 *   TRAX_STREAM_AT_TID(TRAX_TID_STREAM_ADC).sequence_index = 0;
 *   uint64_t idx = TRAX_STREAM_AT_TID(TRAX_TID_STREAM_ADC).sequence_index;
 */
#define TRAX_STREAM_AT_TID(tid)  p_trax_stream_list[TRAX_STREAM_TID_INDEX(tid)]

/**
 * @brief Mark one data stream active and anchor its start time (low-level)
 * @param tid Stream Trace ID
 * @param start_tick_overflow_cntr Tick overflow counter at stream start
 * @param start_timepacked  Packed timestamp [tick][timer] at stream start
 *
 * Stores start timing. Resets sequence index to 0 and marks stream as active.
 * Sequence size is NOT set here — it is a stream-lifetime constant that
 * trax_stream_init() pre-computes once from the meta section, so the
 * start path stays cheap and free of meta lookups.
 *
 * NOTE:
 * - No bounds checking for performance. Ensure tid is valid.
 * - Usually called automatically by TRAX_STREAM_START macro.
 * - Activates only when trax_stream_init() resolved a sequence size.
 *
 * A zero sequence_size_bytes means this TID has no channel schema in the
 * .trax_stream_* meta section, so its wire frames could not be decoded and
 * TRAX_STREAM_UPDATE would divide by zero.  Refusing to activate keeps that
 * out of the ISR that feeds the stream; the cost is one cold-path compare,
 * because start runs once per stream, not per block.
 */
static inline void trax_stream_activate(uint32_t tid, uint32_t start_tick_overflow_cntr,
                                     uint32_t start_timepacked)
{
	struct trax_stream_t *s = &TRAX_STREAM_AT_TID(tid);
	s->start_tick_overflow_cntr = start_tick_overflow_cntr;
	s->start_timepacked = start_timepacked;
	s->sequence_index = 0;
	s->is_active = (s->sequence_size_bytes != 0u);
}

/**
 * @brief Stop/deactivate a variable stream (low-level function)
 * @param tid Stream Trace ID
 * 
 * Marks the stream as inactive. Stream data frames received after this
 * will fail validation.
 * 
 * NOTE:
 * - Usually called automatically by TRAX_STREAM_STOP macro
 * - Stream can be restarted by calling TRAX_STREAM_START again
 * - Validation will reject frames from inactive streams
 */
static inline void trax_stream_deactivate(uint32_t tid)
{
	TRAX_STREAM_AT_TID(tid).is_active = false;
}

/**
 * @brief Advance variable-stream sequence index (pure metadata)
 *
 * Bumps sequence_index by @p increment SEQUENCES without touching
 * pending_data / pending_size — useful for SLICED FPGA streams where
 * the front-end captured more sequences than the link could carry, and
 * the firmware needs to advance the host timeline by the gap without
 * registering or shipping any data.
 *
 * @p increment is uint64_t so a single call can express the full gap
 * of an FPGA-rate stream (e.g. ~2×10⁹ sequences for one 10 GHz × 200 ms
 * NaN-gap on a 4 kB-sliced 2-channel int16 link).  Most demos using the
 * pure UPDATE + SEND pattern do NOT need this function — UPDATE's size
 * argument is now 64-bit and handles the conceptual advance directly.
 *
 * @param tid       Stream Trace ID
 * @param increment Sequences to advance (uint64_t — pure metadata, no
 *                  wire-frame size constraint)
 * @return New sequence index value
 *
 * Example (gap-only advance after a sliced send):
 *   trax_stream_advance_sequence_index(TRAX_TID_STREAM_ADC, num_gap_seqs);
 */
static inline uint64_t trax_stream_advance_sequence_index(uint32_t tid, uint64_t increment)
{
	TRAX_STREAM_AT_TID(tid).sequence_index += increment;
	return TRAX_STREAM_AT_TID(tid).sequence_index;
}

/**
 * @brief Sequences represented by a block, avoiding the 64-bit divider
 *
 * Called from TRAX_STREAM_UPDATE, which normally runs in the ISR that owns
 * the stream's DMA transfer, so the cost of the divide matters.  Three tiers,
 * cheapest first:
 *
 *   1. Power-of-two sequence size (the common schema): a shift.
 *   2. Odd sequence size, block below 4 GB: a 32-bit divide.  Cortex-M3 and
 *      up have a single-instruction UDIV but no 64-bit divide, so letting
 *      this stay a uint64_t division would emit a ~40-100 cycle
 *      __aeabi_uldivmod call for what UDIV does in a handful of cycles.
 *   3. Odd sequence size, block at or above 4 GB: the wide divide.  Only the
 *      sliced FPGA pattern registers conceptual blocks that large.
 *
 * @param size_bytes      Block size in bytes
 * @param seq_size_bytes  Bytes per sequence (stream-lifetime constant)
 * @param seq_size_shift  log2(seq_size_bytes), or TRAX_STREAM_SHIFT_NONE
 * @return Number of sequences the block represents
 */
static inline uint64_t trax_stream_seq_count(uint64_t size_bytes,
                                             uint16_t seq_size_bytes,
                                             uint8_t seq_size_shift)
{
	if (seq_size_shift != TRAX_STREAM_SHIFT_NONE) {
		return size_bytes >> seq_size_shift;
	}
	if (size_bytes <= (uint64_t)0xFFFFFFFFu) {
		return (uint64_t)((uint32_t)size_bytes / (uint32_t)seq_size_bytes);
	}
	return size_bytes / (uint64_t)seq_size_bytes;
}

/*=============================================================================
 ====================STREAM MACROS=============================================
 =============================================================================*/

/**
 * @brief Variable-stream start: atomically arm hardware + anchor timestamp
 *
 * Usage: TRAX_STREAM_START(tid, hw_start_callback)
 *
 * Starts ONE data stream. It does not start the session (transmission to
 * Traxcope) — that is trax_session_start() / the host's start command, and
 * trax_wait_session_started() waits for it.
 *
 * The whole purpose of the macro is to keep the **hardware start** and the
 * **timestamp capture** inside a single critical section so that the
 * stream's time anchor reflects the exact moment samples begin flowing.
 * Any gap between "enable timer / DMA" and "record start time" would
 * shift every sample on the host plot by that delay.
 *
 * What the macro does, in order:
 *   1. Enter critical section.
 *   2. Invoke `hw_start_callback` — a function call expression that arms
 *      the hardware producing samples (Timer enable, DMA enable, ...).
 *      Pass `0` when there is no hardware to arm; the call site is then a
 *      no-op expression that the compiler elides.
 *   3. Capture the start timestamp.
 *   4. Initialise stream state: start tick / timepacked, sequence index = 0.
 *      Sequence size is already set by trax_stream_init().
 *   5. Exit critical section.
 *   6. Emit the VAR_STREAM_START control frame with stream_id + start time.
 *
 * If called before TRAX_IS_SESSION_ACTIVE() is true (e.g. BSP init before the
 * scope connects), the control frame is silently skipped; the host will
 * pick up the start state via the runtime-metadata snapshot at connect.
 *
 * For more than one hardware action, wrap them in a small helper:
 *
 *   static inline void adc_hw_start(void) {
 *       LL_DMA_EnableChannel(...);
 *       LL_TIM_EnableCounter(adc_timer);
 *   }
 *   TRAX_STREAM_START(TRAX_TID_STREAM_ADC, adc_hw_start());
 *
 * Example (single hardware call):
 *   TRAX_STREAM_START(TRAX_TID_STREAM_ADC, LL_TIM_EnableCounter(adc_timer));
 *
 * Example (software-generated stream — no hardware to arm):
 *   TRAX_STREAM_START(TRAX_TID_STREAM_SAW, 0);
 *
 * @param tid                Stream Trace ID
 * @param hw_start_callback  Function-call expression that starts the
 *                           hardware producing samples (Timer / DMA
 *                           enable, etc.).  Runs inside the critical
 *                           section, atomically with timestamp capture.
 *                           Pass `0` when no hardware needs to be armed.
 */
#if TRAX_ENABLE
#define TRAX_STREAM_START(tid, hw_start_callback) \
	TRAX_PORT_ENTER_CRITICAL_SECTION { \
		(void)(hw_start_callback); \
		uint32_t _start_ts = TRAX_FRAME_TIMEPACKED_GET(); \
		trax_stream_activate(tid, trax_timebase.tick_overflow_cntr, \
			_start_ts); \
	} \
	TRAX_PORT_EXIT_CRITICAL_SECTION \
	TRAX_FRAME_ARGS(TRAX_TID_VAR_STREAM_START, \
		(uint32_t)(tid), \
		TRAX_STREAM_AT_TID(tid).start_tick_overflow_cntr, \
		TRAX_STREAM_AT_TID(tid).start_timepacked)

/**
 * @brief Variable-stream stop with automatic deactivation and control frame
 *
 * Usage: TRAX_STREAM_STOP(tid, hw_stop_callback)
 *
 * Symmetric with TRAX_STREAM_START — the hardware stop and the
 * stream-state deactivation run inside the same critical section so no
 * sample can sneak in between "disable timer/DMA" and "is_active = false"
 * and be rejected by the validator.
 *
 * The macro automatically:
 *   1. Enters a critical section.
 *   2. Invokes `hw_stop_callback` (Timer/DMA disable, ...).  Pass `0`
 *      when there is no hardware to stop.
 *   3. Marks the stream inactive (validator drops subsequent data frames).
 *   4. Exits the critical section.
 *   5. Emits the VAR_STREAM_STOP control frame.
 *
 * Example (single hardware call):
 *   TRAX_STREAM_STOP(TRAX_TID_STREAM_ADC, LL_TIM_DisableCounter(adc_timer));
 *
 * Example (software-generated stream — nothing to stop):
 *   TRAX_STREAM_STOP(TRAX_TID_STREAM_SAW, 0);
 *
 * @param tid                Stream Trace ID
 * @param hw_stop_callback   Function-call expression that stops the
 *                           hardware (Timer/DMA disable).  Runs inside the
 *                           critical section, atomically with stream
 *                           deactivation.  Pass `0` when no hardware
 *                           needs to be stopped.
 */
#define TRAX_STREAM_STOP(tid, hw_stop_callback) \
	do { \
		TRAX_PORT_ENTER_CRITICAL_SECTION { \
			(void)(hw_stop_callback); \
			trax_stream_deactivate(tid); \
		} \
		TRAX_PORT_EXIT_CRITICAL_SECTION \
		TRAX_FRAME_ARGS(TRAX_TID_VAR_STREAM_STOP, (uint32_t)(tid)); \
	} while(0)

/**
 * @brief Update variable-stream state with latest data block
 *
 * Stores the block pointer and byte size, then advances sequence_index
 * by (size_bytes / sequence_size_bytes) — i.e. by the number of
 * sequences the declared block represents.  The task then calls
 *   TRAX_STREAM_SEND_ALL()           to ship the whole pending block, or
 *   TRAX_STREAM_SEND(tid, max_bytes) to ship a head-slice of it.
 *
 * Two operating modes are supported:
 *
 *   1. HONEST mode (low-rate ADC / SDR baseband):
 *      size_bytes == real bytes living in p_data.  SEND_ALL is safe;
 *      SEND(max_bytes) is just an optional optimization for partial frames.
 *
 *   2. SLICED FPGA mode (very-high-rate fronts):
 *      size_bytes == conceptual block bytes the FPGA captured this
 *      period (may exceed real RAM held in p_data).  The big size_bytes
 *      advances sequence_index by the full count — keeping the host
 *      timeline aligned with wall clock — but only the head max_bytes
 *      (set via SEND(max_bytes)) are actually put on the wire; the rest
 *      is plotted as a NaN gap.  In this mode SEND_ALL is FORBIDDEN: it
 *      would walk past p_data and trash RAM.
 *
 * @p size_bytes is uint64_t.  At 10 GHz × 200 ms × 4 B/seq one block is
 * 8 GB, which is why this field is no longer 32-bit.
 *
 * If the task misses a block, the next TRAX_STREAM_UPDATE overwrites
 * the pending data and the sequence index jumps — the PC detects a gap
 * instead of silently drifting.
 *
 * Cost: this macro usually runs in the ISR that owns the DMA half-transfer,
 * so the sequence count goes through trax_stream_seq_count(), which keeps
 * the 64-bit divider off the path for every realistic block size — see that
 * function for the three tiers.
 *
 * size_bytes is evaluated once, so passing a function call is safe.
 *
 * @param tid        Stream Trace ID
 * @param p_data     Pointer to data buffer
 * @param size_bytes Block size in bytes (uint64_t; conceptual block size
 *                   in SLICED FPGA mode, real bytes in HONEST mode)
 */
#define TRAX_STREAM_UPDATE(tid, p_data, size_bytes) \
	do { \
		struct trax_stream_t *_s = &TRAX_STREAM_AT_TID(tid); \
		uint64_t _upd_size = (uint64_t)(size_bytes); \
		_s->pending_data = (p_data); \
		_s->pending_size = _upd_size; \
		_s->sequence_index += trax_stream_seq_count(_upd_size, \
			_s->sequence_size_bytes, _s->sequence_size_shift); \
	} while(0)

/**
 * @brief Send ALL of the pending variable-stream block stored by TRAX_STREAM_UPDATE
 *
 * Pairs with TRAX_STREAM_UPDATE().  Does NOT advance the sequence
 * index (already done by TRAX_STREAM_UPDATE).  The wire frame
 * carries the full pending_data buffer.
 *
 * Use this only in HONEST mode (pending_size == real bytes in p_data).
 * For SLICED FPGA mode where pending_size is the conceptual block size
 * and may exceed the RAM held in p_data, use TRAX_STREAM_SEND(tid,
 * max_sequences) instead — SEND_ALL would attempt to read past p_data
 * and trash RAM.
 *
 * Wire-frame mem_size is uint32_t, so this macro caps the shipped bytes
 * at UINT32_MAX.  In HONEST mode that's a non-issue (real RAM buffers
 * are far below 4 GB on any practical MCU); in SLICED FPGA mode the
 * cap can silently truncate the conceptual size — another reason to
 * use SEND(N) there.
 *
 * Self-guarded: if pending_size is zero (no UPDATE since the last
 * SEND_ALL), or the stream is not active, this macro is a no-op. Callers
 * can therefore invoke it unconditionally from a polling task without
 * tracking "is there anything to send?" themselves.
 *
 * The is_active gate lives here rather than in TRAX_STREAM_UPDATE so the
 * ISR that calls UPDATE pays nothing for it: this path already builds a
 * header and hands a block to the transport, so one extra byte load is
 * free, and when the stream is stopped it now skips all of that work.
 * Without the gate a stream that was never started still puts frames on
 * the wire, and the host counts each one as stream corruption — which
 * aborts the whole decode pass, not just this lane.
 *
 * Race-safe vs. UPDATE: the snapshot of pending_data/pending_size/
 * sequence_index and the clear of pending_size happen inside a single
 * critical section, so an ISR firing in the middle either lands fully
 * before the snapshot (its block is shipped now) or fully after the
 * clear (its block is shipped on the next call) — never split.
 *
 * @param tid Stream Trace ID
 */
#define TRAX_STREAM_SEND_ALL(tid) \
	do { \
		struct trax_stream_t *_s = &TRAX_STREAM_AT_TID(tid); \
		const void *_p_data; \
		uint64_t    _size; \
		uint64_t    _seq; \
		bool        _active; \
		TRAX_PORT_ENTER_CRITICAL_SECTION { \
			_p_data         = _s->pending_data; \
			_size           = _s->pending_size; \
			_seq            = _s->sequence_index; \
			_active         = _s->is_active; \
			_s->pending_size = 0u; \
		} \
		TRAX_PORT_EXIT_CRITICAL_SECTION \
		if (_active && _size > 0u) { \
			uint32_t _wire_bytes = (_size > (uint64_t)0xFFFFFFFFu) \
				? 0xFFFFFFFFu : (uint32_t)_size; \
			uint32_t _seq_hi = (uint32_t)(_seq >> 32); \
			uint32_t _seq_lo = (uint32_t)(_seq & 0xFFFFFFFFu); \
			TRAX_FRAME_RAW_ARGS(tid, _p_data, _wire_bytes, _seq_hi, _seq_lo); \
		} \
	} while(0)

/**
 * @brief Send AT MOST max_bytes from the front of the pending block
 *
 * Pairs with TRAX_STREAM_UPDATE().  Ships only the first
 * `min(max_bytes, pending_size)` bytes from pending_data, floored
 * internally to a whole-sequence multiple; the un-shipped tail of
 * the block becomes a NaN-bridged gap on the scope until the next
 * UPDATE/SEND lands.
 *
 * Use this when the FPGA / front-end has filled (or is conceptually
 * tracking) a much larger block than the MCU can put on the wire — the
 * caller previously called UPDATE with the full block size so that
 * sequence_index advances at the true sample rate, and now picks how
 * many BYTES from the head of that block to actually transmit (typically
 * derived from a UART / RTT / USB bandwidth budget).
 *
 * The wire frame's reported sequence index is the slice's end-index
 * (= start_of_block + sent_sequences), so the host plots the slice
 * at the correct time anchor and renders the un-shipped tail as a
 * gap up to the next block.
 *
 * Does NOT modify sequence_index — the index stays where UPDATE left
 * it (= start_of_block + full_pending_sequences), so the next UPDATE's
 * data lines up with reality.
 *
 * If max_bytes >= pending_size, behaves exactly like
 * TRAX_STREAM_SEND_ALL().
 *
 * Self-guarded: if pending_size is zero (no UPDATE since the last
 * SEND/SEND_ALL), or the stream is not active, this macro is a no-op.
 * Callers can therefore invoke it unconditionally from a polling task.
 * See TRAX_STREAM_SEND_ALL for why the is_active gate sits on the send
 * path instead of in TRAX_STREAM_UPDATE.
 *
 * Race-safe vs. UPDATE: the snapshot of pending_data/pending_size/
 * sequence_index/sequence_size_bytes and the clear of pending_size
 * happen inside a single critical section, so an ISR firing in the
 * middle either lands fully before the snapshot (its block is shipped
 * now) or fully after the clear (its block is shipped on the next call)
 * — never split.
 *
 * Argument unit is BYTES on the wire, matching TRAX_STREAM_UPDATE's
 * size_bytes parameter and the byte-natural mental model of bandwidth
 * budgeting.  Internally floored to a whole-sequence multiple because
 * the wire protocol does not represent fractional sequences; the
 * `sequence_size_bytes` field comes from the stream schema and is known
 * to both firmware and host, so the floor is transparent and lossless:
 *
 *     wire_bytes = (min(max_bytes, pending_size) / sequence_size_bytes)
 *                  * sequence_size_bytes;
 *
 * Any (max_bytes % sequence_size_bytes) tail is left in pending — it
 * ships on the next SEND, or appears as a NaN gap on the host scope if
 * the next UPDATE supersedes it.
 *
 * @param tid       Stream Trace ID
 * @param max_bytes Upper bound on BYTES to put on the wire
 *                  (uint64_t — clamped to pending_size, then floored
 *                  to a whole-sequence multiple)
 *
 * Example (FPGA wrote a 1.92 G-sample block of a 1-ch int16 stream;
 *          ship only the first 4096 sequences = 8192 bytes):
 *   TRAX_STREAM_UPDATE(TRAX_TID_STREAM_RF, &fpga_ram_block,
 *                          IRQ_BLOCK_SEQUENCES * sizeof(int16_t));
 *   TRAX_STREAM_SEND  (TRAX_TID_STREAM_RF, 4096 * sizeof(int16_t));
 */
#define TRAX_STREAM_SEND(tid, max_bytes) \
	do { \
		struct trax_stream_t *_s = &TRAX_STREAM_AT_TID(tid); \
		const void *_p_data; \
		uint64_t    _pending_size; \
		uint32_t    _seq_size; \
		uint64_t    _seq; \
		bool        _active; \
		TRAX_PORT_ENTER_CRITICAL_SECTION { \
			_p_data       = _s->pending_data; \
			_pending_size = _s->pending_size; \
			_seq_size     = _s->sequence_size_bytes; \
			_seq          = _s->sequence_index; \
			_active       = _s->is_active; \
			_s->pending_size = 0u; \
		} \
		TRAX_PORT_EXIT_CRITICAL_SECTION \
		if (_active && _pending_size > 0u && _seq_size > 0u) { \
			uint64_t _cap_bytes  = ((uint64_t)(max_bytes) < _pending_size) \
				? (uint64_t)(max_bytes) : _pending_size; \
			uint64_t _send_seqs  = _cap_bytes / _seq_size; \
			uint64_t _send_bytes = _send_seqs * _seq_size; \
			if (_send_bytes > 0u) { \
				uint64_t _avail_seqs = _pending_size / _seq_size; \
				uint64_t _wire_idx   = _seq - (_avail_seqs - _send_seqs); \
				uint32_t _seq_hi     = (uint32_t)(_wire_idx >> 32); \
				uint32_t _seq_lo     = (uint32_t)(_wire_idx & 0xFFFFFFFFu); \
				TRAX_FRAME_RAW_ARGS(tid, _p_data, \
					(uint32_t)_send_bytes, _seq_hi, _seq_lo); \
			} \
		} \
	} while(0)
#else /* !TRAX_ENABLE — stream runtime macros become no-ops */
#define TRAX_STREAM_START(tid, hw_start_callback) ((void)0)
#define TRAX_STREAM_STOP(tid, hw_stop_callback)   ((void)0)
#define TRAX_STREAM_UPDATE(tid, p_data, size_bytes) ((void)0)
#define TRAX_STREAM_SEND_ALL(tid)                 ((void)0)
#define TRAX_STREAM_SEND(tid, max_bytes)          ((void)0)
#endif /* TRAX_ENABLE */

#endif /* TRAX_CFG_STREAM_CNT > 0 */

#ifdef __cplusplus
}
#endif

#endif /* TRAX_STREAM_H_ */
