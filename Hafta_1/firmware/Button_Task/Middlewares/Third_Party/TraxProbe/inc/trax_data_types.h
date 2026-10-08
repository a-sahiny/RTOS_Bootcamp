/*
 * SPDX-License-Identifier: LicenseRef-TraxProbe-Commercial
 * Copyright (c) 2026 Embedya. All rights reserved.
 *
 * TraxProbe core engine. CONFIDENTIAL and proprietary to Embedya.
 * Licensed under the TraxProbe Commercial License (see LICENSE-COMMERCIAL.txt).
 * No use, copying, modification, redistribution, decompilation, or reverse
 * engineering is permitted except as expressly authorized by that agreement.
 */

#ifndef TRAX_DATA_TYPES_H_
#define TRAX_DATA_TYPES_H_

#include <stdint.h>
#include "trax_compiler.h"
/* Note: Do NOT include trax_config_default.h here - causes circular dependencies */


/*=============================================================================
 ====================GLOBAL MACRO DEFINITIONS==================================
 ============================================================================*/

/**
 * @brief TraxProbe data type identifiers
 *
 * Identifies the data type carried by a variable, var-stream channel, or
 * other trace payload.
 * Stored as a `uint8_t` field in the metadata blocks (see
 * `trax_meta_type.h`), so values must remain small unsigned integers and
 * never be reordered — the numeric value is part of the on-wire protocol.
 * Add new types at the end only.
 *
 * Why macros instead of an enum:
 *   These identifiers are used most often in file-scope macro invocations
 *   such as `TRAX_VAR_DEFINE(...)`. Several IDEs (including Eclipse
 *   CDT) do not propose enum constants in auto-completion outside function
 *   scope, which made the names awkward to discover where they are needed
 *   most. Preprocessor macros are visible to every editor at every scope.
 *
 * Used by:
 *   - Variables:  TRAX_VAR_DEFINE
 *   - Var streams: TRAX_STREAM_ADC_DEFINE (per-channel element type)
 *   - Logs:       log parameter type (future)
 */
#define TRAX_DATA_TYPE_INT8      0U   /**< 8-bit signed integer            */
#define TRAX_DATA_TYPE_INT16     1U   /**< 16-bit signed integer           */
#define TRAX_DATA_TYPE_INT32     2U   /**< 32-bit signed integer           */
#define TRAX_DATA_TYPE_INT64     3U   /**< 64-bit signed integer           */
#define TRAX_DATA_TYPE_UINT8     4U   /**< 8-bit unsigned integer          */
#define TRAX_DATA_TYPE_UINT16    5U   /**< 16-bit unsigned integer         */
#define TRAX_DATA_TYPE_UINT32    6U   /**< 32-bit unsigned integer         */
#define TRAX_DATA_TYPE_UINT64    7U   /**< 64-bit unsigned integer         */
#define TRAX_DATA_TYPE_FLOAT     8U   /**< 32-bit IEEE 754 floating point  */
#define TRAX_DATA_TYPE_DOUBLE    9U   /**< 64-bit IEEE 754 floating point  */
#define TRAX_DATA_TYPE_BOOL      10U  /**< Boolean (0 = false, non-zero = true) */
#define TRAX_DATA_TYPE_STRING    11U  /**< Null-terminated string          */

/**
 * @brief Byte size of a TRAX_DATA_TYPE_* constant
 *
 * Compile-time constant expression: maps a TRAX_DATA_TYPE_* identifier to
 * the number of bytes one element occupies on the wire and in memory.
 * Returns 0 for variable-size types (STRING) — callers that handle
 * variable-size samples must special-case this.
 *
 * Usage:
 *   uint16_t sz = TRAX_DATA_TYPE_SIZE(TRAX_DATA_TYPE_INT16);  // = 2
 */
#define TRAX_DATA_TYPE_SIZE(t) ( \
    ((t) == TRAX_DATA_TYPE_INT8)   ? 1U : \
    ((t) == TRAX_DATA_TYPE_INT16)  ? 2U : \
    ((t) == TRAX_DATA_TYPE_INT32)  ? 4U : \
    ((t) == TRAX_DATA_TYPE_INT64)  ? 8U : \
    ((t) == TRAX_DATA_TYPE_UINT8)  ? 1U : \
    ((t) == TRAX_DATA_TYPE_UINT16) ? 2U : \
    ((t) == TRAX_DATA_TYPE_UINT32) ? 4U : \
    ((t) == TRAX_DATA_TYPE_UINT64) ? 8U : \
    ((t) == TRAX_DATA_TYPE_FLOAT)  ? 4U : \
    ((t) == TRAX_DATA_TYPE_DOUBLE) ? 8U : \
    ((t) == TRAX_DATA_TYPE_BOOL)   ? 1U : 0U)

/*=============================================================================
 ====================BYTE ORDER================================================
 ============================================================================*/

/**
 * @brief Byte order identifiers for multi-byte stream samples
 *
 * Used by TRAX_STREAM_ADC_DEFINE as the `_byte_order` argument.
 * The value is stored as a `uint8_t` field in the stream metadata block and
 * is part of the on-wire protocol — do not change the numeric values.
 *
 * Most embedded targets (ARM Cortex-M) are little-endian; use
 * TRAX_BYTE_ORDER_LITTLE unless the ADC peripheral or DMA delivers samples
 * in big-endian order.
 */
#define TRAX_BYTE_ORDER_LITTLE   0U   /**< Little-endian (LSB at lowest address) */
#define TRAX_BYTE_ORDER_BIG      1U   /**< Big-endian    (MSB at lowest address) */

#endif /* TRAX_DATA_TYPES_H_ */
