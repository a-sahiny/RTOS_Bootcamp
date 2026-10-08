/*
 * SPDX-License-Identifier: LicenseRef-TraxProbe-Commercial
 * Copyright (c) 2026 Embedya. All rights reserved.
 *
 * TraxProbe core engine. CONFIDENTIAL and proprietary to Embedya.
 * Licensed under the TraxProbe Commercial License (see LICENSE-COMMERCIAL.txt).
 * No use, copying, modification, redistribution, decompilation, or reverse
 * engineering is permitted except as expressly authorized by that agreement.
 */

#ifndef TRAX_H_
#define TRAX_H_

#include "trax_config_default.h"
#include "trax_hw.h"          /* HW port layer: TRAX_PORT_ENTER/EXIT_CRITICAL_SECTION, timepacked */
#include "trax_isr.h"
#include "trax_sm.h"
#include "trax_log.h"
#include "trax_stream.h"
#include "trax_var.h"
#include "trax_marker.h"
#include "trax_main_loop.h"
#include "trax_utility.h"
#include "trax_buffer.h"
#include "trax_cmd_protocol.h"
#include "trax_tid.h"
#include "trax_data_types.h"
#include "trax_diag.h"        /* For trax_diag_callback_t, trax_diag_subscribe() */
#include "trax_fault.h"       /* TraxFault: crash capture / reset reason (opt-in) */

/*=============================================================================
 ====================GLOBAL MACRO DEFINITIONS==================================
 ============================================================================*/

/**
 * @brief Frame limit constants for trax_consume_frames()
 */
#define TRAX_CONSUME_ALL_FRAMES    UINT32_MAX   /* Process all available frames in buffer */

/*=============================================================================
 ====================GLOBAL MACRO FUNCTIONS====================================
 ============================================================================*/

/*=============================================================================
 ====================GLOBAL TYPEDEF============================================
 ============================================================================*/

/*=============================================================================
 ====================GLOBAL VARIABLES==========================================
 ============================================================================*/

/*=============================================================================
 ====================GLOBAL FUNCTION DECLERATION===============================
 ============================================================================*/
/**
 * @brief Initialize TRAX subsystem and start recording
 * 
 * Initializes all TRAX components and starts recording automatically.
 * Recording is always on after init - use trax_session_start() to enable transmission.
 * 
 * @return 0 on success, non-zero on failure
 */
#if TRAX_ENABLE
int trax_init(void);
#else
static inline int trax_init(void) { return 0; }
#endif

/**
 * @brief Main processing function - call this periodically from your main loop
 * 
 * This single function handles all TRAX processing:
 *   1. Processes incoming commands from Traxcope (START/STOP/PING)
 *   2. Transmits pending trace frames to the host
 * 
 * Should be called periodically from the main loop or a dedicated task.
 * Non-blocking - returns immediately if nothing to process.
 * 
 * @return Number of commands processed
 * 
 * @example
 *   while (1) {
 *       // Your application code here
 *       update_sensor_data();
 *       
 *       // Single call handles all TRAX processing
 *       trax_process();
 *   }
 */
#if TRAX_ENABLE
int trax_process(void);
#else
static inline int trax_process(void) { return 0; }
#endif

/*-----------------------------------------------------------------------------
 * Session Control
 *
 * The SESSION is the whole link to Traxcope: while it is active, every
 * producer (logs, vars, RTOS events, data streams) is transmitted. It is
 * normally started and stopped by the host. Not to be confused with a
 * single data stream, which TRAX_STREAM_START/STOP (trax_stream.h) control.
 *---------------------------------------------------------------------------*/

/**
 * @brief Start the session (enable transmission to PC)
 *
 * Begins real-time transmission of trace data:
 *   - Sends dynamic metadata
 *   - Sends the SESSION_START frame
 *   - Transmits buffered and new frames
 *
 * Recording is always active after trax_init(). This function enables
 * the transmission of recorded data to the PC.
 *
 * @return 0 on success
 */
#if TRAX_ENABLE
int trax_session_start(void);
#else
static inline int trax_session_start(void) { return 0; }
#endif

/**
 * @brief Stop the session (disable transmission, keep recording)
 *
 * Pauses transmission but continues recording:
 *   - Sends the SESSION_STOP frame
 *   - Events continue to be captured to buffer
 *   - Call trax_session_start() to resume
 * 
 * @return 0 on success
 * 
 * @note Has no effect if not streaming.
 */
#if TRAX_ENABLE
int trax_session_stop(void);
#else
static inline int trax_session_stop(void) { return 0; }
#endif

/**
 * @brief Pause streaming: gate producers, hold the resume (trigger mode).
 *
 * Designed for band-limited links with rare conditions of interest:
 * everything captured BEFORE the pause still drains to the host, then
 * the probe stays silent (suppressed frames are counted) until
 * trax_session_resume() or a TRAX_TRIGGER_FIRE() releases the hold —
 * at which point a SESSION_GAP resync frame (full dynamic snapshot +
 * trigger attribution) is emitted and streaming continues live.
 *
 * @return 0 on success
 * @note No-op unless currently streaming. See inc/trax_trigger.h.
 */
#if TRAX_ENABLE
int trax_session_pause(void);
#else
static inline int trax_session_pause(void) { return 0; }
#endif

/**
 * @brief Resume a paused stream (reason = USER).
 *
 * The next trax_process() pass emits the SESSION_GAP resync frame and
 * re-enables streaming. For hardware-condition resumes, use
 * TRAX_TRIGGER_FIRE() instead — it attributes the resume to the trigger
 * TID and carries the exact fire-time anchor.
 *
 * @return 0 on success
 * @note No-op when no pause is held.
 */
#if TRAX_ENABLE
int trax_session_resume(void);
#else
static inline int trax_session_resume(void) { return 0; }
#endif

/**
 * @brief Block until Traxcope sends CMD_SESSION_START (blocking).
 *
 * Call once after trax_init() before entering the main loop.
 * Polls the transport RX channel, parses the command frame
 * [0xAA 0x55 | len | 0x15], allocates SESSION_START, and drains it
 * onto the wire (the host's implicit ACK) before returning.
 *
 * Implementation: trax_cmd_protocol.c
 */
#if TRAX_ENABLE
void trax_wait_session_started(void);
#else
static inline void trax_wait_session_started(void) { }
#endif

/**
 * @brief Set a callback for immediate notification when a buffer allocation fails
 *
 * Called in the context of the caller (may be ISR). Keep the callback
 * short and non-blocking (e.g., set a flag, toggle a GPIO).
 *
 * @param callback Function pointer (receives the requested size in words), or NULL to disable
 * @return int 0 on success
 */
#if TRAX_ENABLE
int trax_set_buffer_alloc_fail_callback(trax_buffer_alloc_fail_callback_t callback);
#else
static inline int trax_set_buffer_alloc_fail_callback(trax_buffer_alloc_fail_callback_t callback) { (void)callback; return 0; }
#endif

/*-----------------------------------------------------------------------------
 * Diagnostics
 *
 * Three access patterns — choose what fits your use case:
 *
 *   1. Subscribe to specific events (mask-based, up to TRAX_CFG_DIAG_MAX_SUBSCRIBERS):
 *        trax_diag_subscribe(TRAX_DIAG_EVT_OVERFLOW | TRAX_DIAG_EVT_WRITE_FAIL, my_cb);
 *        trax_diag_subscribe(TRAX_DIAG_EVT_STATS,   my_stats_cb);
 *        trax_diag_unsubscribe(my_cb);
 *
 *   2. Set threshold alerts (fires TRAX_DIAG_EVT_THRESHOLD once per crossing):
 *        trax_diag_set_threshold(TRAX_DIAG_METRIC_RING_BUF_USED,
 *                                TRAX_CFG_OUT_BUFFER_SIZE32 * 4 * 75 / 100);
 *        trax_diag_clear_threshold(TRAX_DIAG_METRIC_RING_BUF_USED);
 *
 *   3. Poll stats at any time — no callback needed:
 *        struct trax_diag_stats_t s;
 *        trax_diag_get_stats(&s);   // consistent snapshot, safe from concurrent writes
 *
 *   Catch-all subscription (single callback for every event):
 *        trax_diag_subscribe(TRAX_DIAG_EVT_ALL, my_cb);
 *
 * See trax_diag.h for full API, event constants, and metric enum.
 *---------------------------------------------------------------------------*/



/*=============================================================================
 ====================RTOS PORT=================================================
 ============================================================================*/

/* When an RTOS is configured, pull in the port dispatcher so that users only
 * need to include trax.h — no separate RTOS-specific include required. */
#if (TRAX_CFG_RTOS_TYPE != TRAX_RTOS_NONE)
    #include "trax_rtos_port.h"
#endif

#endif /* TRAX_H_ */
