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
 * @file           : trax_frame.h
 * @brief          : TRAX unified frame structure definitions
 * @version        : 3.0.0
 ******************************************************************************
 * @attention
 * 
 * This module provides structured access to TRAX frames for both modes:
 * 
 * Frame Structure (Unified):
 *   [Header: 4 bytes]    - Frame size (words), transaction counter
 *   [Timestamp: 4 bytes] - System timestamp
 *   [ID: 4 bytes]        - Trace ID (remote) OR format address (local)
 *   [Params: N×4 bytes]  - Parameters (0-N)
 * 
 * The same structure (`trax_frame_t`) is used for both modes:
 *   - Remote mode (TRAX_CFG_ENABLE_LOCAL_LOG = 0): id = trace ID
 *   - Local mode  (TRAX_CFG_ENABLE_LOCAL_LOG = 1): id = format address
 * 
 * Helper functions are provided for both interpretations:
 *   - trax_frame_get_id()            - For remote mode
 *   - trax_frame_get_format_string() - For local mode
 * 
 ******************************************************************************
 */

#ifndef TRAX_FRAME_H_
#define TRAX_FRAME_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "trax_compiler.h"
#include "trax_config_default.h"  /* For TRAX_SESSION_KEYWORD, config */
#include "trax_utility.h"         /* For TRAX_PUT32, TRAX_PUT_ARG_AUTO, PP_NARG */
#include "trax_session.h"         /* For TRAX_IS_ENABLED() */
#include "trax_hw.h"
#include "trax_buffer.h"
#include "trax_timestamp.h"

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
====================FRAME COMMIT BARRIER FALLBACK==============================
=============================================================================*/

/**
 * @brief Fallback for ports that do not define TRAX_PORT_COMMIT_BARRIER.
 *
 * The barrier is issued immediately before the Word-0 commit store so that,
 * on multi-core targets, the frame body (timestamp / tid / params) is
 * globally visible before the commit marker — the ring reader
 * (trax_collect_iov) scans lock-free and trusts a non-zero Word 0.
 *
 * Single-core targets need nothing here (the reader cannot preempt a
 * masked-interrupt critical section), so the fallback is empty. The SMP
 * reference ports (ARM_Cortex_M, Xtensa, Zynq) define a real barrier when
 * TRAX_CFG_CORE_COUNT > 1. Custom SMP ports MUST do the same.
 */
#ifndef TRAX_PORT_COMMIT_BARRIER
#define TRAX_PORT_COMMIT_BARRIER()
#endif

/*=============================================================================
====================FRAME MODULE STATE=========================================
=============================================================================*/

/**
 * @brief Transaction counter for frame sequencing (module-owned)
 * 
 * Incremented for each frame created. Used by TRAX_FRAME_ALLOC macros.
 * Declared here for direct access from macros (performance critical).
 */
extern uint16_t trax_trans_cntr;

/**
 * @brief Initialize frame module
 * 
 * Resets transaction counter to 0. Call during system initialization.
 */
void trax_frame_init(void);

/**
 * @brief Reset frame module for trace restart
 * 
 * Resets transaction counter to 0 without affecting any future
 * user-configurable state. Use on restart instead of trax_frame_init().
 */
void trax_frame_reset(void);

/*=============================================================================
====================FRAME TYPE DEFINITIONS====================================
=============================================================================*/


/*=============================================================================
====================FRAME HEADER STRUCTURE====================================
=============================================================================*/

/**
 * @brief TRAX frame header structure (using bit fields)
 * 
 * This structure provides structured access to the frame header fields.
 * Total size: 32 bits (4 bytes)
 *
 * frame_size32 holds the total frame size in 32-bit words (header + timestamp
 * + id + params).  Always >= TRAX_CFG_FRAME_MIN_SIZE32 (3) for committed
 * frames, which guarantees Word 0 is never zero — enabling its use as the
 * atomic commit marker in the ring buffer (0 = uncommitted sentinel).
 *
 * To obtain param_count:  param_count = frame_size32 - TRAX_CFG_FRAME_MIN_SIZE32
 *
 * @warning The bitfield view is a DEBUGGER CONVENIENCE ONLY (watch windows,
 *          host-side little-endian GCC/Clang tooling). On-target code must
 *          decode Word 0 from `.raw` with shifts/masks — bitfield allocation
 *          order within a storage unit is implementation-defined (C11
 *          6.7.2.1p11), so `.fields` is not guaranteed to match the wire
 *          layout on every compiler. The writer composes Word 0 with
 *          shifts/ORs; keep readers symmetric.
 */
struct trax_frame_header_bf_t {
	uint32_t trans_counter   : 16;  /**< Transaction counter (bits 0-15) */
	uint32_t frame_size32    : 16;  /**< Total frame size in words (bits 16-31) */
};

/**
 * @brief Union for accessing frame header as raw value or bit fields
 */
union trax_frame_header_t {
	uint32_t                      	raw;      /**< Raw 32-bit value */
	struct trax_frame_header_bf_t   fields;   /**< Bit field access */
};

/* The header must occupy exactly one 32-bit ring-buffer word. This held
 * before via TRAX_PACKED; it is now asserted instead — a packed attribute on
 * an all-uint32_t struct did nothing on GCC/Clang but was a prefix keyword
 * (__packed) on ARMCC5, i.e. a syntax error in the suffix position used
 * here. Plain structs + asserts are correct on every toolchain. */
TRAX_STATIC_ASSERT(sizeof(struct trax_frame_header_bf_t) == 4,
                   "frame header bitfields must pack into one 32-bit word");
TRAX_STATIC_ASSERT(sizeof(union trax_frame_header_t) == 4,
                   "frame header union must be exactly one 32-bit word");

/*=============================================================================
====================COMPLETE FRAME STRUCTURE==================================
=============================================================================*/

/**
 * @brief Generic TRAX frame structure (variable length)
 * 
 * This structure maps directly to ring buffer memory.
 * The params array is variable length (frame_size32 - TRAX_CFG_FRAME_MIN_SIZE32).
 * Generic structure - id can be trace ID or format string address.
 */
struct trax_frame_t {
	union trax_frame_header_t  header;      /**< Frame header (4 bytes) */
	uint32_t                 timepacked;  /**< Packed encoded timestamp (4 bytes) — format depends on TRAX_CFG_TIMESTAMP_MODE */
	uint32_t                 id;          /**< Trace ID or format address (4 bytes) */
	uint32_t                 p_params[];    /**< Variable length parameters (N×4 bytes) */
};

/* All-uint32_t members: naturally packed, no attribute needed (see the
 * header assert above for why TRAX_PACKED was removed). The assert pins the
 * wire layout: header + timestamp + id = TRAX_CFG_FRAME_MIN_SIZE32 words. */
TRAX_STATIC_ASSERT(sizeof(struct trax_frame_t) == 12,
                   "trax_frame_t fixed part must be exactly 3 wire words");

/*=============================================================================
====================ID WIRE-WORD LAYOUT (SMP)==================================
=============================================================================*/

/**
 * @brief Layout of the per-frame `id` wire word and the helper that
 *        contributes the SMP coreId byte at frame-emission time.
 *
 * Wire layout (little-endian on the wire, MSB-first when described as
 * a 32-bit value):
 *
 *     [ coreId : 8 ][ reserved : 8 ][ traceId : 16 ]
 *       \_ MSB                        \_ LSB
 *
 *   - `coreId`   : 8-bit zero-based CPU core index (0..TRAX_CFG_CORE_COUNT-1).
 *                  Always 0 on single-core builds (the macro folds away).
 *                  Extracted by the host as
 *                  `frame.coreId = (idWord >> 24) & 0xFF`
 *                  (UI/Traxcope/probe/codec/FrameParser.cpp::55) and used
 *                  by MessageDecoder for the per-core monotonicity /
 *                  wrap-reconciliation pass and by the UI to pick the
 *                  swimlane an event lands on.
 *   - `reserved` : Currently zero.  Reserved for future protocol bits;
 *                  do NOT repurpose without bumping the SESSION_START
 *                  protocol version, because the host masks
 *                  `idWord & 0xFFFF` to extract traceId and ignores
 *                  these bits today — silently lighting them up would
 *                  ship semantically-loaded data that older host
 *                  builds can't see.
 *   - `traceId`  : 16-bit TID assigned in trax_tid.h.
 *
 * `TRAX_FRAME_CORE_ID_BITS()` returns the OR-mask that contributes the
 * coreId byte to that layout.  It is OR'd into every emission of `tid`
 * inside TRAX_FRAME_ALLOC / TRAX_FRAME_NOARGS_ATOMIC /
 * TRAX_FRAME_ARGS_ATOMIC so that ALL frame paths — not just one of
 * them — carry the right coreId.  On single-core builds it expands to
 * the constant `0U`, the C compiler folds it into `(tid)`, and the
 * generated machine code is identical to the previous version that
 * had no SMP awareness.
 *
 * The helper deliberately lives next to the frame-emission macros
 * rather than in trax_hw_port.h because the wire-layout
 * decision is a TraxProbe protocol concern, while
 * TRAX_PORT_GET_CORE_ID() — the platform-specific register read — is
 * a hardware-port concern.  Keeping the OR-position here makes the
 * one place a future code reviewer needs to look in order to confirm
 * "yes, every frame is core-tagged" obvious.
 */
#define TRAX_FRAME_CORE_ID_BITS()  \
    (((uint32_t)TRAX_PORT_GET_CORE_ID() & 0xFFU) << 24U)

/*=============================================================================
====================INTERNAL FRAME ALLOCATION MACROS===========================
=============================================================================*/

/**
 * @brief Allocate frame in buffer (internal macro)
 * 
 * Allocates space and writes timestamp + ID, but leaves Word 0 as zero
 * (the uncommitted sentinel).  Word 0 is written atomically during
 * TRAX_FRAME_COMMIT() to serve as the commit marker.
 *
 * Must be paired with TRAX_FRAME_COMMIT().
 * 
 * Variables created:
 *   - p_frame_start: Pointer to frame start / Word 0 (for commit)
 *   - p_wr: Pointer to write position (for parameters)
 *   - frame_size32: Total frame size in 32-bit words
 *   - saved_trans_cntr: Captured transaction counter (written at commit)
 * 
 * @param tid Trace ID
 * @param param_cnt Number of parameters
 */
#define TRAX_FRAME_ALLOC(tid, param_cnt) \
{ \
    uint32_t *p_frame_start = NULL; \
    uint32_t *p_wr; \
    uint16_t frame_size32 = (param_cnt) + TRAX_CFG_FRAME_MIN_SIZE32; \
    uint16_t saved_trans_cntr; \
    TRAX_PORT_ENTER_CRITICAL_SECTION { \
        TRAX_BUFF_ALLOC(p_wr, frame_size32); \
        if (p_wr != NULL) { \
            p_frame_start = p_wr; \
            saved_trans_cntr = trax_trans_cntr; \
            trax_trans_cntr++; \
            p_wr++; /* Skip Word 0 — written during COMMIT */ \
            TRAX_FRAME_TIMEPACKED_PUT(p_wr); \
            TRAX_PUT32(p_wr, (uint32_t)(tid) | TRAX_FRAME_CORE_ID_BITS()); \
        } \
    } \
    TRAX_PORT_EXIT_CRITICAL_SECTION

/**
 * @brief Commit frame to buffer (internal macro)
 * 
 * Writes Word 0 (frame_size32 | trans_counter) as the atomic commit marker.
 * The reader detects committed frames via the non-zero Word 0 sentinel.
 * Must be paired with TRAX_FRAME_ALLOC.
 */
#define TRAX_FRAME_COMMIT() \
    if (p_frame_start != NULL) { \
        TRAX_PORT_ENTER_CRITICAL_SECTION { \
            TRAX_PORT_COMMIT_BARRIER(); /* SMP: body visible before marker */ \
            *p_frame_start = ((uint32_t)frame_size32 << 16U) | saved_trans_cntr; \
        } \
        TRAX_PORT_EXIT_CRITICAL_SECTION; \
    } \
}

/*=============================================================================
====================FRAME CREATION MACROS======================================
=============================================================================*/

/**
 * @brief Frame creation with variable arguments (non-atomic)
 * 
 * Usage: TRAX_FRAME_ARGS(tid, arg1, arg2, ...)
 * 
 * This macro creates a TRAX frame with variable arguments and automatic
 * argument counting. Used for any frame type that needs variable arguments
 * and can tolerate interrupts.
 * 
 * NOTE: This macro releases the lock between allocation and finalization,
 * allowing interrupts during parameter writing. For small frames where
 * atomic operation is desired, use TRAX_FRAME_ARGS_ATOMIC instead.
 * 
 * NOTE: Only writes when STREAMING (not IDLE). For protocol responses
 * that must work in any state, use TRAX_FRAME_RESPONSE().
 * 
 * @param tid Trace ID
 * @param ... Variable arguments (auto-counted)
 */
#ifndef TRAX_FRAME_ARGS
#define TRAX_FRAME_ARGS(tid, ...) \
    if (TRAX_IS_SESSION_ACTIVE()) { \
	    TRAX_FRAME_ALLOC(tid, PP_NARG(__VA_ARGS__)) \
	    if (p_wr != NULL) { \
		TRAX_PUT_ARG_AUTO(p_wr, ##__VA_ARGS__); \
	    } \
	    TRAX_FRAME_COMMIT() \
    } else { \
	    TRAX_GAP_NOTE_SKIP(); \
    }
#endif

/**
 * @brief Atomic frame creation with variable arguments
 * 
 * Usage: TRAX_FRAME_ARGS_ATOMIC(tid, arg1, arg2, ...)
 * 
 * This macro keeps the entire frame creation (allocation, writing, finalization)
 * within a single critical section. This provides:
 * - Better performance for small frames (avoids lock/unlock overhead)
 * - Atomic frame writes (no interruption between start and completion)
 * - More deterministic timing
 * 
 * RECOMMENDED FOR:
 * - Log messages with few parameters (0-8 params)
 * - Signal updates
 * - Events and status updates
 * - Any small frame where interrupt latency is acceptable
 * 
 * NOT RECOMMENDED FOR:
 * - Large data transfers (ADC streams, buffers)
 * - Frames with > 8 parameters
 * - Cases where extended interrupt disable is problematic
 * 
 * NOTE: Only writes when STREAMING (not IDLE). For protocol responses
 * that must work in any state, use TRAX_FRAME_RESPONSE().
 * 
 * WARNING: The variadic arguments are evaluated INSIDE the critical
 * section, between the frame allocation and the Word-0 commit. They must
 * be ISR-safe, side-effect-free expressions. In particular, never call a
 * task-level RTOS API in an argument: FreeRTOS taskEXIT_CRITICAL() (used
 * internally by most non-FromISR APIs) unconditionally re-enables
 * interrupts when its own nesting count reaches zero, blowing the
 * TraxProbe critical section open mid-frame. The frame then commits late
 * with a stale trans_counter and the validator kills the stream with
 * FRAME_CORRUPT. TraxProbe manages the interrupt mask itself, so the
 * macro body runs in an ISR-like context — use the query-style FromISR
 * variants (e.g. uxQueueMessagesWaitingFromISR()), or evaluate the value
 * into a local variable BEFORE the macro. On BASEPRI-based FreeRTOS ports
 * (Cortex-M3/M4/M7) the task-level call is accidentally harmless, on
 * PRIMASK-based ports (Cortex-M0/M0+/M23) it corrupts the stream — do not
 * rely on the former. See docs/PITFALL_RTOS_CALLS_IN_FRAME_ARGUMENTS.md.
 * 
 * @param tid Trace ID
 * @param ... Variable arguments (auto-counted)
 */
/**
 * @brief Emit a minimal frame (header + timestamp + TID, no parameters) atomically.
 *
 * Specialisation of TRAX_FRAME_ARGS_ATOMIC for zero parameters.
 * Avoids PP_NARG(__VA_ARGS__) which is unreliable with an empty argument list
 * in strict C99/C11. frame_size32 is hardcoded to TRAX_CFG_FRAME_MIN_SIZE32.
 *
 * Only emits when TRAX_IS_SESSION_ACTIVE(). Used by TRAX_MARKER_START/STOP.
 *
 * @param tid  Trace ID
 */
#ifndef TRAX_FRAME_NOARGS_ATOMIC
#define TRAX_FRAME_NOARGS_ATOMIC(tid) \
{ \
    if (TRAX_IS_SESSION_ACTIVE()) { \
        uint32_t *p_frame_start = NULL; \
        uint32_t *p_wr; \
        uint16_t frame_size32 = TRAX_CFG_FRAME_MIN_SIZE32; \
        TRAX_PORT_ENTER_CRITICAL_SECTION { \
            TRAX_BUFF_ALLOC(p_wr, frame_size32); \
            if (p_wr != NULL) { \
                p_frame_start = p_wr; \
                p_wr++; \
                TRAX_FRAME_TIMEPACKED_PUT(p_wr); \
                TRAX_PUT32(p_wr, (uint32_t)(tid) | TRAX_FRAME_CORE_ID_BITS()); \
                TRAX_PORT_COMMIT_BARRIER(); /* SMP: body visible before marker */ \
                *p_frame_start = ((uint32_t)frame_size32 << 16U) | trax_trans_cntr; \
                trax_trans_cntr++; \
            } \
        } \
        TRAX_PORT_EXIT_CRITICAL_SECTION; \
    } else { \
        TRAX_GAP_NOTE_SKIP(); \
    } \
}
#endif

#ifndef TRAX_FRAME_ARGS_ATOMIC
#define TRAX_FRAME_ARGS_ATOMIC(tid, ...) \
{ \
	if (TRAX_IS_SESSION_ACTIVE()) { \
		uint32_t *p_frame_start = NULL; \
		uint32_t *p_wr; \
		uint16_t frame_size32 = PP_NARG(__VA_ARGS__) + TRAX_CFG_FRAME_MIN_SIZE32; \
		TRAX_PORT_ENTER_CRITICAL_SECTION { \
			TRAX_BUFF_ALLOC(p_wr, frame_size32); \
			if (p_wr != NULL) { \
				p_frame_start = p_wr; \
				p_wr++; /* Skip Word 0 */ \
				TRAX_FRAME_TIMEPACKED_PUT(p_wr); \
				TRAX_PUT32(p_wr, (uint32_t)(tid) | TRAX_FRAME_CORE_ID_BITS()); \
				TRAX_PUT_ARG_AUTO(p_wr, ##__VA_ARGS__); \
			TRAX_PORT_COMMIT_BARRIER(); /* SMP: body visible before marker */ \
			*p_frame_start = ((uint32_t)frame_size32 << 16U) | trax_trans_cntr; \
			trax_trans_cntr++; \
			} \
		} \
		TRAX_PORT_EXIT_CRITICAL_SECTION; \
	} else { \
		TRAX_GAP_NOTE_SKIP(); \
	} /* End TRAX_IS_SESSION_ACTIVE() */ \
}
#endif

/**
 * @brief Frame creation with raw memory data and variable arguments (non-atomic)
 * 
 * Usage: TRAX_FRAME_RAW_ARGS(tid, p_mem, mem_size, arg1, arg2, ...)
 * 
 * This macro creates a frame with:
 * - Variable arguments (processed first)
 * - Raw memory data (appended after arguments)
 * 
 * Frame Structure:
 *   - Param 1-N: Variable arguments
 *   - Data: Raw memory data (variable length, padded to 32-bit boundary)
 * 
 * NOTE: This macro releases the lock between allocation and finalization,
 * allowing interrupts during data writing. This is acceptable for large
 * data transfers (hex dumps, streams) where interrupt latency is less critical.
 * 
 * NOTE: Only writes when STREAMING (not IDLE). For protocol responses
 * that must work in any state, use TRAX_FRAME_RESPONSE().
 * 
 * Applications should use higher-level APIs:
 *   - TRAX_LOG_XXX: Regular logging
 *   - TRAX_HEX_XXX: Hex dump logging  
 *   - TRAX_STREAM_UPDATE/TRAX_STREAM_SEND_ALL/TRAX_STREAM_SEND: Variable-stream data
 * 
 * Related macros:
 *   - TRAX_FRAME_ARGS: Frame with variable arguments only
 *   - TRAX_FRAME_RAW: Frame with raw memory only
 * 
 * @param tid Trace ID
 * @param p_mem Pointer to memory data to copy
 * @param mem_size Size of memory data in bytes (padded to 32-bit boundary)
 * @param ... Variable arguments (auto-counted, written before memory data)
 */
#ifndef TRAX_FRAME_RAW_ARGS
#define TRAX_FRAME_RAW_ARGS(tid, p_mem, mem_size, ...) \
{ \
    if (TRAX_IS_SESSION_ACTIVE()) { \
        uint32_t mem_size_val = (uint32_t)(mem_size); \
        uint16_t mem_size32 = (mem_size_val + 3) / 4; \
        uint16_t arg_count = PP_NARG(__VA_ARGS__); \
        uint16_t total_param_count = arg_count + mem_size32; \
        TRAX_FRAME_ALLOC(tid, total_param_count) \
        if (p_wr != NULL) { \
            /* Write variable arguments first */ \
            TRAX_PUT_ARG_AUTO(p_wr, ##__VA_ARGS__); \
            /* Write memory data after arguments */ \
            memcpy(p_wr, p_mem, mem_size_val); \
            /* Zero the tail padding bytes inside the last 32-bit \
             * word. The frame slot is allocated word-granular but \
             * only mem_size_val bytes are user data — the remaining \
             * (4 - mem_size_val % 4) bytes would otherwise leak \
             * whatever was previously in this RTT ring slot, \
             * which corrupts NUL-implied string payloads on the \
             * host side (e.g. task names like "worker" rendered as \
             * "worker\xNN"). Cost: zero or one short memset per \
             * frame, only when mem_size is not 4-aligned. */ \
            uint32_t pad = (4u - (mem_size_val & 3u)) & 3u; \
            if (pad != 0u) { \
                memset((uint8_t *)p_wr + mem_size_val, 0, pad); \
            } \
        } \
        TRAX_FRAME_COMMIT() \
    } else { \
        TRAX_GAP_NOTE_SKIP(); \
    } /* End TRAX_IS_SESSION_ACTIVE() */ \
}
#endif

/*=============================================================================
====================PROTOCOL FRAME MACROS (NO STATE CHECK)====================
=============================================================================*/

/**
 * @brief Frame creation for protocol responses (no state check)
 * 
 * Usage: TRAX_FRAME_ARGS_PROTOCOL(tid, arg1, arg2, ...)
 * 
 * Similar to TRAX_FRAME_ARGS but WITHOUT the TRAX_IS_SESSION_ACTIVE() check.
 * Use this for protocol frames that must be sent regardless of state:
 * - Metadata frames (in response to CMD_GET_METADATA)
 * - Trace start/stop markers
 * - Any frame that is part of the protocol, not user trace data
 * 
 * @note For small ACK/NAK responses, prefer TRAX_FRAME_RESPONSE() in
 *       trax_cmd_protocol.c which sends directly without buffering.
 * 
 * @param tid Trace ID
 * @param ... Variable arguments (auto-counted)
 */
#define TRAX_FRAME_ARGS_PROTOCOL(tid, ...) \
    { \
	    TRAX_FRAME_ALLOC(tid, PP_NARG(__VA_ARGS__)) \
	    if (p_wr != NULL) { \
		TRAX_PUT_ARG_AUTO(p_wr, ##__VA_ARGS__); \
	    } \
	    TRAX_FRAME_COMMIT() \
    }

/**
 * @brief Frame creation with raw data for protocol responses (no state check)
 * 
 * Usage: TRAX_FRAME_RAW_ARGS_PROTOCOL(tid, p_mem, mem_size, arg1, arg2, ...)
 * 
 * Similar to TRAX_FRAME_RAW_ARGS but WITHOUT the TRAX_IS_SESSION_ACTIVE() check.
 * Use this for protocol frames with raw data that must be sent regardless of state:
 * - Metadata frames containing struct data
 * - Build information, signal/log/stream metadata
 * 
 * @param tid Trace ID
 * @param p_mem Pointer to memory data to copy
 * @param mem_size Size of memory data in bytes (padded to 32-bit boundary)
 * @param ... Variable arguments (auto-counted, written before memory data)
 */
#define TRAX_FRAME_RAW_ARGS_PROTOCOL(tid, p_mem, mem_size, ...) \
{ \
    uint32_t mem_size_val = (uint32_t)(mem_size); \
    uint16_t mem_size32 = (mem_size_val + 3) / 4; \
    uint16_t arg_count = PP_NARG(__VA_ARGS__); \
    uint16_t total_param_count = arg_count + mem_size32; \
    TRAX_FRAME_ALLOC(tid, total_param_count) \
    if (p_wr != NULL) { \
        /* Write variable arguments first */ \
        TRAX_PUT_ARG_AUTO(p_wr, ##__VA_ARGS__); \
        /* Write memory data after arguments */ \
        memcpy(p_wr, p_mem, mem_size_val); \
        /* Zero the tail padding bytes — see the matching comment in \
         * TRAX_FRAME_RAW_ARGS above. Critical for TRAX_TID_TASK_CREATE / \
         * TRAX_TID_OBJ_NAME frames where the wire payload is a string \
         * whose end is implied by the frame size: stale bytes in \
         * the final word render as trailing garbage glyphs in the \
         * host UI. */ \
        uint32_t pad = (4u - (mem_size_val & 3u)) & 3u; \
        if (pad != 0u) { \
            memset((uint8_t *)p_wr + mem_size_val, 0, pad); \
        } \
    } \
    TRAX_FRAME_COMMIT() \
}

/*=============================================================================
====================USAGE EXAMPLES============================================
=============================================================================*/

/**
 * @example Frame Access Examples
 * 
 * Generic frame access:
 * ```c
 * // Cast to generic frame structure
 * struct trax_frame *frame = trax_frame_cast(p_ring_buffer);
 * 
 * // Check frame type
 * if (trax_frame_is_addr_based(frame)) {
 *     printf("Address-based log\n");
 *     printf("  Format addr: 0x%08X\n", trax_frame_get_format_addr(frame));
 *     printf("  Format string: \"%s\"\n", trax_frame_get_format_string(frame));
 * } else {
 *     printf("ID-based log\n");
 *     printf("  Trace ID: %u\n", trax_frame_get_id(frame));
 * }
 * 
 * // Get parameters (works for both types)
 * uint16_t param_count = trax_frame_get_param_count(frame);
 * for (int i = 0; i < param_count; i++) {
 *     uint32_t param = trax_frame_get_param(frame, i);
 *     printf("  Param[%d] = %u\n", i, param);
 * }
 * ```
 * 
 * Address-based log frame access (when you know it's address-based):
 * ```c
 * // Cast directly to log frame structure (when bit 31 = 1)
 * struct trax_log_frame *log = trax_log_frame_cast(p_ring_buffer);
 * 
 * // Access address-specific fields directly
 * const char *fmt = trax_log_frame_get_format_string(log);
 * uint16_t param_count = trax_log_frame_get_param_count(log);
 * 
 * // Handle any number of parameters (no limit)
 * for (int i = 0; i < param_count; i++) {
 *     uint32_t param = trax_log_frame_get_param(log, i);
 *     printf("  Param[%d] = %u\n", i, param);
 * }
 * ```
 */

#ifdef __cplusplus
}
#endif

#endif /* TRAX_FRAME_H_ */
