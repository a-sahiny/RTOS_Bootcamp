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
 * @file           : trax_task.h
 * @brief          : TraxProbe Internal Task (RTOS mode)
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 *
 * When TRAX_CFG_RTOS_TYPE == TRAX_RTOS_FREERTOS, TraxProbe creates its own
 * low-priority task that periodically drains the ring buffer and processes
 * host commands. The user does NOT need to call trax_process() manually.
 *
 * For bare-metal (TRAX_RTOS_NONE), this module is not compiled. The user
 * must call trax_process() from their main loop.
 *
 ******************************************************************************
 */

#ifndef TRAX_TASK_H_
#define TRAX_TASK_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the TraxProbe internal task
 *
 * Creates a low-priority periodic task that calls trax_process() every
 * TRAX_CFG_CTRL_TASK_PERIOD_MS. Called automatically from trax_init() when
 * TRAX_CFG_RTOS_TYPE == TRAX_RTOS_FREERTOS.
 *
 * Task configuration (override in trax_config.h):
 *   TRAX_CFG_CTRL_TASK_PRIORITY    (default 1)
 *   TRAX_CFG_CTRL_TASK_STACK_SIZE  (default 256 words)
 *   TRAX_CFG_CTRL_TASK_PERIOD_MS   (default 10 ms)
 */
void trax_task_init(void);

#ifdef __cplusplus
}
#endif

#endif /* TRAX_TASK_H_ */
