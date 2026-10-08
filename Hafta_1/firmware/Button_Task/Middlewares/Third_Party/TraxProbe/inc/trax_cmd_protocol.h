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
 * @file           : trax_cmd_protocol.h
 * @brief          : TraxProbe Command Protocol Interface
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 * 
 * This module implements the command protocol for receiving commands from
 * Traxcope via any transport (RTT, UART, USB, Ethernet, etc.).
 * 
 * FRAME FORMAT:
 *   ┌──────────┬──────────┬──────────┬─────────────┐
 *   │  PREFIX  │  LENGTH  │   CMD    │   PARAMS    │
 *   │ (2 bytes)│ (2 bytes)│ (1 byte) │  (N bytes)  │
 *   │ 0xAA 0x55│ little-e │          │             │
 *   └──────────┴──────────┴──────────┴─────────────┘
 * 
 * USAGE:
 *   1. Call trax_cmd_protocol_init() during initialization
 *   2. Call trax_cmd_protocol_process() periodically from main loop
 * 
 ******************************************************************************
 */

#ifndef TRAX_CMD_PROTOCOL_H_
#define TRAX_CMD_PROTOCOL_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "trax_compiler.h"
#include "trax_session.h"
#include "trax_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 ====================PROTOCOL DEFINITIONS======================================
 ============================================================================*/

/** @brief Frame prefix bytes for synchronization */
#define TRAX_CMD_PREFIX_BYTE0       0xAA
#define TRAX_CMD_PREFIX_BYTE1       0x55

/** @brief Maximum payload size (CMD + PARAMS) */
#define TRAX_CMD_MAX_PAYLOAD_SIZE   64

/** @brief Frame header size (PREFIX + LENGTH) */
#define TRAX_CMD_HEADER_SIZE        4

/*=============================================================================
 ====================COMMAND CODES=============================================
 ============================================================================*/

/**
 * @brief Command codes sent from Traxcope to TraxProbe
 *
 * Minimal command set:
 *   - SESSION_START: Start the session (enable transmission to host)
 *   - SESSION_STOP:  Stop the session (disable transmission to host)
 *   - PING: Test bidirectional communication without affecting the session
 *
 * @note Multi-probe sync is firmware-driven. The application configured as
 *       MASTER drives the bus itself with trax_sync_trigger_start() /
 *       trax_sync_trigger_end() (e.g. from a periodic timer ISR); SLAVE
 *       applications wire their EXTI ISR to trax_sync_triggered(). The
 *       host only observes TRAX_TID_SYNC_TIMESTAMP frames — there is no
 *       host-initiated trigger command.
 */
enum trax_cmd_code_t {
    /* Streaming Control */
    TRAX_CMD_SESSION_START       = 0x15,  /**< Start streaming (SESSION_START) */
    TRAX_CMD_SESSION_STOP        = 0x12,  /**< Stop streaming (SESSION_STOP) */

    /* Utility */
    TRAX_CMD_PING                  = 0xFE,  /**< Ping (test bidirectional, responds with ACK) */

    TRAX_CMD_LAST
};

/*=============================================================================
 ====================RESPONSE CODES=============================================
 ============================================================================*/

/**
 * @brief Result codes for SET command responses
 * 
 * Used in ACK frames to indicate command execution result.
 */
enum trax_cmd_result_t {
    TRAX_CMD_RESULT_SUCCESS = 0x00,        /**< Command succeeded (state changed) */
    TRAX_CMD_RESULT_ALREADY = 0x01,        /**< Command succeeded (already in target state) */
    TRAX_CMD_RESULT_INVALID_STATE = 0x02,  /**< Command failed (invalid state transition) */
    TRAX_CMD_RESULT_ERROR = 0xFF           /**< Command failed (general error) */
};

/*=============================================================================
 ====================PROTOCOL STATE MACHINE=====================================
 ============================================================================*/

/**
 * @brief Protocol state uses trax_session flags (from trax_session.h)
 * 
 * The probe is always tracing after trax_init().
 * Only the stream to the host is toggled by commands.
 *   streaming = 0: Probe tracing, no stream to host
 *   streaming = 1: Probe tracing + streaming to host
 * 
 * Transitions (via commands):
 *   streaming 0 -> 1   (CMD_SESSION_START)
 *   streaming 1 -> 0   (CMD_SESSION_STOP)
 */


/*=============================================================================
 ====================STRUCTURES================================================
 ============================================================================*/

/**
 * @brief Frame header structure
 */
struct trax_cmd_frame_header_t {
    uint8_t  prefix[2];     /**< 0xAA, 0x55 */
    uint16_t length;        /**< Payload length (little-endian) */
} TRAX_PACKED;

/**
 * @brief Complete frame structure
 */
struct trax_cmd_frame_t {
    struct trax_cmd_frame_header_t header;
    uint8_t payload[TRAX_CMD_MAX_PAYLOAD_SIZE];  /**< CMD + PARAMS */
} TRAX_PACKED;

/**
 * @brief Command callback function type
 * 
 * @param cmd Command code
 * @param params Pointer to parameters (may be NULL)
 * @param params_len Length of parameters
 */
typedef void (*trax_cmd_callback_t)(enum trax_cmd_code_t cmd, 
                                     const uint8_t *params, 
                                     uint16_t params_len);

/*=============================================================================
 ====================FUNCTION DECLARATIONS=====================================
 ============================================================================*/

/**
 * @brief Initialize command protocol
 * 
 * Resets parser state and internal variables.
 * Call once during system initialization.
 * 
 * @return 0 on success
 */
int trax_cmd_protocol_init(void);

/**
 * @brief Process incoming command data
 * 
 * Reads data from transport, parses frames, and dispatches commands.
 * Call periodically from main loop or a task.
 * 
 * @return Number of commands processed
 * 
 * @note Non-blocking - returns immediately if no data available
 */
int trax_cmd_protocol_process(void);

/* trax_wait_session_started() is declared in trax.h (public API). */

/* Session-active check: use TRAX_IS_SESSION_ACTIVE() from trax_session.h.
 * Protocol state is available directly via trax_session.state. */

/*=============================================================================
 ====================RESPONSE FUNCTIONS=========================================
 ============================================================================*/

/**
 * @brief Send ACK response for SET commands
 * 
 * @param cmd     Command being acknowledged
 * @param result  Result code (TRAX_CMD_RESULT_xxx)
 * @return 0 on success
 * 
 * @note ACK frame format: [cmd(8b) | result(8b) | state(8b) | reserved(8b)] [session_id(32b)]
 * @note GET commands don't send ACK - their data response is the acknowledgment
 */
int32_t trax_cmd_send_ack(uint8_t cmd, uint8_t result);

/**
 * @brief Send NAK response
 * 
 * @param cmd       Command that failed
 * @param err_code  Error code
 * @return 0 on success
 */
int32_t trax_cmd_send_nak(uint8_t cmd, uint8_t err_code);

/*=============================================================================
 ====================TRANSPORT INTERFACE=======================================
 ============================================================================*/

/* Transport functions are declared in trax_transport.h (included above) */

#ifdef __cplusplus
}
#endif

#endif /* TRAX_CMD_PROTOCOL_H_ */
