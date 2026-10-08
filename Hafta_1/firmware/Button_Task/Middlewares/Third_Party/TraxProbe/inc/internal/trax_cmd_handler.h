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
 * @file           : trax_cmd_handler.h
 * @brief          : TraxProbe Command Handler Interface
 * @version        : 2.0.0
 ******************************************************************************
 * @attention
 * 
 * Minimal command set:
 *   - START_TRACE: Enable transmission
 *   - STOP_TRACE: Disable transmission
 *   - PING: Test bidirectional communication
 * 
 ******************************************************************************
 */

#ifndef TRAX_CMD_HANDLER_H_
#define TRAX_CMD_HANDLER_H_

#include <stdint.h>
#include <stdbool.h>
#include "trax_cmd_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 ====================HANDLER FUNCTION DECLARATIONS=============================
 ============================================================================*/

/**
 * @brief Initialize command handler module
 * 
 * @return 0 on success
 */
int trax_cmd_handler_init(void);

/**
 * @brief Dispatch command to appropriate handler
 * 
 * Main dispatch function called by protocol layer.
 * Routes command to the appropriate handler function.
 * 
 * @param cmd Command code
 * @param params Pointer to parameters (may be NULL)
 * @param params_len Length of parameters
 */
void trax_cmd_dispatch(enum trax_cmd_code_t cmd, 
                       const uint8_t *params, 
                       uint16_t params_len);

/**
 * @brief Execute a latched CMD_SESSION_START once the ring is quiescent.
 *
 * CMD_SESSION_START only gates streaming and latches a pending-start
 * flag; the ring-buffer reset + SESSION_START emission happen here, and
 * only when p_rd32 == p_alloc32 — which proves the ring is fully drained
 * AND no producer is inside a non-atomic ALLOC..COMMIT window (an
 * uncommitted reservation pins p_rd32 behind p_alloc32 via its zero
 * Word 0). Resetting the ring under an in-flight producer corrupts the
 * new session with a stale old-epoch commit — see the gate comment in
 * trax_cmd_handler.c. The check reads only existing ring state: the
 * producer hot path carries zero extra cost.
 *
 * Called from trax_process() after command processing; the ring drains
 * through the normal trax_send_frames() path each pass until the gate
 * opens (typically the next pass). No-op when no start is pending.
 */
void trax_cmd_handler_process_pending_start(void);

#ifdef __cplusplus
}
#endif

#endif /* TRAX_CMD_HANDLER_H_ */
