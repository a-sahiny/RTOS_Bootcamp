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
 * @file           : trax_rtos_types.h
 * @brief          : TraxProbe RTOS/OS Type Definitions
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 *
 * Named constants for supported RTOS/OS types.
 *
 * Used with:
 *   - TRAX_CFG_RTOS_TYPE  (in trax_config.h)
 *   - os_type field in session header (sent to Traxcope)
 *
 ******************************************************************************
 */

#ifndef TRAX_RTOS_TYPES_H_
#define TRAX_RTOS_TYPES_H_

#define TRAX_RTOS_NONE          0       /**< Bare metal (no RTOS) */
#define TRAX_RTOS_FREERTOS      1       /**< FreeRTOS */
#define TRAX_RTOS_ZEPHYR        2       /**< Zephyr RTOS */
#define TRAX_RTOS_THREADX       3       /**< Azure RTOS ThreadX */
#define TRAX_RTOS_RTTHREAD      4       /**< RT-Thread */
#define TRAX_RTOS_EMBOS         5       /**< Segger embOS */
#define TRAX_RTOS_UCOSII        6       /**< Micrium uC/OS-II */
#define TRAX_RTOS_UCOSIII       7       /**< Micrium uC/OS-III */
#define TRAX_RTOS_CMSIS_RTOS    8       /**< CMSIS-RTOS (generic) */
#define TRAX_RTOS_NUTTX         9       /**< Apache NuttX */
#define TRAX_RTOS_CHIBIOS       10      /**< ChibiOS/RT */
#define TRAX_RTOS_MBED          11      /**< Mbed OS */
#define TRAX_RTOS_CUSTOM        255     /**< User-defined RTOS */

#endif /* TRAX_RTOS_TYPES_H_ */
