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
 * @file           : trax_utility.h
 * @brief          : TRAX utility macros for data conversion and writing
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 * 
 * This file contains low-level utility macros for TraxProbe:
 *   - Data conversion macros (TRAX_32F, TRAX_64_PAIR, etc.)
 *   - Data write macros (TRAX_PUT32, TRAX_PUT_ARG_AUTO, etc.)
 *   - Argument counting macros (PP_NARG)
 * 
 * Frame allocation/commit macros (TRAX_FRAME_ALLOC, TRAX_FRAME_COMMIT)
 * are defined in trax_frame.h.
 * 
 ******************************************************************************
 */

#ifndef TRAX_UTILITY_H_
#define TRAX_UTILITY_H_

#include <stdint.h>
#include "trax_config_default.h"

/* Note: Do NOT include trax_frame.h, trax_stream.h, trax_timestamp.h here!
 * Those headers include trax_utility.h, creating circular dependencies.
 * This file should only contain standalone utility macros. */

/*=============================================================================
 ====================DATA CONVERSION MACROS====================================
 =============================================================================*/

/**
 * @brief Convert float to uint32_t (bit-preserving)
 * @param x Float value
 * @return uint32_t with same bit pattern
 */
#define TRAX_32F(x) \
    (((union { float f; uint32_t u; }){ .f = (x) }).u)

/**
 * @brief Convert double to two uint32_t values (high, low)
 * @param x Double value
 * @return Two comma-separated uint32_t values (high word, low word)
 *
 * @note Same caveat as TRAX_64_PAIR: cannot be used directly inside variadic
 *       TRAX_LOG_x / TRAX_FRAME_ARGS_ATOMIC macros because the comma between
 *       the two words travels through ##__VA_ARGS__ unexpanded and ends up
 *       inside TRAX_PUT_ARG_1, breaking TRAX_PUT32 (which only takes 2 args).
 *       Use TRAX_64F_HI / TRAX_64F_LO instead (see below).
 */
#define TRAX_64F_PAIR(x) \
    ((uint32_t)((((union { double d; uint64_t u; }){ .d = (x) }).u) >> 32)), \
    ((uint32_t)(((union { double d; uint64_t u; }){ .d = (x) }).u & 0xFFFFFFFF))

/**
 * @brief High 32-bit word of a double (IEEE 754 bit pattern).
 * @param x double value
 *
 * Single-expression counterpart to TRAX_64F_PAIR for use as a frame argument.
 * Pass the high word first, then the low word — most-significant first, the
 * same order you would write the number:
 *
 *   TRAX_VAR_SET_DOUBLE(VAR_TID, val);
 *   TRAX_LOG_INFO(TID, "tag", "v=%lf", TRAX_64F_HI(val), TRAX_64F_LO(val));
 */
#define TRAX_64F_HI(x) \
    ((uint32_t)((((union { double d; uint64_t u; }){ .d = (x) }).u) >> 32U))

/**
 * @brief Low 32-bit word of a double (IEEE 754 bit pattern).
 * @param x double value
 * @see TRAX_64F_HI
 */
#define TRAX_64F_LO(x) \
    ((uint32_t)(((union { double d; uint64_t u; }){ .d = (x) }).u & 0xFFFFFFFFULL))

/**
 * @brief Cast value to uint32_t
 * @param x Value to cast
 * @return uint32_t value
 */
#define TRAX_32(x) ((uint32_t)(x))

/**
 * @brief Convert uint64_t to two uint32_t values (high, low)
 * @param x 64-bit value
 * @return Two comma-separated uint32_t values (high word, low word)
 *
 * @note Cannot be used directly inside TRAX_LOG_x macros: the comma between
 *       the two words is seen by the C preprocessor as an extra macro argument,
 *       which breaks PP_NARG argument counting and causes a compile error.
 *       Use TRAX_64_HI / TRAX_64_LO instead (see below).
 */
#define TRAX_64_PAIR(x) \
    ((uint32_t)(((x) >> 32))), \
    ((uint32_t)((x) & 0xFFFFFFFF))

/**
 * @brief High 32-bit word of a 64-bit value, for use as a log argument.
 * @param x uint64_t value
 *
 * TRAX log frames carry 32-bit words. Pass a 64-bit argument as two adjacent
 * words — high word first, then low word (most-significant first, the same
 * order you would write the number) — and use %llu in the format string so
 * Traxcope can reassemble them on the host side:
 *
 *   TRAX_LOG_INFO(TID, "tag", "bytes=%llu", TRAX_64_HI(val), TRAX_64_LO(val));
 *
 * Unlike TRAX_64_PAIR(), each macro expands to a single expression (no internal
 * comma), so PP_NARG counts the arguments correctly.
 */
#define TRAX_64_HI(x)  ((uint32_t)((uint64_t)(x) >> 32U))

/**
 * @brief Low 32-bit word of a 64-bit value, for use as a log argument.
 * @param x uint64_t value
 * @see TRAX_64_HI
 */
#define TRAX_64_LO(x)  ((uint32_t)((uint64_t)(x) & 0xFFFFFFFFULL))

/*=============================================================================
 ====================DATA WRITE MACROS=========================================
 =============================================================================*/

/**
 * @brief Write a 32-bit value and advance pointer
 * @param p_wr Pointer to write location (will be incremented)
 * @param x Value to write
 */
#define TRAX_PUT32(p_wr, x) \
    do { *p_wr++ = (x); } while(0)

/* Zero arguments - do nothing */
#define TRAX_PUT_ARG_0(p_wr) \
    do { (void)(p_wr); } while(0)

#define TRAX_PUT_ARG_1(p_wr, v0) \
    TRAX_PUT32(p_wr, v0)

#define TRAX_PUT_ARG_2(p_wr, v0, v1) \
    TRAX_PUT_ARG_1(p_wr, v0); \
    TRAX_PUT_ARG_1(p_wr, v1)

#define TRAX_PUT_ARG_3(p_wr, v0, v1, v2) \
    TRAX_PUT_ARG_2(p_wr, v0, v1); \
    TRAX_PUT_ARG_1(p_wr, v2)

#define TRAX_PUT_ARG_4(p_wr, v0, v1, v2, v3) \
    TRAX_PUT_ARG_3(p_wr, v0, v1, v2); \
    TRAX_PUT_ARG_1(p_wr, v3)

#define TRAX_PUT_ARG_5(p_wr, v0, v1, v2, v3, v4) \
    TRAX_PUT_ARG_4(p_wr, v0, v1, v2, v3); \
    TRAX_PUT_ARG_1(p_wr, v4)

#define TRAX_PUT_ARG_6(p_wr, v0, v1, v2, v3, v4, v5) \
    TRAX_PUT_ARG_5(p_wr, v0, v1, v2, v3, v4); \
    TRAX_PUT_ARG_1(p_wr, v5)

#define TRAX_PUT_ARG_7(p_wr, v0, v1, v2, v3, v4, v5, v6) \
    TRAX_PUT_ARG_6(p_wr, v0, v1, v2, v3, v4, v5); \
    TRAX_PUT_ARG_1(p_wr, v6)

#define TRAX_PUT_ARG_8(p_wr, v0, v1, v2, v3, v4, v5, v6, v7) \
    TRAX_PUT_ARG_7(p_wr, v0, v1, v2, v3, v4, v5, v6); \
    TRAX_PUT_ARG_1(p_wr, v7)

#define TRAX_PUT_ARG_9(p_wr, v0, v1, v2, v3, v4, v5, v6, v7, v8) \
    TRAX_PUT_ARG_8(p_wr, v0, v1, v2, v3, v4, v5, v6, v7); \
    TRAX_PUT_ARG_1(p_wr, v8)

#define TRAX_PUT_ARG_10(p_wr, v0, v1, v2, v3, v4, v5, v6, v7, v8, v9) \
    TRAX_PUT_ARG_9(p_wr, v0, v1, v2, v3, v4, v5, v6, v7, v8); \
    TRAX_PUT_ARG_1(p_wr, v9)


/*=============================================================================
 ====================ARGUMENT ROUTER MACRO=====================================
 =============================================================================*/

/**
 * @brief Helper macro to select the correct TRAX_PUT_ARG_N macro
 */
#define GET_TRAX_ARG_MACRO(_0,_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,NAME, ...) NAME

/**
 * @brief Automatically write 0-10 arguments based on count
 * @param p_wr Pointer to write location
 * @param ... Variable arguments (0-10)
 */
#define TRAX_PUT_ARG_AUTO(p_wr, ...) \
    GET_TRAX_ARG_MACRO(_0, ##__VA_ARGS__, \
	TRAX_PUT_ARG_10, TRAX_PUT_ARG_9, \
        TRAX_PUT_ARG_8, TRAX_PUT_ARG_7, TRAX_PUT_ARG_6, TRAX_PUT_ARG_5, \
        TRAX_PUT_ARG_4, TRAX_PUT_ARG_3, TRAX_PUT_ARG_2, TRAX_PUT_ARG_1, \
        TRAX_PUT_ARG_0)(p_wr, ##__VA_ARGS__)

/*=============================================================================
 ====================ARGUMENT COUNTING MACROS==================================
 =============================================================================*/

/**
 * @brief Count number of arguments (1-10)
 * @param ... Variable arguments
 * @return Number of arguments
 *
 * MUST cover the same range as TRAX_PUT_ARG_AUTO (10). If the counter
 * saturates below the writer's range, an 11-argument call would still
 * compile but return the 11th ARGUMENT ITSELF as the "count" — which then
 * becomes frame_size32 and silently corrupts the ring. Keep both tables in
 * sync when extending.
 *
 * Known limitation (documented, by design): zero arguments yields 1, not 0.
 * Zero-parameter frames must use TRAX_FRAME_NOARGS_ATOMIC instead.
 */
#define PP_NARG(...)  PP_NARG_(__VA_ARGS__, PP_RSEQ_N())
#define PP_NARG_(...) PP_ARG_N(__VA_ARGS__)
#define PP_ARG_N(_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,N,...) N
#define PP_RSEQ_N()   10,9,8,7,6,5,4,3,2,1,0

/**
 * @brief Helper macro that returns the Nth argument
 * @note Used to get a specific argument position (extendible to more args)
 */
#define TRAX_NTH_ARG(arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10, arg11, arg12, arg13, arg14, ...) arg14

/**
 * @brief Count number of arguments (0-12)
 * @param ... Variable arguments
 * @return Number of arguments (supports up to 12)
 */
#define TRAX_ARG_CNT(...) TRAX_NTH_ARG(dummy, ## __VA_ARGS__, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)

/*=============================================================================
 ====================TOKEN CONCATENATION MACROS================================
 =============================================================================*/

/**
 * @brief Concatenate two tokens
 */
#define TRAX_CONCAT_2(a, b)         a ## b

/**
 * @brief Concatenate three tokens
 */
#define TRAX_CONCAT_3(a, b, c)      a ## b ## c

/**
 * @brief Concatenate two tokens with expansion
 * 
 * Concatenates two tokens and ensures the result is expanded as a
 * callable macro or function. Useful for dynamically generating
 * function or macro names based on input parameters.
 */
#define TRAX_CONCAT_2_AND_EXPAND(a, b) TRAX_CONCAT_2(a, b)

/**
 * @brief Strip the outer parentheses from a token list.
 *
 * The C preprocessor splits function-like macro arguments on top-level
 * commas regardless of brace nesting (`{a, b}` is two arguments, not
 * one). To pass a multi-element brace-initialiser to a macro, wrap the
 * payload in PARENTHESES at the call site and unwrap with TRAX_UNPAREN
 * before placing inside `{ ... }` in the expansion.
 *
 * Example:
 *   #define DEFINE_ARR(name, lit) static const float name[] = { TRAX_UNPAREN lit }
 *   DEFINE_ARR(coeffs, (0.1f, 0.2f, 0.3f));
 *     -> static const float coeffs[] = { 0.1f, 0.2f, 0.3f };
 */
#define TRAX_UNPAREN(...)               __VA_ARGS__

/*=============================================================================
 ====================STRING UTILITIES==========================================
 =============================================================================*/

/**
 * @brief Bounded string length — no C library dependency.
 * @param s   Pointer to null-terminated string
 * @param max Maximum number of characters to scan
 * @return    Number of characters before the null terminator, capped at max
 */
static inline uint8_t trax_strnlen(const char *s, uint8_t max)
{
    uint8_t len = 0;
    while (len < max && s[len] != '\0') { len++; }
    return len;
}

/**
 * @brief Copy string with zero-padding — no C library dependency.
 * @param dst   Destination buffer
 * @param src   Source null-terminated string
 * @param size  Total size of destination buffer
 *
 * Copies up to (size - 1) characters from src, then zero-fills the rest.
 */
static inline void trax_strncpy_pad(char *dst, const char *src, uint32_t size)
{
    uint32_t i;
    for (i = 0; i < size && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    for (; i < size; i++) {
        dst[i] = '\0';
    }
}

/*=============================================================================
 ====================FRAME MACROS (in trax_frame.h)=============================
 =============================================================================*/

/* Frame macros are defined in trax_frame.h:
 *   - TRAX_FRAME_ALLOC      : Allocate frame in buffer (internal)
 *   - TRAX_FRAME_COMMIT     : Commit frame to buffer (internal)
 *   - TRAX_FRAME_ARGS       : Create frame with variable args (non-atomic)
 *   - TRAX_FRAME_ARGS_ATOMIC: Create frame with variable args (atomic)
 *   - TRAX_FRAME_RAW_ARGS   : Create frame with raw memory + args
 */

#endif /* TRAX_UTILITY_H_ */
