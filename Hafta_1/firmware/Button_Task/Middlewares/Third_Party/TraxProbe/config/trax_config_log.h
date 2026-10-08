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
 * @file           : trax_config_log.h
 * @brief          : TRAX Logging Configuration
 * @version        : 2.0.0
 ******************************************************************************
 * @attention
 * 
 * This file contains logging-related configuration:
 *   - Log level definitions
 *   - Compile-time log level filtering
 *   - Metadata control (file:line)
 * 
 * DO NOT MODIFY THIS FILE!
 * To override, define values in App/Config/trax_config.h
 * 
 ******************************************************************************
 */

#ifndef TRAX_CONFIG_LOG_H_
#define TRAX_CONFIG_LOG_H_

/*=============================================================================
====================LOG LEVEL DEFINITIONS======================================
============================================================================*/

/**
 * @brief Log level definitions (severity hierarchy)
 *
 * Log level constants are defined in trax_log_levels.h
 */
#include "trax_log_levels.h"

/**
 * @brief Hex dump flag (bit 8 of level parameter)
 * 
 * Used to distinguish hex dump frames from regular log frames.
 * Set in first parameter: level_with_flag = level | TRAX_LOG_HEX_FLAG
 * 
 * Detection: if (param1 & TRAX_LOG_HEX_FLAG) → It's a hex dump
 * Extract level: log_level = param1 & TRAX_LOG_LEVEL_MASK
 */
#define TRAX_LOG_HEX_FLAG           0x100U  /**< Hex dump indicator (bit 8) */
#define TRAX_LOG_LEVEL_MASK         0x0FFU  /**< Log level mask (bits 0-7) */

/**
 * @brief Macros for hex flag operations
 * 
 * These macros provide clean interface for hex dump flag manipulation:
 * - TRAX_LOG_SET_HEX_FLAG(level): Set hex flag on log level
 * - TRAX_LOG_IS_HEX(level_with_flag): Check if frame is hex dump
 * - TRAX_LOG_GET_LEVEL(level_with_flag): Extract log level from flagged value
 */
#define TRAX_LOG_SET_HEX_FLAG(level)    ((uint32_t)(level) | TRAX_LOG_HEX_FLAG)
#define TRAX_LOG_IS_HEX(level_with_flag) (((level_with_flag) & TRAX_LOG_HEX_FLAG) != 0)
#define TRAX_LOG_GET_LEVEL(level_with_flag) ((uint8_t)((level_with_flag) & TRAX_LOG_LEVEL_MASK))

/*=============================================================================
====================COMPILE-TIME LOG LEVEL FILTERING===========================
============================================================================*/

/**
 * @brief Global compile-time log level threshold
 * 
 * Default: TRAX_LOG_LEVEL_INFO (6)
 * 
 * PURPOSE:
 *   Controls which log levels are compiled into the binary.
 *   Logs ABOVE this level are completely removed at compile-time (zero overhead).
 * 
 * HOW IT WORKS:
 *   - Logs at or BELOW this level → Compiled in (code generated)
 *   - Logs ABOVE this level → Removed by preprocessor (no code, zero cost)
 * 
 * LOG LEVELS (lower number = higher severity):
 *   0 = EMERGENCY  (Always compiled - critical system failure)
 *   1 = ALERT      (System requires immediate attention)
 *   2 = CRITICAL   (Critical condition)
 *   3 = ERROR      (Error condition)
 *   4 = WARNING    (Warning condition)
 *   5 = NOTICE     (Normal but significant)
 *   6 = INFO       (Informational - DEFAULT)
 *   7 = DEBUG      (Debug messages)
 *   8 = TRACE      (Trace messages - most verbose)
 * 
 * EXAMPLES:
 *   Set to INFO (6):    INFO, NOTICE, WARNING, ERROR, CRITICAL, ALERT, EMERGENCY compiled
 *   Set to WARNING (4): WARNING, ERROR, CRITICAL, ALERT, EMERGENCY compiled (INFO/DEBUG removed)
 *   Set to ERROR (3):   Only ERROR and above compiled (saves code space)
 * 
 * BENEFITS:
 *   - Reduces code size (debug logs completely removed in production)
 *   - Zero runtime overhead for disabled logs
 *   - Optimized for production builds
 * 
 * USAGE IN BUILD CONFIGURATIONS:
 *   Development:  #define TRAX_LOG_COMPILE_LEVEL TRAX_LOG_LEVEL_DEBUG   (all logs)
 *   Production:   #define TRAX_LOG_COMPILE_LEVEL TRAX_LOG_LEVEL_WARNING (errors/warnings only)
 *   Release:      #define TRAX_LOG_COMPILE_LEVEL TRAX_LOG_LEVEL_ERROR   (errors only)
 * 
 * To override: Define in App/Config/trax_config.h:
 *   #define TRAX_LOG_COMPILE_LEVEL TRAX_LOG_LEVEL_WARNING
 */
#ifndef TRAX_LOG_COMPILE_LEVEL
#define TRAX_LOG_COMPILE_LEVEL      TRAX_LOG_LEVEL_TRACE
#endif

/**
 * @brief Per-file compile-time log level override
 * 
 * Default: TRAX_LOG_COMPILE_LEVEL (inherits global setting)
 * 
 * PURPOSE:
 *   Allows individual source files to override the global log level.
 *   Useful for enabling detailed logs in specific modules while keeping
 *   others quiet.
 * 
 * HOW IT WORKS:
 *   1. By default, all files use TRAX_LOG_COMPILE_LEVEL
 *   2. A file can override by defining TRAX_FILE_LOG_LEVEL before including trax_log.h
 *   3. That file's logs are then filtered based on its own threshold
 * 
 * USAGE EXAMPLE (in your source file):
 * 
 *   // my_driver.c - Enable DEBUG logs only for this file
 *   #define TRAX_FILE_LOG_LEVEL TRAX_LOG_LEVEL_DEBUG
 *   #include "trax_log.h"
 * 
 *   void my_function(void) {
 *       TRAX_LOG_DEBUG(1001, "This will be compiled in!");
 *       TRAX_LOG_INFO(1002, "This too!");
 *   }
 * 
 *   // other_file.c - Uses global level (e.g., WARNING)
 *   #include "trax_log.h"
 * 
 *   void other_function(void) {
 *       TRAX_LOG_DEBUG(2001, "This is REMOVED (below global WARNING threshold)");
 *       TRAX_LOG_ERROR(2002, "This is compiled in");
 *   }
 * 
 * COMMON USE CASES:
 *   - Debugging specific module: Set TRAX_FILE_LOG_LEVEL to DEBUG in that file
 *   - Quiet noisy module: Set TRAX_FILE_LOG_LEVEL to ERROR to suppress INFO/WARNING
 *   - Protocol tracing: Set DEBUG level only in protocol handler files
 * 
 * IMPORTANT:
 *   - Must be defined BEFORE #include "trax_log.h"
 *   - Affects only the file where it's defined
 *   - Does NOT affect other files
 * 
 * NOTE:
 *   This is a compile-time setting. To change it, you must recompile the file.
 *   For runtime filtering, use TRAX's runtime log level control (if enabled).
 */
#ifndef TRAX_FILE_LOG_LEVEL
#define TRAX_FILE_LOG_LEVEL         TRAX_LOG_COMPILE_LEVEL
#endif


/*=============================================================================
====================START_TRACE KEYWORD=======================================
============================================================================*/
/* TRAX_SESSION_KEYWORD is defined in trax_meta_tx.h (not a log config item) */

/*=============================================================================
====================CONFIGURATION VALIDATION===================================
============================================================================*/

/**
 * Validate log level configuration
 */
#if TRAX_LOG_COMPILE_LEVEL > 8
    #error "TRAX_LOG_COMPILE_LEVEL cannot exceed 8 (TRAX_LOG_LEVEL_TRACE)"
#endif

#if TRAX_LOG_COMPILE_LEVEL < 0
    #error "TRAX_LOG_COMPILE_LEVEL cannot be negative"
#endif

/* Warn if file log level is less restrictive than the global compile level */
#ifdef TRAX_FILE_LOG_LEVEL
    #if TRAX_FILE_LOG_LEVEL > TRAX_LOG_COMPILE_LEVEL
        #warning "TRAX_FILE_LOG_LEVEL is higher than TRAX_LOG_COMPILE_LEVEL. TRAX_LOG_COMPILE_LEVEL acts as a hard global ceiling - logs above it are always compiled out regardless of TRAX_FILE_LOG_LEVEL."
    #endif
#endif

#endif /* TRAX_CONFIG_LOG_H_ */

