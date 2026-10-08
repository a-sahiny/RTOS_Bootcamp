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
 * @file           : trax_compiler.h
 * @brief          : Compiler portability macros for TraxProbe
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 *
 * Centralises every compiler-specific construct used across TraxProbe.
 * Any header that needs one of these macros must include this file
 * rather than using raw __attribute__ or built-ins directly.
 *
 * Supported toolchains:
 *   - GCC (arm-none-eabi and native)
 *   - Clang / LLVM  (including armclang / ARM Compiler 6)
 *   - IAR Embedded Workbench (EWARM, v8+)
 *   - ARM Compiler 5 (Keil MDK, armcc)
 *   - MSVC (host-side builds only)
 *
 * ─── Language / preprocessor requirements (beyond strict C99) ───────────────
 *
 * 1. `, ##__VA_ARGS__` (GNU comma-swallowing paste)
 *    The variadic frame macros (TRAX_FRAME_ARGS*, TRAX_PUT_ARG_AUTO call
 *    sites) use `##__VA_ARGS__` so an EMPTY argument list drops the leading
 *    comma. This GNU extension is accepted by GCC, Clang/armclang, IAR (v8+)
 *    and armcc — i.e. every supported embedded toolchain — but it is NOT
 *    ISO C99/C11 and MSVC's traditional preprocessor handles empty variadic
 *    lists by different (coincidentally compatible) rules. A strictly
 *    conforming preprocessor would reject it; C23's `__VA_OPT__` is the
 *    standard replacement once all supported toolchains speak C23.
 *    Practical rule: zero-parameter frames should use the dedicated
 *    TRAX_FRAME_NOARGS_ATOMIC / non-variadic entry points anyway (PP_NARG
 *    cannot count zero arguments — see trax_utility.h).
 *
 * 2. NO GNU statement expressions
 *    `({ ... })` is deliberately NOT used anywhere in TraxProbe (ports
 *    included) — it is GCC/Clang-only and unconditionally rejected by IAR,
 *    armcc and MSVC. Multi-statement expression-position helpers are
 *    implemented as `static inline` functions instead (e.g. the ports'
 *    trax_hw_port_timepacked_get32()). Keep it that way in new code.
 *
 * 3. Flexible array member (`uint32_t p_params[];` in trax_frame.h)
 *    Standard C99; supported by all toolchains above (MSVC accepts it in
 *    C mode). Do not replace it with the pre-C99 `[0]`/`[1]` hacks.
 *
 * 4. Bitfields are NEVER used for wire-format access on target
 *    Bitfield allocation order within a storage unit is implementation-
 *    defined (C11 6.7.2.1p11). Wire words are composed and decoded with
 *    shifts/masks; bitfield structs exist only as debugger-watch views.
 *
 ******************************************************************************
 */

#ifndef TRAX_COMPILER_H_
#define TRAX_COMPILER_H_

/*=============================================================================
 ====================STRUCT PACKING============================================
 ============================================================================*/

/**
 * @brief Suppress padding in a wire-format struct.
 *
 * Wire-format structs must be packed to match the protocol byte layout.
 * An empty fallback compiles silently but produces mis-aligned fields —
 * always prefer a visible warning over silent data corruption.
 *
 * Usage (GCC / Clang / IAR / ARMCC6 — suffix after closing brace):
 *   struct my_frame_t { ... } TRAX_PACKED;
 *
 * ARMCC5 note: __packed is a prefix keyword, so structs must be written as:
 *   TRAX_PACKED struct my_frame_t { ... };
 */
#if defined(__GNUC__) || defined(__clang__)
    /* GCC, Clang, and ARM Compiler 6 (armclang defines both __clang__ and __ARMCC_VERSION) */
    #define TRAX_PACKED         __attribute__((packed))

#elif defined(__IAR_SYSTEMS_ICC__)
    /* IAR Embedded Workbench v8+ */
    #define TRAX_PACKED         __attribute__((packed))

#elif defined(__ARMCC_VERSION)
    /* ARM Compiler 5 (Keil MDK — armcc, not armclang) */
    #define TRAX_PACKED         __packed

#elif defined(_MSC_VER)
    /* MSVC — host-side compilation only */
    #define TRAX_PACKED
    #pragma message("WARNING: TRAX_PACKED is not effective on MSVC. Wire-format structs will not be packed.")

#else
    #define TRAX_PACKED
    #warning "Unsupported compiler: TRAX_PACKED has no effect. Wire-format structs may be mis-aligned."
#endif


/*=============================================================================
 ====================ELF SECTION PLACEMENT====================================
 ============================================================================*/

/**
 * @brief Place a symbol into a named ELF output section.
 *
 * Used to embed metadata objects that the host tool extracts from the binary.
 *   - 'used'   : prevents the linker from discarding unreferenced symbols
 *   - 'unused' : suppresses the compiler warning about unused variables
 *   - 'section': places the symbol in the named output section
 *
 * On compilers that do not support section placement the macro expands to
 * nothing — metadata will not be embedded in the binary for those targets.
 */
#if defined(__GNUC__) || defined(__clang__)
    #define TRAX_SECTION(name)  __attribute__((used, unused, section(name)))

#elif defined(__IAR_SYSTEMS_ICC__)
    #define TRAX_SECTION(name)  __attribute__((section(name)))

#elif defined(__ARMCC_VERSION)
    #define TRAX_SECTION(name)  __attribute__((section(name)))

#else
    #define TRAX_SECTION(name)
#endif


/*=============================================================================
 ====================ALIGNMENT================================================
 ============================================================================*/

/**
 * @brief Request minimum n-byte alignment for a variable or struct member.
 *
 * Used for the ring buffer memory which must be 8-byte aligned for safe
 * 64-bit access on Cortex-M and for DMA transfers.
 */
#if defined(__GNUC__) || defined(__clang__)
    #define TRAX_ALIGNED(n)     __attribute__((aligned(n)))

#elif defined(__IAR_SYSTEMS_ICC__)
    #define TRAX_ALIGNED(n)     __attribute__((aligned(n)))

#elif defined(__ARMCC_VERSION)
    #define TRAX_ALIGNED(n)     __attribute__((aligned(n)))

#elif defined(_MSC_VER)
    #define TRAX_ALIGNED(n)     __declspec(align(n))

#else
    #define TRAX_ALIGNED(n)
    #warning "Unsupported compiler: TRAX_ALIGNED has no effect."
#endif


/*=============================================================================
 ====================COMPILE-TIME ASSERTIONS==================================
 ============================================================================*/

/**
 * @brief Portable compile-time assertion.
 *
 * Selection hierarchy:
 *   1. C++11  static_assert   — reports the msg string on failure
 *   2. C11   _Static_assert   — reports the msg string on failure
 *   3. Negative-array-size typedef (C99 / C89 fallback) — the error
 *      message reads "array has negative size" but the failing line is
 *      clearly identified by the compiler.
 *
 * The two-level macro indirection for __LINE__ forces the preprocessor
 * to expand the line number before token concatenation, guaranteeing
 * unique typedef names when the macro is used multiple times in a file.
 */
#if defined(__cplusplus) && (__cplusplus >= 201103L)
    #define TRAX_STATIC_ASSERT(cond, msg)       static_assert(cond, msg)

#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
    #define TRAX_STATIC_ASSERT(cond, msg)       _Static_assert(cond, msg)

#else
    /* C99 / C89 fallback */
    #define TRAX_SA_CONCAT2(a, b)               a##b
    #define TRAX_SA_CONCAT(a, b)                TRAX_SA_CONCAT2(a, b)
    #define TRAX_STATIC_ASSERT(cond, msg) \
        typedef char TRAX_SA_CONCAT(trax_static_assert_, __LINE__)[(cond) ? 1 : -1]
#endif

#endif /* TRAX_COMPILER_H_ */
