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
 * @file           : trax_config_buffer.h
 * @brief          : TRAX Buffer Configuration
 * @version        : 2.0.0
 ******************************************************************************
 * @attention
 * 
 * This file contains buffer-related configuration:
 *   - Output ring buffer size
 *   - Input buffer size
 *   - Frame size limits (min/max)
 *   - Processing mode (Remote/Local)
 * 
 * DO NOT MODIFY THIS FILE!
 * To override, define values in App/Config/trax_config.h
 * 
 ******************************************************************************
 */

#ifndef TRAX_CONFIG_BUFFER_H_
#define TRAX_CONFIG_BUFFER_H_

/*=============================================================================
====================FRAME PROCESSING MODE======================================
============================================================================*/

/*=============================================================================
====================BUFFER SIZE CONFIGURATION==================================
============================================================================*/

/**
 * @brief Output ring buffer size (in 32-bit words)
 * 
 * Default: 2048 words (8192 bytes)
 * 
 * PURPOSE:
 *   Stores log frames before they are transferred/processed.
 *   Acts as a FIFO queue between logging and output.
 * 
 * SIZING GUIDE:
 *   - Larger buffer = more logs can be stored during burst activity
 *   - Smaller buffer = saves RAM but may overflow during bursts
 * 
 *   Typical frame size: 8-12 words (32-48 bytes)
 *   
 *   Examples:
 *     512 words (2 KB): ~40-60 frames (minimal system)
 *     2048 words (8 KB): ~170-250 frames (default, recommended)
 *     4096 words (16 KB): ~340-500 frames (high-traffic system)
 * 
 * OVERFLOW BEHAVIOR ("accordion" / gap mode — always on):
 *   Streaming pauses instead of dying. Producers are gated, the ring keeps
 *   draining to the transport, and once it is completely empty
 *   trax_process() emits a TRAX_TID_SESSION_GAP resync frame and re-enables
 *   streaming. The result is coherent frame bursts separated by explicit,
 *   quantified gaps — never a dead session.
 *
 *   What is lost during a gap: LOG / trace / kernel event frames (the gap
 *   frame carries the drop count). Var-stream samples are NOT silently
 *   lost — TRAX_STREAM_UPDATE keeps advancing the sequence index while
 *   gated, so the host plots the missing interval as a NaN region at the
 *   correct time.
 *
 *   Draining the APPLICATION ring to EMPTY (not "some space freed") is
 *   deliberate: it gives hysteresis against gate/resync thrash and
 *   guarantees the resync frame always has room. Resume is the same
 *   on every transport — a still-full TX buffer is the transport's
 *   problem (backpressure), not a second accordion pause.
 * 
 * MEMORY IMPACT:
 *   - RAM usage: TRAX_CFG_OUT_BUFFER_SIZE32 * 4 bytes
 *   - Example: 2048 words = 8192 bytes = 8 KB RAM
 * 
 * RECOMMENDATIONS:
 *   Minimal system: 512-1024 words (2-4 KB)
 *   Standard system: 2048 words (8 KB) - DEFAULT
 *   High-traffic system: 4096-8192 words (16-32 KB)
 * 
 * To override: Define in App/Config/trax_config.h:
 *   #define TRAX_CFG_OUT_BUFFER_SIZE32 4096
 */
#ifndef TRAX_CFG_OUT_BUFFER_SIZE32
#define TRAX_CFG_OUT_BUFFER_SIZE32    2048U  /* 8 KB */
#endif

/**
 * @brief Input buffer size (in 32-bit words)
 * 
 * Default: 128 words (512 bytes)
 * 
 * PURPOSE:
 *   Buffer for incoming data from PC (if bidirectional communication used).
 *   Used for commands, configuration, or control from host.
 * 
 * SIZING GUIDE:
 *   - Most systems: 128 words (512 bytes) sufficient
 *   - High command traffic: 256-512 words (1-2 KB)
 * 
 * NOTES:
 *   - Only used if implementing bidirectional communication
 *   - Not required for basic logging (one-way MCU → PC)
 * 
 * To override: Define in App/Config/trax_config.h:
 *   #define TRAX_CFG_IN_BUFFER_SIZE32 256
 */
#ifndef TRAX_CFG_IN_BUFFER_SIZE32
#define TRAX_CFG_IN_BUFFER_SIZE32     16  /* 64 bytes */
#endif

/*=============================================================================
====================FRAME SIZE LIMITS==========================================
============================================================================*/

/**
 * @brief Minimum frame size (in 32-bit words)
 * 
 * Default: 3 words (12 bytes)
 * 
 * STRUCTURE:
 *   Word 0: Frame header (size + flags)
 *   Word 1: Timestamp (24-bit timer + 8-bit tick)
 *   Word 2: Trace ID or Format String pointer
 * 
 * DO NOT CHANGE unless modifying frame structure!
 */
#ifndef TRAX_CFG_FRAME_MIN_SIZE32
#define TRAX_CFG_FRAME_MIN_SIZE32     3U
#endif

/**
 * @brief Maximum frame size (in 32-bit words)
 * 
 * Default: 65535 words (262140 bytes) — hardware maximum
 * 
 * PURPOSE:
 *   Hard ceiling imposed by the 16-bit frame_size32 field in the frame header.
 *   Max params = 65535 - 3 = 65532 parameters.
 * 
 * SIZING:
 *   - Frame header: 1 word (frame_size32 field is 16 bits → max 65535 words)
 *   - Value 0 reserved as uncommitted sentinel
 *   - Value 1 reserved as wrap marker (below TRAX_CFG_FRAME_MIN_SIZE32)
 *   - Usable maximum: 65535 words
 * 
 * PRACTICAL LIMITS:
 *   Typical log: 3-10 words (header + timestamp + ID + few params)
 *   Heavy log: 20-50 words (many parameters)
 *   Real ceiling: TRAX_CFG_OUT_BUFFER_SIZE32 (frame must fit in ring buffer)
 * 
 * BANDWIDTH IMPACT:
 *   - Larger max allows more parameters per log
 *   - Most logs use far less than maximum
 *   - Only affects worst-case scenario
 * 
 * To override: Define in App/Config/trax_config.h:
 *   #define TRAX_CFG_FRAME_MAX_SIZE32 128   Limit to 125 params
 */
#ifndef TRAX_CFG_FRAME_MAX_SIZE32
#define TRAX_CFG_FRAME_MAX_SIZE32     65535U
#endif

/*=============================================================================
====================TRANSMIT BATCH SIZE========================================
============================================================================*/

/**
 * @brief Maximum bytes drained from the ring per trax_process() pass
 *
 * Default: 4096 bytes
 *
 * PURPOSE:
 *   trax_send_frames() collects committed frames into one batch and hands
 *   it to the transport as a single all-or-nothing write. If the batch is
 *   allowed to grow beyond what the transport buffer can EVER hold (e.g.
 *   a 20 KB burst against a 16 KB RTT up-buffer), the write can never
 *   succeed and the stream stalls permanently. Capping the batch keeps
 *   every write acceptable by a partially-drained transport and lets the
 *   ring (TRAX_CFG_OUT_BUFFER_SIZE32) do its job as the burst absorber.
 *
 * SIZING GUIDE:
 *   - Must be comfortably below the transport's buffer capacity
 *     (RTT default: TRAX_RTT_BUFFER_SIZE_UP = 16384 bytes).
 *   - Must be at least as large as your biggest frame
 *     (a frame larger than the cap is still sent — alone in its batch).
 *   - Larger cap = fewer passes to drain a backlog; smaller cap = lower
 *     worst-case latency added to trax_process().
 *
 * To override: Define in App/Config/trax_config.h:
 *   #define TRAX_CFG_TX_BATCH_MAX_BYTES 8192
 */
#ifndef TRAX_CFG_TX_BATCH_MAX_BYTES
#define TRAX_CFG_TX_BATCH_MAX_BYTES    4096U
#endif

/*=============================================================================
====================TRANSPORT BUFFER CAPACITY HINT=============================
============================================================================*/

/**
 * @brief Transport TX buffer capacity hint for CUSTOM transports (in bytes)
 *
 * Default: 0 (unknown)
 *
 * PURPOSE:
 *   Diagnostics only. Shipped in every TRAX_TID_DIAG_REPORT snapshot so the
 *   host Diagnostics panel can render the transport buffer fill level as
 *   "used / capacity" instead of a bare byte count. Never affects streaming.
 *
 * BACKEND BEHAVIOR:
 *   - RTT (default transport): IGNORED — the RTT backend self-reports its
 *     real up-buffer size (TRAX_RTT_BUFFER_SIZE_UP) via
 *     trax_transport_rtt_buf_capacity().
 *   - CUSTOM transport: reported as-is. Set it to the size of whatever TX
 *     buffer backs your trax_transport_write() (UART DMA ring, USB FIFO, …).
 *     Leave at 0 if the capacity is unknown — the host then shows usage
 *     without a capacity figure.
 *
 * To override: Define in App/Config/trax_config.h:
 *   #define TRAX_CFG_TRANSPORT_BUF_SIZE 2048
 */
#ifndef TRAX_CFG_TRANSPORT_BUF_SIZE
#define TRAX_CFG_TRANSPORT_BUF_SIZE    0U
#endif

/**
 * @brief TX free-space query for CUSTOM transports (function name)
 *
 * Default: undefined (free space unknown)
 *
 * PURPOSE:
 *   Names a size_t(void) function returning how many bytes your TX path can
 *   accept right now. The core caps every drain batch at that value BEFORE
 *   collecting frames, so the all-or-nothing trax_transport_write() never has
 *   to reject — which is what keeps trax_process() pass cost proportional to
 *   what actually goes on the wire instead of to the whole backlog.
 *
 * STRONGLY RECOMMENDED. When left undefined the core assumes SIZE_MAX and
 * falls back to try-and-retry: correct and lossless, but a saturated link
 * costs a full collect/reject walk every pass, AND
 * TRAX_CFG_TX_BATCH_MAX_BYTES becomes load-bearing — keep it below the TX
 * buffer's real capacity or a max-size batch can never fit and the stream
 * stalls until the ring overflows.
 *
 * BACKEND BEHAVIOR:
 *   - RTT (default transport): IGNORED — the RTT backend reports its own
 *     ring's free space via trax_transport_rtt_tx_free().
 *   - CUSTOM transport: called before every drain batch.
 *
 * To override: Define in App/Config/trax_config.h:
 *   #define TRAX_CFG_TRANSPORT_TX_FREE  my_uart_tx_free
 */

/**
 * @brief Exact TX occupancy query for CUSTOM transports (function name)
 *
 * Default: undefined (derived from capacity minus free space)
 *
 * PURPOSE:
 *   Diagnostics only; never affects streaming. Occupancy and free space are
 *   two views of one TX buffer, so the core derives occupancy as
 *   TRAX_CFG_TRANSPORT_BUF_SIZE minus TRAX_CFG_TRANSPORT_TX_FREE rather than
 *   asking the transport for a second figure. Name a size_t(void) function
 *   here only when that derivation does not fit your TX path — a link that
 *   reports occupancy but not free space, or a ring whose reserved slots
 *   make the subtraction off by a byte and you want it exact.
 *
 * To override: Define in App/Config/trax_config.h:
 *   #define TRAX_CFG_TRANSPORT_BYTES_USED  my_uart_tx_pending
 */

/*=============================================================================
====================STREAM RESTART / RING QUIESCENCE===========================
============================================================================*/

/**
 * @brief Max pending-start polls waiting for ring quiescence on restart
 *
 * Default: 100 polls
 *
 * PURPOSE:
 *   CMD_SESSION_START does not reset the ring buffer inline. It gates
 *   streaming and defers the reset until the ring is quiescent
 *   (p_rd32 == p_alloc32): fully drained AND no producer inside a
 *   non-atomic ALLOC..COMMIT window. Resetting under an in-flight
 *   producer would let its late COMMIT plant a stale old-session Word 0
 *   in the new ring — the validator then kills the fresh session with
 *   FRAME_CORRUPT. This limit bounds how many polls the pending start
 *   may wait before giving up and resetting anyway.
 *
 * TIMING:
 *   One poll = one trax_process() pass:
 *     - RTOS builds: TRAX_CFG_CTRL_TASK_PERIOD_MS (default 10 ms)
 *       → 100 polls ≈ 1 second worst-case wait
 *     - Bare metal: one main-loop iteration
 *   A healthy system needs only ~1 poll (one drain pass); the limit
 *   exists so a dead transport or a producer task suspended/deleted
 *   mid-frame (application bug) cannot block the host's START forever.
 *   On expiry the reset proceeds — degrading to the historical
 *   (pre-quiescence) behavior and its stale-commit risk, which beats
 *   never ACKing the host.
 *
 * SIZING GUIDE:
 *   - Larger ring + slow transport = more polls to drain the backlog.
 *     Worst case ≈ ring bytes / transport bytes-per-poll-period.
 *     Example: 16 KB ring, 921600 baud UART (~92 kB/s), 10 ms period
 *     → ~921 bytes/poll → ~18 polls. Default 100 covers this with margin.
 *   - Bare-metal loops iterate much faster than 10 ms; consider a larger
 *     value there if your transport drains slowly per iteration.
 *
 * To override: Define in App/Config/trax_config.h:
 *   #define TRAX_CFG_START_RUNDOWN_POLL_LIMIT 300
 */
#ifndef TRAX_CFG_START_RUNDOWN_POLL_LIMIT
#define TRAX_CFG_START_RUNDOWN_POLL_LIMIT    100u
#endif

/*=============================================================================
====================CONFIGURATION VALIDATION===================================
============================================================================*/

/**
 * Validate buffer configuration
 */

/* Output buffer size checks */
#if TRAX_CFG_OUT_BUFFER_SIZE32 < 64
    #warning "TRAX_CFG_OUT_BUFFER_SIZE32 is very small (< 256 bytes). Buffer may overflow easily during burst logging."
#endif

/* Input buffer size checks */
#if TRAX_CFG_IN_BUFFER_SIZE32 < 16
    #warning "TRAX_CFG_IN_BUFFER_SIZE32 is very small (< 64 bytes). May not handle incoming commands properly."
#endif

/* Frame size validation */
#if TRAX_CFG_FRAME_MAX_SIZE32 > 65535
    #error "TRAX_CFG_FRAME_MAX_SIZE32 cannot exceed 65535 words (limited by 16-bit frame_size32 field in frame header)"
#endif

#if TRAX_CFG_FRAME_MIN_SIZE32 < 3
    #error "TRAX_CFG_FRAME_MIN_SIZE32 must be at least 3 words (header + timestamp + ID)"
#endif

#if TRAX_CFG_FRAME_MAX_SIZE32 < TRAX_CFG_FRAME_MIN_SIZE32
    #error "TRAX_CFG_FRAME_MAX_SIZE32 must be greater than or equal to TRAX_CFG_FRAME_MIN_SIZE32"
#endif

/* Transmit batch cap checks */
#if TRAX_CFG_TX_BATCH_MAX_BYTES < (TRAX_CFG_FRAME_MIN_SIZE32 * 4U)
    #error "TRAX_CFG_TX_BATCH_MAX_BYTES must hold at least one minimum-size frame"
#endif

/* Pending-start quiescence limit checks */
#if TRAX_CFG_START_RUNDOWN_POLL_LIMIT < 1
    #error "TRAX_CFG_START_RUNDOWN_POLL_LIMIT must be at least 1 (0 would reset the ring under in-flight producers on every restart)"
#endif

#endif /* TRAX_CONFIG_BUFFER_H_ */

