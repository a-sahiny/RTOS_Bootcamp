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
 * @file           : trax_log.h
 * @brief          : TRAX logging API for remote binary logging
 * @version        : 4.0.0
 ******************************************************************************
 * @attention
 * 
 * This module provides a high-performance binary logging API:
 *   - Format strings auto-captured at each call site (stored in ELF)
 *   - Runtime logging uses TID + arguments only (no format string sent)
 *   - PC application decodes using TID->Format mapping from ELF metadata
 *   - Optional channel bindings for data visualization
 * 
 * NEW API (v4.0): Format string included in log call, auto-stored in metadata
 * 
 * Basic Usage:
 *   // Just use TRAX_LOG_xxx with format and args - no separate definition!
 *   TRAX_LOG_INFO(TRAX_TID_TEMP, "Temperature: %d C", temperature);
 *   TRAX_LOG_ERROR(TRAX_TID_ERR, "Sensor failed: 0x%08x", error);
 * 
 * With Channel Bindings (optional):
 *   // Define channel bindings to link log args to signal metadata
 *   TRAX_LOG_DEFINE_VARS(TRAX_TID_MOTOR, TRAX_TID_TEMP_CH, TRAX_TID_SPEED_CH);
 *   
 *   // Use in code - args are now linked to channels
 *   TRAX_LOG_INFO(TRAX_TID_MOTOR, "Temp=%d Speed=%d", temp, speed);
 *   // → Scope displays log AND updates TRAX_TID_TEMP_CH/TRAX_TID_SPEED_CH plots!
 * 
 * Hex Dump Logs:
 *   TRAX_HEX_DEBUG(TRAX_TID_SPI, buffer, 16, "SPI TX");
 * 
 ******************************************************************************
 */

#ifndef TRAX_LOG_H_
#define TRAX_LOG_H_

#include "trax_config_default.h"
#include "trax_utility.h"       /* For TRAX_PUT32, TRAX_PUT_ARG_AUTO, TRAX_ARG_CNT */
#include "trax_frame.h"         /* For frame creation macros */
#include "trax_data_types.h"    /* TRAX_SECTION, TRAX_STATIC_ASSERT */
#include "trax_tid.h"           /* TRAX_TID_RANGE_LOG_START / _END for range checks */
#include "trax_meta_type.h"     /* For trax_log_meta_t, trax_log_vars_meta_t */
#include "trax_meta_section.h"  /* TRAX_META_SECTION_LOG / _LOG_VARS — flash-vs-elf-only switch */

/*=============================================================================
 ====================LOG→VAR BINDING METADATA==================================
 ============================================================================*/

/**
 * @brief Define log argument → VAR bindings (stored in ELF .trax_log_vars section)
 *
 * OPTIONAL — tells Scope which log arguments correspond to VARs.
 *
 * Usage:
 *   TRAX_LOG_DEFINE_VARS(TRAX_TID_MOTOR_LOG, VAR_TEMP, 0, VAR_SPEED, 0);
 *   //  → arg[0]=VAR_TEMP, arg[1]=plain, arg[2]=VAR_SPEED, arg[3]=plain
 *
 * @param _tid  Trace ID (must match the TID used in TRAX_LOG_xxx calls)
 * @param ...   VAR TIDs for each argument in order (0 = not a VAR)
 *
 * @note Argument count is auto-calculated from variadic arguments.
 */
#if TRAX_ENABLE
#define TRAX_LOG_DEFINE_VARS(_tid, ...) \
    TRAX_STATIC_ASSERT(TRAX_ARG_CNT(__VA_ARGS__) <= TRAX_CFG_LOG_MAX_ARGS, \
        "Argument count exceeds TRAX_CFG_LOG_MAX_ARGS"); \
    static const struct trax_log_vars_meta_t __trax_log_vars_##_tid \
        TRAX_SECTION(TRAX_META_SECTION_LOG_VARS) = { \
            .id = (_tid), \
            .arg_count = TRAX_ARG_CNT(__VA_ARGS__), \
            .reserved = 0, \
            .p_arg_vars = { __VA_ARGS__ } \
        }
#else
/* TraxProbe disabled (TRAX_ENABLE==0): emit no metadata. The benign struct
 * forward-declaration absorbs the trailing ';' at file scope with zero
 * footprint and is legal to repeat. */
#define TRAX_LOG_DEFINE_VARS(_tid, ...) struct trax_log_vars_meta_t
#endif


/*=============================================================================
 ====================GLOBAL MACRO DEFINITIONS==================================
 ============================================================================*/
/* Note: Log level definitions moved to trax_config_default.h (line ~158)
 * They are configuration values and need to be available early for defaults. */

/*=============================================================================
 ====================LOG LEVEL ENUM============================================
 ============================================================================*/
/**
 * @brief Log severity levels (syslog-style)
 * 
 * These levels follow the standard syslog severity levels (RFC 5424).
 * Higher numbers indicate more verbose/less critical messages.
 */
enum trax_log_level_t {
    TRAX_LEVEL_EMERGENCY = 0,   /**< System is unusable */
    TRAX_LEVEL_ALERT     = 1,   /**< Action must be taken immediately */
    TRAX_LEVEL_CRITICAL  = 2,   /**< Critical conditions */
    TRAX_LEVEL_ERROR     = 3,   /**< Error conditions */
    TRAX_LEVEL_WARNING   = 4,   /**< Warning conditions */
    TRAX_LEVEL_NOTICE    = 5,   /**< Normal but significant */
    TRAX_LEVEL_INFO      = 6,   /**< Informational */
    TRAX_LEVEL_DEBUG     = 7,   /**< Debug-level messages */
    TRAX_LEVEL_TRACE     = 8,   /**< Detailed trace messages */
};

/*=============================================================================
====================METADATA CONFIGURATION====================================
============================================================================*/


/*=============================================================================
====================METADATA FLAGS=============================================
============================================================================*/
/**
 * @brief Log metadata flag (stored in trax_log_meta_t.flags)
 * 
 * These flags are stored in compile-time metadata, NOT encoded in TID at runtime.
 * This simplifies the protocol: TID is always the base TID.
 */
#define TRAX_LOG_FLAG_HEX    0x01U   /**< This is a hex dump log */

/*=============================================================================
====================GLOBAL MACRO FUNCTIONS====================================
============================================================================*/

/**
 * @brief Base logging macro for remote mode (with auto-generated format metadata)
 * 
 * Log level and flags are stored in metadata (compile-time), NOT encoded in TID.
 * Runtime sends only base TID + arguments = simpler and more efficient.
 * 
 * Format string is automatically captured in ELF metadata at each call site.
 * No need for separate TRAX_LOG_DEFINE - just use TRAX_LOG_INFO directly!
 * 
 * Optionally, use TRAX_LOG_DEFINE_VARS to link arguments to channels.
 * 
 * @param level Log level (0-8) - stored in metadata, not sent at runtime
 * @param tid   Log TID (unique identifier for this log message)
 * @param fmt   Printf-style format string (auto-stored in ELF metadata)
 * @param ...   Variable arguments matching the format
 */
/**
 * @brief Base logging macro with context name
 * 
 * @param level Log level (0-8)
 * @param tid   Log TID
 * @param tag   Filter tag string (shown in Context column, used for log filtering in Traxcope)
 * @param fmt   Printf-style format string
 * @param ...   Variable arguments
 */
#if TRAX_ENABLE
#define TRAX_LOG_BASE(_level, _tid, _tag, _fmt, ...) \
    do { \
        TRAX_STATIC_ASSERT( \
            (_tid) >= TRAX_TID_RANGE_LOG_START && (_tid) <= TRAX_TID_RANGE_LOG_END, \
            "TRAX_LOG: TID is outside the LOG range (0x0100-0x1FFF)"); \
        /* Auto-generate format metadata - one per TID */ \
        /* static ensures single definition per file */ \
        static const struct trax_log_meta_t \
            TRAX_SECTION(TRAX_META_SECTION_LOG) \
            __trax_log_fmt_##_tid = { \
                .id = (_tid), \
                .reserved1 = 0, \
                .arg_count = TRAX_ARG_CNT(__VA_ARGS__), \
                .level = (_level), \
                .flags = 0, \
                .reserved2 = 0, \
                .p_tag = _tag, \
                .p_format = _fmt \
            }; \
        /* Runtime: send base TID + values only */ \
        TRAX_FRAME_ARGS_ATOMIC((_tid), ##__VA_ARGS__); \
    } while(0)
#else
#define TRAX_LOG_BASE(_level, _tid, _tag, _fmt, ...) ((void)0)
#endif


/*=============================================================================
====================HEX DUMP BASE MACROS======================================
============================================================================*/
/**
 * @brief Base hex dump macro for remote mode (with auto-generated format metadata)
 * 
 * Log level and HEX flag are stored in metadata (compile-time), NOT encoded in TID.
 * Runtime sends only base TID + data = simpler and more efficient.
 * 
 * Format/label string is automatically captured in ELF metadata.
 * 
 * Frame Structure:
 *   - TID: Base TID only (level/flags in metadata)
 *   - Param 1: data_size (32-bit, size of hex data in bytes)
 *   - Param 2+: Variable arguments (optional, for PC-side formatting)
 *   - Data: Raw hex data (variable length, padded to 32-bit boundary)
 * 
 * @param level     Log level (0-8) - stored in metadata, not sent at runtime
 * @param tid       Log TID
 * @param fmt       Label/format string (auto-stored in ELF metadata)
 * @param data_ptr  Pointer to binary data
 * @param data_size Size of binary data in bytes
 * @param ...       Variable arguments matching the format
 */
/**
 * @brief Base hex dump macro with filter tag
 */
#if TRAX_ENABLE
#define TRAX_HEX_LOG_BASE(_level, _tid, _tag, _data_ptr, _data_size, _fmt, ...) \
    do { \
        /* Auto-generate format metadata - one per TID */ \
        /* Using TID-only name ensures duplicate TID = redefinition error */ \
        static const struct trax_log_meta_t \
            TRAX_SECTION(TRAX_META_SECTION_LOG) \
            __trax_log_fmt_##_tid = { \
                .id = (_tid), \
                .reserved1 = 0, \
                .arg_count = TRAX_ARG_CNT(__VA_ARGS__) + 1, /* +1 for data_size */ \
                .level = (_level), \
                .flags = TRAX_LOG_FLAG_HEX, \
                .reserved2 = 0, \
                .p_tag = _tag, \
                .p_format = _fmt \
            }; \
        /* Runtime: send base TID + data only (no HEX/level encoding) */ \
        TRAX_FRAME_RAW_ARGS((_tid), _data_ptr, _data_size, \
            (uint32_t)(_data_size), ##__VA_ARGS__); \
    } while(0)
#else
#define TRAX_HEX_LOG_BASE(_level, _tid, _tag, _data_ptr, _data_size, _fmt, ...) ((void)0)
#endif

/*=============================================================================
====================REGULAR LOGGING IMPLEMENTATION MACROS======================
============================================================================*/
/**
 * @brief Regular logging implementation macros
 * 
 * These macros implement the actual regular log frame creation and data transfer.
 * They are used by the TRAX_LOG_BASE macros above.
 */


/*=============================================================================
====================LEVEL-SPECIFIC LOGGING MACROS==============================
=============================================================================*/
/**
 * @brief Level-specific logging macros with compile-time filtering
 * 
 * Each macro requires a filter tag for UI display and filtering.
 * Logs above TRAX_FILE_LOG_LEVEL are compiled out (zero overhead).
 * 
 * Usage:
 *   TRAX_LOG_INFO(TRAX_TID_TEMP, "temp_sensor", "Temperature: %d C", temperature);
 *   TRAX_LOG_ERROR(TRAX_TID_ERR, "motor_ctrl", "Motor failed: 0x%08x", error);
 *   
 *   // In Traxcope EventLog:
 *   //   Tag="temp_sensor", Summary="[INFO] Temperature: 25 C"
 *   //   Tag="motor_ctrl", Summary="[ERROR] Motor failed: 0x00000005"
 */

#if (TRAX_LOG_LEVEL_EMERGENCY <= TRAX_LOG_COMPILE_LEVEL) && (TRAX_LOG_LEVEL_EMERGENCY <= TRAX_FILE_LOG_LEVEL)
#define TRAX_LOG_EMERGENCY(tid, tag, fmt, ...) \
	TRAX_LOG_BASE(TRAX_LOG_LEVEL_EMERGENCY, tid, tag, fmt, ##__VA_ARGS__)
#else
#define TRAX_LOG_EMERGENCY(tid, tag, fmt, ...) ((void)0)
#endif

#if (TRAX_LOG_LEVEL_ALERT <= TRAX_LOG_COMPILE_LEVEL) && (TRAX_LOG_LEVEL_ALERT <= TRAX_FILE_LOG_LEVEL)
#define TRAX_LOG_ALERT(tid, tag, fmt, ...) \
	TRAX_LOG_BASE(TRAX_LOG_LEVEL_ALERT, tid, tag, fmt, ##__VA_ARGS__)
#else
#define TRAX_LOG_ALERT(tid, tag, fmt, ...) ((void)0)
#endif

#if (TRAX_LOG_LEVEL_CRITICAL <= TRAX_LOG_COMPILE_LEVEL) && (TRAX_LOG_LEVEL_CRITICAL <= TRAX_FILE_LOG_LEVEL)
#define TRAX_LOG_CRITICAL(tid, tag, fmt, ...) \
	TRAX_LOG_BASE(TRAX_LOG_LEVEL_CRITICAL, tid, tag, fmt, ##__VA_ARGS__)
#else
#define TRAX_LOG_CRITICAL(tid, tag, fmt, ...) ((void)0)
#endif

#if (TRAX_LOG_LEVEL_ERROR <= TRAX_LOG_COMPILE_LEVEL) && (TRAX_LOG_LEVEL_ERROR <= TRAX_FILE_LOG_LEVEL)
#define TRAX_LOG_ERROR(tid, tag, fmt, ...) \
	TRAX_LOG_BASE(TRAX_LOG_LEVEL_ERROR, tid, tag, fmt, ##__VA_ARGS__)
#else
#define TRAX_LOG_ERROR(tid, tag, fmt, ...) ((void)0)
#endif

#if (TRAX_LOG_LEVEL_WARNING <= TRAX_LOG_COMPILE_LEVEL) && (TRAX_LOG_LEVEL_WARNING <= TRAX_FILE_LOG_LEVEL)
#define TRAX_LOG_WARNING(tid, tag, fmt, ...) \
	TRAX_LOG_BASE(TRAX_LOG_LEVEL_WARNING, tid, tag, fmt, ##__VA_ARGS__)
#else
#define TRAX_LOG_WARNING(tid, tag, fmt, ...) ((void)0)
#endif

#if (TRAX_LOG_LEVEL_NOTICE <= TRAX_LOG_COMPILE_LEVEL) && (TRAX_LOG_LEVEL_NOTICE <= TRAX_FILE_LOG_LEVEL)
#define TRAX_LOG_NOTICE(tid, tag, fmt, ...) \
	TRAX_LOG_BASE(TRAX_LOG_LEVEL_NOTICE, tid, tag, fmt, ##__VA_ARGS__)
#else
#define TRAX_LOG_NOTICE(tid, tag, fmt, ...) ((void)0)
#endif

#if (TRAX_LOG_LEVEL_INFO <= TRAX_LOG_COMPILE_LEVEL) && (TRAX_LOG_LEVEL_INFO <= TRAX_FILE_LOG_LEVEL)
#define TRAX_LOG_INFO(tid, tag, fmt, ...) \
	TRAX_LOG_BASE(TRAX_LOG_LEVEL_INFO, tid, tag, fmt, ##__VA_ARGS__)
#else
#define TRAX_LOG_INFO(tid, tag, fmt, ...) ((void)0)
#endif

#if (TRAX_LOG_LEVEL_DEBUG <= TRAX_LOG_COMPILE_LEVEL) && (TRAX_LOG_LEVEL_DEBUG <= TRAX_FILE_LOG_LEVEL)
#define TRAX_LOG_DEBUG(tid, tag, fmt, ...) \
	TRAX_LOG_BASE(TRAX_LOG_LEVEL_DEBUG, tid, tag, fmt, ##__VA_ARGS__)
#else
#define TRAX_LOG_DEBUG(tid, tag, fmt, ...) ((void)0)
#endif

#if (TRAX_LOG_LEVEL_TRACE <= TRAX_LOG_COMPILE_LEVEL) && (TRAX_LOG_LEVEL_TRACE <= TRAX_FILE_LOG_LEVEL)
#define TRAX_LOG_TRACE(tid, tag, fmt, ...) \
	TRAX_LOG_BASE(TRAX_LOG_LEVEL_TRACE, tid, tag, fmt, ##__VA_ARGS__)
#else
#define TRAX_LOG_TRACE(tid, tag, fmt, ...) ((void)0)
#endif

/* Default logging macro (INFO level) */
#define TRAX_LOG(tid, tag, fmt, ...) \
	TRAX_LOG_INFO(tid, tag, fmt, ##__VA_ARGS__)

/*=============================================================================
====================HEX DUMP LOGGING MACROS=====================================
============================================================================*/
/**
 * @brief Hex dump logging macros with filter tag
 * 
 * Usage Examples:
 *   TRAX_HEX_INFO(TRAX_TID_SPI_TX, "spi_driver", buffer, 16, "SPI TX Data");
 *   TRAX_HEX_ERROR(TRAX_TID_I2C, "i2c_driver", error_data, 32, "I2C %d Error", i2c_num);
 */

#if (TRAX_LOG_LEVEL_EMERGENCY <= TRAX_LOG_COMPILE_LEVEL) && (TRAX_LOG_LEVEL_EMERGENCY <= TRAX_FILE_LOG_LEVEL)
#define TRAX_HEX_EMERGENCY(tid, tag, data_ptr, data_size, fmt, ...) \
	TRAX_HEX_LOG_BASE(TRAX_LOG_LEVEL_EMERGENCY, tid, tag, data_ptr, data_size, fmt, ##__VA_ARGS__)
#else
#define TRAX_HEX_EMERGENCY(tid, tag, data_ptr, data_size, fmt, ...) ((void)0)
#endif

#if (TRAX_LOG_LEVEL_ALERT <= TRAX_LOG_COMPILE_LEVEL) && (TRAX_LOG_LEVEL_ALERT <= TRAX_FILE_LOG_LEVEL)
#define TRAX_HEX_ALERT(tid, tag, data_ptr, data_size, fmt, ...) \
	TRAX_HEX_LOG_BASE(TRAX_LOG_LEVEL_ALERT, tid, tag, data_ptr, data_size, fmt, ##__VA_ARGS__)
#else
#define TRAX_HEX_ALERT(tid, tag, data_ptr, data_size, fmt, ...) ((void)0)
#endif

#if (TRAX_LOG_LEVEL_CRITICAL <= TRAX_LOG_COMPILE_LEVEL) && (TRAX_LOG_LEVEL_CRITICAL <= TRAX_FILE_LOG_LEVEL)
#define TRAX_HEX_CRITICAL(tid, tag, data_ptr, data_size, fmt, ...) \
	TRAX_HEX_LOG_BASE(TRAX_LOG_LEVEL_CRITICAL, tid, tag, data_ptr, data_size, fmt, ##__VA_ARGS__)
#else
#define TRAX_HEX_CRITICAL(tid, tag, data_ptr, data_size, fmt, ...) ((void)0)
#endif

#if (TRAX_LOG_LEVEL_ERROR <= TRAX_LOG_COMPILE_LEVEL) && (TRAX_LOG_LEVEL_ERROR <= TRAX_FILE_LOG_LEVEL)
#define TRAX_HEX_ERROR(tid, tag, data_ptr, data_size, fmt, ...) \
	TRAX_HEX_LOG_BASE(TRAX_LOG_LEVEL_ERROR, tid, tag, data_ptr, data_size, fmt, ##__VA_ARGS__)
#else
#define TRAX_HEX_ERROR(tid, tag, data_ptr, data_size, fmt, ...) ((void)0)
#endif

#if (TRAX_LOG_LEVEL_WARNING <= TRAX_LOG_COMPILE_LEVEL) && (TRAX_LOG_LEVEL_WARNING <= TRAX_FILE_LOG_LEVEL)
#define TRAX_HEX_WARNING(tid, tag, data_ptr, data_size, fmt, ...) \
	TRAX_HEX_LOG_BASE(TRAX_LOG_LEVEL_WARNING, tid, tag, data_ptr, data_size, fmt, ##__VA_ARGS__)
#else
#define TRAX_HEX_WARNING(tid, tag, data_ptr, data_size, fmt, ...) ((void)0)
#endif

#if (TRAX_LOG_LEVEL_NOTICE <= TRAX_LOG_COMPILE_LEVEL) && (TRAX_LOG_LEVEL_NOTICE <= TRAX_FILE_LOG_LEVEL)
#define TRAX_HEX_NOTICE(tid, tag, data_ptr, data_size, fmt, ...) \
	TRAX_HEX_LOG_BASE(TRAX_LOG_LEVEL_NOTICE, tid, tag, data_ptr, data_size, fmt, ##__VA_ARGS__)
#else
#define TRAX_HEX_NOTICE(tid, tag, data_ptr, data_size, fmt, ...) ((void)0)
#endif

#if (TRAX_LOG_LEVEL_INFO <= TRAX_LOG_COMPILE_LEVEL) && (TRAX_LOG_LEVEL_INFO <= TRAX_FILE_LOG_LEVEL)
#define TRAX_HEX_INFO(tid, tag, data_ptr, data_size, fmt, ...) \
	TRAX_HEX_LOG_BASE(TRAX_LOG_LEVEL_INFO, tid, tag, data_ptr, data_size, fmt, ##__VA_ARGS__)
#else
#define TRAX_HEX_INFO(tid, tag, data_ptr, data_size, fmt, ...) ((void)0)
#endif

#if (TRAX_LOG_LEVEL_DEBUG <= TRAX_LOG_COMPILE_LEVEL) && (TRAX_LOG_LEVEL_DEBUG <= TRAX_FILE_LOG_LEVEL)
#define TRAX_HEX_DEBUG(tid, tag, data_ptr, data_size, fmt, ...) \
	TRAX_HEX_LOG_BASE(TRAX_LOG_LEVEL_DEBUG, tid, tag, data_ptr, data_size, fmt, ##__VA_ARGS__)
#else
#define TRAX_HEX_DEBUG(tid, tag, data_ptr, data_size, fmt, ...) ((void)0)
#endif

#if (TRAX_LOG_LEVEL_TRACE <= TRAX_LOG_COMPILE_LEVEL) && (TRAX_LOG_LEVEL_TRACE <= TRAX_FILE_LOG_LEVEL)
#define TRAX_HEX_TRACE(tid, tag, data_ptr, data_size, fmt, ...) \
	TRAX_HEX_LOG_BASE(TRAX_LOG_LEVEL_TRACE, tid, tag, data_ptr, data_size, fmt, ##__VA_ARGS__)
#else
#define TRAX_HEX_TRACE(tid, tag, data_ptr, data_size, fmt, ...) ((void)0)
#endif

/* Default hex dump macro (INFO level) */
#define TRAX_HEX(tid, tag, data_ptr, data_size, fmt, ...) \
	TRAX_HEX_INFO(tid, tag, data_ptr, data_size, fmt, ##__VA_ARGS__)

/*=============================================================================
 ====================GLOBAL TYPEDEF============================================
 ============================================================================*/

/*=============================================================================
 ====================GLOBAL VARIABLES==========================================
 ============================================================================*/

/*=============================================================================
 ====================GLOBAL FUNCTION DECLERATION===============================
 ============================================================================*/


#endif /* TRAX_LOG_H_ */
