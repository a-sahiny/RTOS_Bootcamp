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
 * @file           : trax_log_levels.h
 * @brief          : TraxProbe Log Level Definitions
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 *
 * Syslog-style log level constants (RFC 5424).
 * Lower number = higher severity.
 *
 * Used with:
 *   - TRAX_FILE_LOG_LEVEL         (per-file override)
 *   - Log frame param[0]          (level encoded in each log frame)
 *
 ******************************************************************************
 */

#ifndef TRAX_LOG_LEVELS_H_
#define TRAX_LOG_LEVELS_H_

#define TRAX_LOG_LEVEL_EMERGENCY    0U  /**< System unusable - critical failure */
#define TRAX_LOG_LEVEL_ALERT        1U  /**< Action must be taken immediately */
#define TRAX_LOG_LEVEL_CRITICAL     2U  /**< Critical conditions */
#define TRAX_LOG_LEVEL_ERROR        3U  /**< Error conditions */
#define TRAX_LOG_LEVEL_WARNING      4U  /**< Warning conditions */
#define TRAX_LOG_LEVEL_NOTICE       5U  /**< Normal but significant */
#define TRAX_LOG_LEVEL_INFO         6U  /**< Informational messages */
#define TRAX_LOG_LEVEL_DEBUG        7U  /**< Debug-level messages */
#define TRAX_LOG_LEVEL_TRACE        8U  /**< Trace-level messages (most verbose) */

#endif /* TRAX_LOG_LEVELS_H_ */
