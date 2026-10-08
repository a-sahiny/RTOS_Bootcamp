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
 * @file           : trax_var.h
 * @brief          : TraxProbe Variable (VAR) API for runtime value updates
 * @version        : 3.0.0
 ******************************************************************************
 * @attention
 * 
 * This module provides the runtime API for updating variable values.
 * "VAR" is the unified name for what was previously called "signal" or "channel".
 * 
 * KEY DESIGN PRINCIPLE:
 * - TRAX_VAR_SET() works standalone WITHOUT any metadata definition!
 * - TRAX_VAR_DEFINE() is OPTIONAL for enhanced visualization
 * 
 * FEATURES:
 * - Fast variable updates (< 2µs on 64MHz MCU)
 * - Minimal frame size (16 bytes)
 * - No runtime lookup (TID known at compile time)
 * - Variables can be standalone OR bound to log messages
 * - Works without metadata (Scope uses sensible defaults)
 * 
 * SCOPE BEHAVIOR:
 * - WITH metadata: Uses name, unit, min/max for rich display
 * - WITHOUT metadata: Auto-generates defaults:
 *   - Name: "VAR_0xXXXX" (from TID)
 *   - Type: Inferred from value size (uint32 default)
 *   - Min/Max: Auto-scaled from received values
 *   - Unit: None
 * 
 * USAGE PATTERNS:
 * 
 * Pattern A: Quick use (no metadata needed)
 *   // Just send values - Scope handles display automatically
 *   TRAX_VAR_SET(0x0003, temperature);
 *   TRAX_VAR_SET(0x0004, pressure);
 * 
 * Pattern B: Rich metadata (optional, for better visualization)
 *   // Define metadata once (in config file)
 *   // Parameters: TID, Name, Type, Min, Max, Unit, Color, PlotStyle
 *   TRAX_VAR_DEFINE(VAR_TEMP, "Temperature", TRAX_DATA_TYPE_INT16, -40, 125, "°C", TRAX_COLOR_RED, TRAX_PLOT_AUTO);
 *   // Define conversion formulas (optional, multiple per var)
 *   TRAX_VAR_FORMULA(VAR_TEMP, "Celsius", "°C", "x * 0.1");
 *   TRAX_VAR_FORMULA(VAR_TEMP, "Fahrenheit", "°F", "x * 0.18 + 32");
 *   
 *   // Then use at runtime
 *   TRAX_VAR_SET(VAR_TEMP, temperature_value);
 * 
 * Pattern C: Variable bound to log messages
 *   // Define variable binding (positional: 0 = not a variable)
 *   TRAX_LOG_DEFINE_VARS(TRAX_TID_MOTOR_LOG, VAR_TEMP, 0, VAR_SPEED, 0);
 *   
 *   // Log with values - Scope extracts variable data automatically
 *   TRAX_LOG_INFO(TRAX_TID_MOTOR_LOG, "Temp=%d Err=%d Speed=%d Code=%d", temp, err, speed, code);
 * 
 * VAR-LOG RELATIONSHIP:
 * - A variable can be bound to multiple log messages (1:N relationship)
 * - When using TRAX_LOG_xxx with bound variables, values are extracted by Scope
 * - TRAX_VAR_SET sends a standalone variable update (not logged)
 * 
 ******************************************************************************
 */

#ifndef TRAX_VAR_H_
#define TRAX_VAR_H_

#include <stdint.h>
#include "trax_frame.h"          /* Frame creation macros */
#include "trax_data_types.h"     /* TRAX_SECTION, TRAX_STATIC_ASSERT */
#include "trax_tid.h"            /* TRAX_TID_RANGE_VAR_START / _END for range checks */
#include "trax_meta_type.h"      /* struct trax_var_meta_t */
#include "trax_meta_section.h"   /* TRAX_META_SECTION_VAR / _VAR_FORMULA / _VAR_FILTER */
#include "trax_config_default.h" /* TRAX_CFG_META_NAME_LEN, TRAX_CFG_META_UNIT_LEN */
#include "trax_color.h"          /* TRAX_COLOR_TO_RGB_ */

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 ====================PLOT STYLE CONSTANTS======================================
 ============================================================================*/

/** @brief Plot style hints (encoded in flags bits [1:0] of trax_var_meta_t) */
#define TRAX_PLOT_AUTO    0   /**< Default: Line for analog, Step for BOOL */
#define TRAX_PLOT_LINE    1   /**< Force line plot (connect points with lines) */
#define TRAX_PLOT_STEP    2   /**< Force step plot (horizontal-then-vertical) */
#define TRAX_PLOT_POINTS  3   /**< Force points-only plot (no connecting lines) */

/*=============================================================================
 ====================VAR METADATA==============================================
 ============================================================================*/

/**
 * @brief Define a variable (VAR) with metadata (stored in ELF .trax_var section)
 *
 * THIS MACRO IS OPTIONAL! TRAX_VAR_SET() works WITHOUT any metadata definition.
 *
 * If defined: Scope uses name, unit, min/max for rich visualization.
 * If not defined: Scope auto-generates defaults (name="VAR_0xXXXX", auto-scale).
 *
 * Value conversions are defined separately via TRAX_VAR_FORMULA().
 *
 * @param _tid         Trace ID (unique identifier)
 * @param _name        Variable name (max TRAX_CFG_META_NAME_LEN chars)
 * @param _data_type   Data type (TRAX_DATA_TYPE_xxx)
 * @param _min         Minimum expected value (for Y-axis scaling)
 * @param _max         Maximum expected value (for Y-axis scaling)
 * @param _unit        Unit string (max TRAX_CFG_META_UNIT_LEN chars, "" for none)
 * @param _color       TRAX_COLOR_RGB(r,g,b), TRAX_COLOR_xxx, or TRAX_COLOR_AUTO
 * @param _plot_style  TRAX_PLOT_AUTO, TRAX_PLOT_LINE, TRAX_PLOT_STEP, or TRAX_PLOT_POINTS
 *
 * @note Compile error if name or unit exceeds configured limits.
 *
 * @note PRECISION CONTRACT: _min/_max travel as IEEE-754 binary64, and the
 *       entire Traxcope value pipeline (samples, LOD, triggers, rendering)
 *       runs on binary64. Integers are exact up to 2^53 (~9.0e15); beyond
 *       that, values are quantized to the nearest representable double
 *       (e.g. a 2048-count grid near 2^64). If a 64-bit variable operates
 *       near full scale and small deltas matter, transmit an offset/delta
 *       (value - base) instead of the raw value, and put the base in the
 *       name or a formula. Traxcope flags such channels with a [>2^53]
 *       badge in the scope tree.
 */
#if TRAX_ENABLE
#define TRAX_VAR_DEFINE(_tid, _name, _data_type, _min, _max, _unit, _color, _plot_style) \
    TRAX_STATIC_ASSERT(sizeof(_name) <= TRAX_CFG_META_NAME_LEN, \
        "VAR name exceeds TRAX_CFG_META_NAME_LEN"); \
    TRAX_STATIC_ASSERT(sizeof(_unit) <= TRAX_CFG_META_UNIT_LEN, \
        "VAR unit exceeds TRAX_CFG_META_UNIT_LEN"); \
    static const struct trax_var_meta_t __trax_var_meta_##_tid \
        TRAX_SECTION(TRAX_META_SECTION_VAR) = { \
            .id = (_tid), \
            .data_type = (_data_type), \
            .flags = (uint8_t)((_plot_style) & 0x03), \
            .min_value = (_min), \
            .max_value = (_max), \
            .p_name = _name, \
            .p_unit = _unit, \
            .color = TRAX_COLOR_TO_RGB_(_color) \
        }
#else
/* TraxProbe disabled: no VAR metadata. Benign repeatable struct decl
 * absorbs the trailing ';' at file scope with zero footprint. */
#define TRAX_VAR_DEFINE(_tid, _name, _data_type, _min, _max, _unit, _color, _plot_style) \
    struct trax_var_meta_t
#endif

/*=============================================================================
 ====================VAR FORMULA METADATA======================================
 =============================================================================*/

/**
 * @brief Define a named conversion formula for a variable
 *
 * Multiple formulas can be defined per VAR. Traxcope shows them in a
 * dropdown and lets the user switch at runtime. Raw values are always
 * stored; the selected formula is applied at display/query time.
 *
 * The expression uses 'x' as the raw value placeholder.
 * Example expressions: "x * 0.000806", "(x - 500) * 0.1 + 25"
 *
 * @param _tid   VAR Trace ID this formula belongs to
 * @param _name  Formula name (e.g., "Voltage", "Temp_C")
 * @param _unit  Unit string when this formula is active (e.g., "V", "°C")
 * @param _expr  Expression string, 'x' = raw typed value
 */
#if TRAX_ENABLE
#define TRAX_VAR_FORMULA(_tid, _name, _unit, _expr) \
    TRAX_STATIC_ASSERT(sizeof(_name) <= TRAX_CFG_META_NAME_LEN, \
        "Formula name exceeds TRAX_CFG_META_NAME_LEN"); \
    TRAX_STATIC_ASSERT(sizeof(_unit) <= TRAX_CFG_META_UNIT_LEN, \
        "Formula unit exceeds TRAX_CFG_META_UNIT_LEN"); \
    TRAX_STATIC_ASSERT(sizeof(_expr) <= TRAX_CFG_FORMULA_EXPR_LEN, \
        "Formula expression exceeds TRAX_CFG_FORMULA_EXPR_LEN"); \
    static const struct trax_var_formula_meta_t \
        TRAX_CONCAT_2_AND_EXPAND(__trax_formula_, __COUNTER__) \
        TRAX_SECTION(TRAX_META_SECTION_VAR_FORMULA) = { \
            .var_id = (_tid), \
            .expr_len = (uint16_t)TRAX_CFG_FORMULA_EXPR_LEN, \
            .p_name = _name, \
            .p_unit = _unit, \
            .p_expr = _expr \
        }
#else
#define TRAX_VAR_FORMULA(_tid, _name, _unit, _expr) struct trax_var_formula_meta_t
#endif

/*=============================================================================
 ====================VAR FILTER (TRANSFER FUNCTION) METADATA===================
 =============================================================================*/

/**
 * @brief Define a discrete-time filter H(z) attached to a (stream) variable
 *
 * Multiple filters can be defined per VAR. Traxcope shows them in a per-
 * channel dropdown alongside formulas (orthogonal selection). Filters are
 * intended for stream vars; on async vars they are accepted but ignored
 * by the host pipeline.
 *
 * Coefficients ship as float arrays. The host promotes to double for the
 * inner MAC loop. Convention: a[0] is implicit 1.0 — pass only a[1..M]
 * in the _a list. For FIR set _num_a = 0 (the _a placeholder is then
 * unused, conventionally `(0.0f)`).
 *
 * The _b and _a payloads are PARENTHESISED comma-separated lists, NOT
 * brace-initialisers. The C preprocessor splits macro arguments on
 * top-level commas regardless of brace nesting; wrapping in `(...)`
 * keeps the list as a single macro argument and TRAX_UNPAREN strips
 * the parentheses inside the expansion.
 *
 * Example — 1st-order IIR low-pass (alpha = 0.1):
 *   y[n] = 0.1*x[n] + 0.9*y[n-1]   ->   b=[0.1], a=[-0.9] (a[1] = -0.9)
 *   TRAX_VAR_FILTER(VAR_ADC, "10Hz LPF",
 *                        1, (0.1f),
 *                        1, (-0.9f));
 *
 * Example — 5-tap moving average FIR:
 *   TRAX_VAR_FILTER(VAR_ADC, "MAVG-5",
 *                        5, (0.2f, 0.2f, 0.2f, 0.2f, 0.2f),
 *                        0, (0.0f));
 *
 * @param _tid    VAR Trace ID this filter belongs to (typically a stream var)
 * @param _name   Filter name (max TRAX_CFG_META_NAME_LEN chars)
 * @param _num_b  Count of valid b coefficients (>=1, <=TRAX_CFG_FILTER_MAX_ORDER+1)
 * @param _b      Parenthesised list of b coefficients, e.g. (0.1f, 0.2f).
 *                Shorter than the struct array is fine — trailing entries are
 *                zero-filled.
 * @param _num_a  Count of valid a coefficients (0 = FIR, else <=TRAX_CFG_FILTER_MAX_ORDER+1)
 * @param _a      Parenthesised list of a coefficients starting at a[1]. For FIR
 *                pass `(0.0f)` as an unused placeholder.
 *
 * @note Counts are checked at compile time against TRAX_CFG_FILTER_MAX_ORDER+1.
 */
#if TRAX_ENABLE
#define TRAX_VAR_FILTER(_tid, _name, _num_b, _b, _num_a, _a) \
    TRAX_STATIC_ASSERT(sizeof(_name) <= TRAX_CFG_META_NAME_LEN, \
        "Filter name exceeds TRAX_CFG_META_NAME_LEN"); \
    TRAX_STATIC_ASSERT((_num_b) >= 1, \
        "Filter num_b must be >= 1"); \
    TRAX_STATIC_ASSERT((_num_b) <= TRAX_CFG_FILTER_MAX_ORDER + 1, \
        "Filter num_b exceeds TRAX_CFG_FILTER_MAX_ORDER+1"); \
    TRAX_STATIC_ASSERT((_num_a) <= TRAX_CFG_FILTER_MAX_ORDER + 1, \
        "Filter num_a exceeds TRAX_CFG_FILTER_MAX_ORDER+1"); \
    static const struct trax_var_filter_meta_t \
        TRAX_CONCAT_2_AND_EXPAND(__trax_filter_, __COUNTER__) \
        TRAX_SECTION(TRAX_META_SECTION_VAR_FILTER) = { \
            .var_id = (_tid), \
            .num_b = (uint8_t)(_num_b), \
            .num_a = (uint8_t)(_num_a), \
            .p_name = _name, \
            .b = { TRAX_UNPAREN _b }, \
            .a = { TRAX_UNPAREN _a } \
        }
#else
#define TRAX_VAR_FILTER(_tid, _name, _num_b, _b, _num_a, _a) struct trax_var_filter_meta_t
#endif

/*=============================================================================
 ====================VAR UPDATE MACROS=========================================
 =============================================================================*/

/**
 * @brief Update a variable value (standalone VAR frame)
 * 
 * Sends a single variable value update to the trace buffer.
 * 
 * IMPORTANT: Works WITHOUT any prior TRAX_VAR_DEFINE()!
 * If metadata is not defined, Scope will:
 *   - Display as "VAR_0xXXXX" (using TID)
 *   - Auto-scale Y-axis from received values
 *   - Treat value as uint32_t
 * 
 * Frame structure (16 bytes total):
 *   - Header (4 bytes): [param_count=1 (16-bit)][trans_counter (16-bit)]
 *   - Timestamp (4 bytes): Timetrack value
 *   - TID (4 bytes): Variable ID
 *   - Value (4 bytes): Variable value (cast to uint32_t)
 * 
 * @param _tid   Variable Trace ID (any unique value, optionally from TRAX_VAR_DEFINE)
 * @param _value Variable value (any type, cast to uint32_t)
 * 
 * @note Uses atomic frame creation (single critical section)
 * @note Overhead: < 2µs on 64MHz MCU
 * @note Metadata is OPTIONAL - variable will work without it
 * 
 * @warning _value is evaluated INSIDE the TraxProbe critical section.
 *          It must be an ISR-safe, side-effect-free expression (variable,
 *          arithmetic, cast). NEVER call a task-level RTOS API in the
 *          argument — e.g. uxQueueMessagesWaiting() internally uses
 *          taskENTER/EXIT_CRITICAL, which re-enables interrupts on exit
 *          and breaks the half-written frame's atomicity (out-of-order
 *          trans_counter → FRAME_CORRUPT stop). TraxProbe manages the
 *          interrupt mask itself, so the macro body runs in an ISR-like
 *          context: use the query-style FromISR variant instead
 *          (e.g. uxQueueMessagesWaitingFromISR()), or evaluate the value
 *          into a local variable before the macro. Applies to all
 *          TRAX_VAR_SET* variants. See
 *          docs/PITFALL_RTOS_CALLS_IN_FRAME_ARGUMENTS.md.
 * 
 * @example
 *   // Quick use - no metadata needed
 *   #define VAR_TEMP  0x0003
 *   int16_t temp = read_temperature();
 *   TRAX_VAR_SET(VAR_TEMP, temp);  // Works immediately!
 * 
 *   // With metadata (optional, for richer display)
 *   // TRAX_VAR_DEFINE(VAR_TEMP, "Temperature", TRAX_DATA_TYPE_INT16, -40, 125, "°C", TRAX_COLOR_AUTO, TRAX_PLOT_AUTO);
 *   TRAX_VAR_SET(VAR_TEMP, temp);  // Now shows "Temperature" with unit
 */
#if TRAX_ENABLE
#define TRAX_VAR_SET(_tid, _value) \
    do { \
        TRAX_STATIC_ASSERT( \
            (_tid) >= TRAX_TID_RANGE_VAR_START && (_tid) <= TRAX_TID_RANGE_VAR_END, \
            "TRAX_VAR_SET: TID is outside the VAR range (0x2000-0x2FFF)"); \
        TRAX_FRAME_ARGS_ATOMIC((_tid), TRAX_32(_value)) \
    } while(0)
#else
#define TRAX_VAR_SET(_tid, _value) ((void)0)
#endif

/**
 * @brief Update a floating-point variable value
 * 
 * Same as TRAX_VAR_SET but for float values.
 * Uses bit-level conversion to preserve IEEE 754 representation.
 * 
 * @param _tid   Variable Trace ID (must be defined with TRAX_DATA_TYPE_FLOAT)
 * @param _value Float variable value
 * 
 * @example
 *   float temperature = read_temperature_float();
 *   TRAX_VAR_SET_FLOAT(VAR_TEMP_F, temperature);
 */
#if TRAX_ENABLE
#define TRAX_VAR_SET_FLOAT(_tid, _value) \
    do { \
        TRAX_STATIC_ASSERT( \
            (_tid) >= TRAX_TID_RANGE_VAR_START && (_tid) <= TRAX_TID_RANGE_VAR_END, \
            "TRAX_VAR_SET_FLOAT: TID is outside the VAR range (0x2000-0x2FFF)"); \
        TRAX_FRAME_ARGS_ATOMIC((_tid), TRAX_32F(_value)) \
    } while(0)
#else
#define TRAX_VAR_SET_FLOAT(_tid, _value) ((void)0)
#endif

/**
 * @brief Update a 64-bit integer variable value
 * 
 * Sends a 64-bit value as two 32-bit words (high, low).
 * Frame size: 20 bytes (Header + Timestamp + TID + 2×Value)
 * 
 * @param _tid   Variable Trace ID (must be defined with TRAX_DATA_TYPE_INT64 or similar)
 * @param _value 64-bit integer value (int64_t or uint64_t)
 * 
 * @note The full 64-bit value is transmitted losslessly, but the Traxcope
 *       host pipeline stores samples as IEEE-754 binary64: integers are
 *       exact up to 2^53 (~9.0e15) and quantized above that (2048-count
 *       grid near 2^64). For near-full-scale counters where small deltas
 *       matter, send an offset/delta (value - base) instead.
 * 
 * @example
 *   uint64_t total_bytes = get_total_bytes_transferred();
 *   TRAX_VAR_SET64(VAR_TOTAL_BYTES, total_bytes);
 */
#if TRAX_ENABLE
#define TRAX_VAR_SET64(_tid, _value) \
    do { \
        TRAX_STATIC_ASSERT( \
            (_tid) >= TRAX_TID_RANGE_VAR_START && (_tid) <= TRAX_TID_RANGE_VAR_END, \
            "TRAX_VAR_SET64: TID is outside the VAR range (0x2000-0x2FFF)"); \
        /* Pass high/low words as separate single-expression args so that      */ \
        /* PP_NARG counts them as 2 (and TRAX_PUT_ARG_AUTO dispatches to       */ \
        /* TRAX_PUT_ARG_2). Using TRAX_64_PAIR here would survive ##__VA_ARGS__ */ \
        /* unexpanded and explode the comma inside TRAX_PUT32 → 3 args.        */ \
        TRAX_FRAME_ARGS_ATOMIC((_tid), \
                               TRAX_64_HI(_value), \
                               TRAX_64_LO(_value)) \
    } while(0)
#else
#define TRAX_VAR_SET64(_tid, _value) ((void)0)
#endif

/**
 * @brief Update a double-precision floating-point variable value
 * 
 * Sends a double (64-bit) as two 32-bit words.
 * Uses bit-level conversion to preserve IEEE 754 double representation.
 * Frame size: 20 bytes (Header + Timestamp + TID + 2×Value)
 * 
 * @param _tid   Variable Trace ID (must be defined with TRAX_DATA_TYPE_DOUBLE)
 * @param _value Double variable value
 * 
 * @example
 *   double precise_measurement = get_precise_value();
 *   TRAX_VAR_SET_DOUBLE(VAR_PRECISE, precise_measurement);
 */
#if TRAX_ENABLE
#define TRAX_VAR_SET_DOUBLE(_tid, _value) \
    do { \
        TRAX_STATIC_ASSERT( \
            (_tid) >= TRAX_TID_RANGE_VAR_START && (_tid) <= TRAX_TID_RANGE_VAR_END, \
            "TRAX_VAR_SET_DOUBLE: TID is outside the VAR range (0x2000-0x2FFF)"); \
        /* See TRAX_VAR_SET64 for why TRAX_64F_PAIR cannot be used directly. */ \
        TRAX_FRAME_ARGS_ATOMIC((_tid), \
                               TRAX_64F_HI(_value), \
                               TRAX_64F_LO(_value)) \
    } while(0)
#else
#define TRAX_VAR_SET_DOUBLE(_tid, _value) ((void)0)
#endif

/*=============================================================================
 ====================VAR VALIDATION MACROS=====================================
 =============================================================================*/

/*=============================================================================
 ====================INITIALIZATION============================================
 =============================================================================*/

/**
 * @brief Initialize VAR subsystem
 *
 * Called from trax_init(). Currently a no-op since VARs have no runtime state.
 * Exists for API consistency and future extensibility.
 */
void trax_var_init(void);

/*=============================================================================
 ====================MULTI-VAR UPDATE MACROS===================================
 =============================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* TRAX_VAR_H_ */
