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
 * @file           : trax_transport.h
 * @brief          : TraxProbe Transport Interface
 * @version        : 5.0.0
 ******************************************************************************
 * @attention
 *
 * This header defines:
 *   1. The scatter-gather I/O vector the core walks when draining the ring.
 *   2. The transport contract — the trax_transport_*() functions a backend
 *      PROVIDES. There is no dispatcher and no ops struct; the core reaches the
 *      active backend through the compile-time TRAX_TRANSPORT_* selector macros
 *      at the bottom of this header. Selection is purely TRAX_CFG_TRANSPORT.
 *
 * TRANSPORT SELECTION IS COMPILE-TIME (TRAX_CFG_TRANSPORT in trax_config.h):
 *
 * BUILT-IN TRANSPORT (default)  —  TRAX_CFG_TRANSPORT == TRAX_TRANSPORT_RTT
 * --------------------------------------------------------------------------
 *   SEGGER RTT is compiled in and used automatically. The built-in backend in
 *   transports/RTT/trax_transport_rtt.c DEFINES the trax_transport_*()
 *   functions. No user code required.
 *
 * CUSTOM TRANSPORT (UART, USB-CDC, TCP, SPI, …)  —  TRAX_TRANSPORT_CUSTOM
 * --------------------------------------------------------------------------
 *   The RTT backend is compiled out (reclaiming its static buffers). YOU
 *   DEFINE four trax_transport_*() functions below in your application —
 *   there is no struct to fill, no register() call, and no template to copy;
 *   the linker binds them by name. Just provide them in any .c in your build:
 *
 *       #include <trax_transport.h>
 *       int    trax_transport_init(void)                     { return 0; }
 *       size_t trax_transport_write(const void *p, size_t n) { ... }
 *       size_t trax_transport_read(void *p, size_t n)        { ... }
 *       void   trax_transport_clear_tx(void)                 { }
 *
 *   ALL FOUR ARE REQUIRED. The core references every one, so leaving any
 *   undefined fails the link with "undefined reference to 'trax_transport_…'"
 *   — never a silent fake. init() is often trivial (`return 0;` when there is
 *   nothing to arm) but you must still define it: it is the only hook where
 *   the RX path gets armed, so defaulting it would silently kill the
 *   down-channel. clear_tx() is called on session restart to drop stale
 *   bytes still sitting in a software TX ring; a blocking UART that has
 *   already handed bytes to the peripheral implements it as an empty body.
 *   Per-function guidance is in each @brief below.
 *
 *   Everything else the core wants to know about a transport is OPTIONAL and
 *   opts in from trax_config.h — no function is mandatory for it, and leaving
 *   it out costs behaviour the core already has a fallback for:
 *
 *       TRAX_CFG_TRANSPORT_TX_FREE     free-space query  (recommended)
 *       TRAX_CFG_TRANSPORT_BUF_SIZE    TX capacity, diagnostics
 *       TRAX_CFG_TRANSPORT_BYTES_USED  exact TX occupancy, diagnostics
 *
 *   There is no writev: the core drains the ring one contiguous batch per
 *   pass (capped at TRAX_CFG_TX_BATCH_MAX_BYTES, split at ring wrap) and
 *   sends it with a single ALL-OR-NOTHING trax_transport_write(): return
 *   the full size (every byte committed) or 0 (nothing accepted — the core
 *   treats this as soft backpressure and retries the same batch on the
 *   next pass). Never commit part of a request: a partial return is
 *   treated as a torn frame and fatally stops the stream.
 *
 *   There is no bytes-available query either: the core drains the
 *   down-channel with trax_transport_read() alone and treats a short read
 *   as "nothing left", so read() must be safe to call when idle and return
 *   0. Any per-poll housekeeping a link needs (e.g. a non-blocking accept()
 *   on a TCP backend) belongs in read().
 *
 *   If TRAX_CFG_TRANSPORT_TX_FREE names a free-space function, the core
 *   sizes each batch to it BEFORE collecting frames, so the transport
 *   virtually never sees a batch it has to reject — backpressure is absorbed
 *   by collecting fewer frames instead of rejecting and retrying whole
 *   batches, which is what keeps trax_process() pass cost flat under load.
 *   Without it the core assumes SIZE_MAX (free space unknown) and falls back
 *   to try-and-retry, which is correct but pays a full collect/reject walk
 *   per pass while the link is saturated. Skipping it also makes
 *   TRAX_CFG_TX_BATCH_MAX_BYTES load-bearing: keep it below the TX buffer's
 *   real capacity or a max-size batch can never fit and the stream stalls
 *   until the ring overflows.
 *
 *   No template is required, but an optional copy-paste skeleton and a
 *   step-by-step guide live in transports/Custom/ if you want a starting point.
 *
 *   See .cursor/rules/trax-transport-transmission.mdc for the strict
 *   all-or-nothing write semantics every transport must honour.
 *
 * CHANNEL SEMANTICS
 * -----------------
 *   Up-channel   (Target -> Host): trace data, via write
 *   Down-channel (Host -> Target): commands,   via read
 *
 * All functions run from the TraxCtrl RTOS task (or from the caller of
 * trax_process() in a bare-metal build) — they must never block indefinitely.
 *
 ******************************************************************************
 */

#ifndef TRAX_TRANSPORT_H_
#define TRAX_TRANSPORT_H_

#include <stddef.h>
#include <stdint.h>

#include "trax_config_default.h"  /* TRAX_ENABLE / TRAX_CFG_TRANSPORT */

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 ====================SCATTER-GATHER I/O VECTOR=================================
 ============================================================================*/

/**
 * @brief Single scatter-gather segment of committed ring data.
 *
 * The collector produces ONE contiguous segment per pass (batches end at
 * wrap markers so a retried batch can never duplicate already-sent data).
 * The array size is kept at 2 for layout stability.
 */
struct trax_iov_t {
	const void *p_base; /**< Pointer to contiguous data */
	size_t      len;    /**< Segment length in bytes    */
};

#define TRAX_IOV_MAX  2  /**< Max segments produced by the ring buffer */

/*=============================================================================
 ====================TRANSPORT CONTRACT (one set, backend-provided)===========
 ============================================================================*/
/*
 * A backend DEFINES these (for RTT, the rtt_-prefixed variants), chosen at
 * compile time by TRAX_CFG_TRANSPORT; the core reaches them via the
 * TRAX_TRANSPORT_* selector macros at the bottom of this header:
 *   - TRAX_TRANSPORT_RTT    -> transports/RTT/trax_transport_rtt.c (built-in)
 *   - TRAX_TRANSPORT_CUSTOM -> your application defines all four below; any
 *                              left undefined fails the link by name.
 *
 * The core never passes NULL / zero-length arguments to write/read, so backends
 * do not re-validate them.
 */

/**
 * @brief Initialize the transport binding (not the hardware itself). REQUIRED.
 *
 * Called once by trax_init(). The hardware (UART, socket, …) is expected to be
 * up already (typically your bsp_init() owns clocks / GPIO / baud); here you
 * only arm what the transport needs, e.g. enable the RX path:
 *     bsp_uart_enable_rx(BSP_UART_X);
 * If there is nothing to arm, just `return 0;`.
 *
 * @return 0 on success, non-zero on failure (aborts trax_init).
 */
int    trax_transport_init(void);

/**
 * @brief Send data to host — strictly all-or-nothing. REQUIRED.
 *
 * Return exactly one of:
 *   - @p size — every byte is committed to the wire / DMA / TX buffer.
 *   - 0      — NOTHING was accepted (TX buffer temporarily full). The core
 *              treats this as soft backpressure: the batch stays queued in
 *              the application ring and is retried on the next
 *              trax_process() pass. No data is lost; a permanently dead
 *              link is bounded by the ring filling up (BUFFER_OVERFLOW).
 *
 * Never commit part of the request: a partial return (0 < n < size) means a
 * torn frame is on the wire and fatally stops the stream with
 * TRAX_STOP_REASON_TRANSPORT_FAIL.
 *
 * Recommended pattern (fail-fast, non-blocking):
 *     if (uart_tx_free_bytes(BSP_UART_X) < size)
 *         return 0;                      // backpressure — core retries
 *     uart_tx_enqueue(BSP_UART_X, p_src, size);
 *     return size;
 *
 * A blocking driver that always commits everything before returning is also
 * acceptable, but stalls trax_process() while it waits.
 *
 * @param[in] p_src  Data to send
 * @param[in] size   Number of bytes
 * @return @p size on success; 0 = retryable backpressure;
 *         anything else = fatal transport error
 */
size_t trax_transport_write(const void *p_src, size_t size);

/**
 * @brief Read data from host — non-blocking. REQUIRED.
 *
 * Copy up to @p size bytes from the down-channel into @p p_dst and return
 * immediately with whatever is available (0 is valid), e.g.:
 *     return (size_t)bsp_uart_receive(BSP_UART_X, p_dst, size);
 *
 * The core drains the down-channel by calling this in a loop until it
 * returns LESS than @p size, so a short read is the "nothing left" signal.
 * Two consequences for the implementer:
 *   - It is called on every trax_process() pass, including idle ones, and
 *     must be cheap and safe to call with nothing pending (return 0).
 *   - Per-poll housekeeping the link needs — e.g. a non-blocking accept()
 *     on a TCP backend — belongs here; there is no separate poll hook.
 *
 * @param[out] p_dst  Destination buffer
 * @param[in]  size   Maximum bytes to read
 * @return Bytes actually read
 */
size_t trax_transport_read(void *p_dst, size_t size);

/**
 * @brief Discard queued TX bytes on session restart. REQUIRED.
 *
 * Called when the host starts a new session, after the application ring is
 * reset and before SESSION_START is sent. Empty any software TX ring you
 * own, and leave the down-channel (RX) untouched. A new session must not
 * start behind stale undrained frames — those land in front of
 * SESSION_START, cause instant backpressure, and can overflow the
 * application ring right after START.
 *
 * Bytes already handed to a blocking UART / DMA engine cannot be recalled;
 * the host treats those as pre-session noise. In that case this function
 * is an empty body — still define it, do not hide it behind a config macro.
 *
 * @return void
 */
void   trax_transport_clear_tx(void);

/*
 * OPTIONAL TX FREE-SPACE QUERY  —  TRAX_CFG_TRANSPORT_TX_FREE
 * -----------------------------------------------------------
 * Not a required trax_transport_*() function: the core has a correct
 * fallback (assume SIZE_MAX, try-and-retry), so making it mandatory would
 * break every existing custom transport at link time for a behaviour they
 * already have. Strongly RECOMMENDED nonetheless — it is the mechanism that
 * keeps trax_process() pass cost proportional to what actually goes on the
 * wire. Opt in from trax_config.h:
 *
 *     #define TRAX_CFG_TRANSPORT_TX_FREE  my_uart_tx_free
 *
 * naming a size_t(void) function that returns exactly one of:
 *   - Free space in bytes — a write() of up to this size MUST be accepted.
 *     Between this call and the write, free space may only GROW (the host
 *     side drains concurrently; nothing else writes the TX buffer), so the
 *     guarantee holds without locking. The core caps each drain batch at
 *     this value BEFORE collecting frames, so the following all-or-nothing
 *     write() never has to reject.
 *   - SIZE_MAX — free space cannot be reported right now. The core falls
 *     back to try-and-retry for that pass.
 *
 * e.g.  size_t my_uart_tx_free(void) { return uart_tx_free_bytes(BSP_UART_X); }
 *
 * Default when undefined: SIZE_MAX (unknown) — full batches are handed to
 * write() and a 0 return is treated as soft backpressure. Note this makes
 * TRAX_CFG_TX_BATCH_MAX_BYTES load-bearing: keep it below the TX buffer's
 * real capacity or a max-size batch can never fit.
 */

/*
 * OPTIONAL TX OCCUPANCY  —  TRAX_CFG_TRANSPORT_BYTES_USED
 * -------------------------------------------------------
 * Diagnostics only; never affects streaming. The core DERIVES occupancy as
 * TRAX_CFG_TRANSPORT_BUF_SIZE minus the free-space query above, which is
 * exact for a plain ring and needs no function from you at all. Override it
 * only when that derivation does not fit your TX path (a link that reports
 * occupancy but not free space, or a ring that reserves slots), via
 * trax_config.h:
 *
 *     #define TRAX_CFG_TRANSPORT_BYTES_USED  my_uart_tx_pending
 *
 * naming a size_t(void) function returning bytes still pending in the TX
 * buffer. Default when undefined: derived, or 0 when neither the capacity
 * nor the free space is known.
 */

/*=============================================================================
 ====================CORE-INTERNAL TRANSPORT SELECTOR========================
 ============================================================================*/
/*
 * The core does NOT call the public trax_transport_*() names directly. It calls
 * the TRAX_TRANSPORT_* selector macros below, which resolve at compile time to
 * the symbols of the active backend:
 *
 *   - TRAX_TRANSPORT_RTT    -> trax_transport_rtt_*()  (built-in; distinct names)
 *   - TRAX_TRANSPORT_CUSTOM -> trax_transport_*()      (defined by your app)
 *
 * WHY THE RTT BACKEND USES DISTINCT NAMES:
 *   Because RTT defines trax_transport_rtt_*() (not trax_transport_*()), a user
 *   may leave their custom-transport source file in the build and simply switch
 *   TRAX_CFG_TRANSPORT back to TRAX_TRANSPORT_RTT WITHOUT getting a
 *   "multiple definition of 'trax_transport_write'" link error: in RTT mode the
 *   user's trax_transport_*() functions are just unreferenced (the core calls
 *   the rtt_ names instead), so there is only one definition of each symbol.
 *
 *   NOTE: this de-conflicts only the transport functions. Any OTHER symbols
 *   your transport file defines (ISR handlers, IRQ vectors, etc.) are still
 *   compiled in RTT mode — guard that file or drop it from the build if those
 *   clash.
 */
#if TRAX_CFG_TRANSPORT == TRAX_TRANSPORT_RTT

int    trax_transport_rtt_init(void);
size_t trax_transport_rtt_write(const void *p_src, size_t size);
size_t trax_transport_rtt_read(void *p_dst, size_t size);
size_t trax_transport_rtt_tx_free(void);
size_t trax_transport_rtt_bytes_used(void);
size_t trax_transport_rtt_buf_capacity(void);
void   trax_transport_rtt_clear_tx(void);

#define TRAX_TRANSPORT_INIT         trax_transport_rtt_init
#define TRAX_TRANSPORT_WRITE        trax_transport_rtt_write
#define TRAX_TRANSPORT_READ         trax_transport_rtt_read
#define TRAX_TRANSPORT_CLEAR_TX     trax_transport_rtt_clear_tx

/* RTT reports both faces of its up ring exactly, so neither is derived. */
#define TRAX_TRANSPORT_TX_FREE      trax_transport_rtt_tx_free
#define TRAX_TRANSPORT_BYTES_USED   trax_transport_rtt_bytes_used

/* Diagnostics-only capacity hint: total up-channel TX buffer size in bytes.
 * RTT self-reports (TRAX_RTT_BUFFER_SIZE_UP); shipped once per diag snapshot
 * so the host can render "transport buffer used / capacity". */
#define TRAX_TRANSPORT_BUF_CAPACITY() trax_transport_rtt_buf_capacity()

#else /* TRAX_TRANSPORT_CUSTOM (and any future user-provided backend) */

#define TRAX_TRANSPORT_INIT         trax_transport_init
#define TRAX_TRANSPORT_WRITE        trax_transport_write
#define TRAX_TRANSPORT_READ         trax_transport_read
#define TRAX_TRANSPORT_CLEAR_TX     trax_transport_clear_tx

/* Diagnostics-only capacity hint. Deliberately NOT a required
 * trax_transport_*() function — it never affects streaming, so forcing a
 * link break on every existing custom transport is not justified. Custom
 * transports opt in via trax_config.h:
 *     #define TRAX_CFG_TRANSPORT_BUF_SIZE  sizeof(my_uart_tx_ring)
 * Default is 0 = unknown; the host then shows usage without a capacity. */
#define TRAX_TRANSPORT_BUF_CAPACITY() ((size_t)TRAX_CFG_TRANSPORT_BUF_SIZE)

/* Optional TX free-space query (see the contract block above). Undefined
 * means "free space unknown": the core hands full batches to write() and
 * treats a 0 return as soft backpressure — the behaviour every custom
 * transport had before this query existed. */
#ifdef TRAX_CFG_TRANSPORT_TX_FREE
size_t TRAX_CFG_TRANSPORT_TX_FREE(void);
#define TRAX_TRANSPORT_TX_FREE()    TRAX_CFG_TRANSPORT_TX_FREE()
#else
#define TRAX_TRANSPORT_TX_FREE()    (SIZE_MAX)
#endif

/* Optional exact TX occupancy. Undefined means derived from the capacity
 * hint and the free-space query — see trax_transport_bytes_used_derived(). */
#ifdef TRAX_CFG_TRANSPORT_BYTES_USED
size_t TRAX_CFG_TRANSPORT_BYTES_USED(void);
#define TRAX_TRANSPORT_BYTES_USED() TRAX_CFG_TRANSPORT_BYTES_USED()
#else
#define TRAX_TRANSPORT_BYTES_USED() trax_transport_bytes_used_derived()
#endif

/**
 * @brief TX occupancy derived from capacity minus free space. Diagnostics.
 *
 * Occupancy and free space are two views of one TX buffer, so a transport
 * that already reports free space does not need to report occupancy too.
 * Returns 0 ("unknown") when either input is missing, which is exactly what
 * a custom transport with no TX-occupancy query used to return by hand.
 * Rings that reserve a slot are off by that slot; this feeds a diagnostics
 * gauge only, and TRAX_CFG_TRANSPORT_BYTES_USED overrides it where the
 * exact figure matters.
 */
static inline size_t trax_transport_bytes_used_derived(void)
{
	size_t capacity = TRAX_TRANSPORT_BUF_CAPACITY();
	size_t tx_free  = TRAX_TRANSPORT_TX_FREE();

	if (capacity == 0U || tx_free == SIZE_MAX || tx_free > capacity) {
		return 0U;
	}
	return capacity - tx_free;
}

#endif

#ifdef __cplusplus
}
#endif

#endif /* TRAX_TRANSPORT_H_ */
