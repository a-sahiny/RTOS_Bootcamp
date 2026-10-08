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
 * @file           : trax_config_default.h
 * @brief          : TRAX Library Default Configuration (Main)
 * @version        : 2.0.0
 ******************************************************************************
 * @attention
 * 
 * This is the MAIN configuration file for TRAX library.
 * 
 * Configuration Architecture (Modular Design):
 *   1. User MUST create: App/Config/trax_config.h (your custom settings)
 *   2. This file includes user config (below)
 *   3. This file includes modular sub-configuration files via relative paths:
 *      - ../config/trax_config_log.h      (Log levels, filtering, metadata)
 *      - ../config/trax_config_buffer.h   (Buffer sizes, frame limits)
 *      - ../config/trax_config_hw_port.h  (Timestamp, timer, sync settings)
 *      - ../config/trax_config_rtos.h     (RTOS type, task config)
 *      - ../config/trax_config_meta.h     (Metadata storage)
 *   4. Each sub-config provides defaults that can be overridden
 * 
 * Required Include Paths (only 3):
 *   - TraxProbe/include/                          (library headers)
 *   - TraxProbe/hw_port/<your_platform>/include/ (hardware port)
 *   - App/Config/                                 (user's trax_config.h)
 * 
 * IMPORTANT: You MUST create App/Config/trax_config.h
 * 
 * DO NOT MODIFY THIS FILE!
 * To configure, create App/Config/trax_config.h and define your overrides there.
 * 
 * Example trax_config.h:
 *   
 *   // Set production log level
 *   #define TRAX_LOG_COMPILE_LEVEL TRAX_LOG_LEVEL_WARNING
 *   
 *   // Configure timestamp timer (STM32 TIM2, count-up)
 *   #define TRAX_CFG_TIMESTAMP_TIMER_VAL (TIM2->CNT)
 * 
 ******************************************************************************
 */

#ifndef TRAX_CONFIG_DEFAULT_H_
#define TRAX_CONFIG_DEFAULT_H_

#include <stdint.h>
#include <stddef.h>

/*=============================================================================
====================INCLUDE USER CONFIGURATION (REQUIRED)=======================
=============================================================================*/

/**
 * Option constants (TRAX_META_ELF_ONLY, TRAX_META_IN_FLASH, etc.)
 * Included BEFORE user config so named constants are available in trax_config.h.
 */
#include "../config/trax_config_options.h"

/**
 * User configuration file - MUST exist in App/Config/trax_config.h
 * This is where you define your project-specific overrides.
 */
#include "trax_config.h"

/*=============================================================================
====================MASTER ENABLE SWITCH======================================
=============================================================================*/

/**
 * @brief Global TraxProbe master enable switch.
 *
 * Single switch that turns the WHOLE TraxProbe integration on or off across
 * the entire project. Defaults to 1 (enabled).
 *
 * How to disable (pick ONE):
 *   1. Compiler symbol:   -D TRAX_ENABLE=0
 *      (Eclipse: Project ▸ C/C++ Build ▸ Settings ▸ Tool Settings ▸
 *       C Compiler ▸ Preprocessor ▸ Defined symbols)
 *      Project-wide: this is also the form the shared BSP hooks
 *      (bsp_isr.c / bsp_tick.c) see, since they are gated directly on it.
 *   2. In trax_config.h:  #define TRAX_ENABLE 0
 *      Reaches every TraxProbe/app translation unit that includes the config.
 *      If you also ship the -D symbol, remove or zero it first to avoid a
 *      macro-redefinition warning.
 *
 * When disabled (== 0):
 *   - All public API macros (TRAX_LOG_*, TRAX_VAR_*, TRAX_ISR_*,
 *     TRAX_MARKER_*, TRAX_SM_*, TRAX_MAIN_LOOP_*, var-stream, and the
 *     FreeRTOS trace hooks) expand to no-ops — no metadata, no frames.
 *   - The public functions (trax_init/trax_process/…) become inline
 *     no-op stubs and every TraxProbe translation unit compiles to nothing,
 *     so the firmware carries zero TraxProbe footprint.
 */
#ifndef TRAX_ENABLE
#define TRAX_ENABLE 1
#endif

/**
 * TID range constants (TRAX_TID_RANGE_VAR_USER_START, TRAX_TID_RANGE_LOG_USER_START,
 * TRAX_TID_RANGE_ISR_USER_START, etc.). Included BEFORE user config so the
 * USER-range constants can anchor user TID enums.
 */
#include "trax_tid.h"

/*=============================================================================
====================HARDWARE PORT FALLBACKS====================================
=============================================================================*/
#ifndef TRAX_CFG_HW_PORT
    #define TRAX_CFG_HW_PORT TRAX_HW_PORT_NONE
#endif

#ifndef TRAX_CFG_RTOS_TYPE
    #define TRAX_CFG_RTOS_TYPE TRAX_RTOS_NONE
#endif

/*=============================================================================
====================REQUIRED CONFIGURATION VALIDATION=========================
=============================================================================*/

#if (TRAX_CFG_HW_PORT == TRAX_HW_PORT_NONE)
    #error "TRAX_CFG_HW_PORT must be defined in trax_config.h. \
Example: #define TRAX_CFG_HW_PORT  TRAX_HW_PORT_ARM_CORTEX_M"
#endif

/**
 * @brief Transport backend selection (default: built-in SEGGER RTT).
 *
 * Mirrors the TRAX_CFG_HW_PORT / TRAX_CFG_RTOS_TYPE pattern: leave it unset to
 * get the zero-config default (RTT), or pick a named constant in trax_config.h.
 *
 *   TRAX_TRANSPORT_RTT     (default) Built-in SEGGER RTT, compiled and used
 *                          automatically. No user code required.
 *   TRAX_TRANSPORT_CUSTOM  RTT is compiled out — reclaiming its static
 *                          buffers (TRAX_RTT_BUFFER_SIZE_UP +
 *                          TRAX_RTT_BUFFER_SIZE_DOWN, 16 KB + 64 B by
 *                          default). You MUST implement the
 *                          trax_transport_*() functions (see
 *                          trax_transport.h); the linker binds them by name.
 *
 * To use a custom transport, add ONE line to trax_config.h:
 *   #define TRAX_CFG_TRANSPORT  TRAX_TRANSPORT_CUSTOM
 */
#ifndef TRAX_CFG_TRANSPORT
#define TRAX_CFG_TRANSPORT  TRAX_TRANSPORT_RTT
#endif

/*=============================================================================
====================INCLUDE MODULAR SUB-CONFIGURATIONS=========================
=============================================================================*/

/**
 * Include all modular configuration files.
 * Each file is responsible for a specific aspect of TRAX configuration.
 * Order matters: Some files may depend on definitions from earlier ones.
 */

/* Log configuration: Levels, filtering, metadata */
#include "../config/trax_config_log.h"

/* Buffer configuration: Sizes, frame limits, processing mode */
#include "../config/trax_config_buffer.h"

/* Platform configuration: Timestamp, timer, sync settings */
#include "../config/trax_config_hw_port.h"

/* RTOS configuration: Type, task name, ctrl task */
#include "../config/trax_config_rtos.h"

/* Metadata configuration: ELF-only vs on-wire (START_TRACE); see trax_config_meta.h */
#include "../config/trax_config_meta.h"

/* Diagnostic module configuration (mandatory; period / subscribers tunable) */
#include "../config/trax_config_diag.h"

/* TraxFault (crash capture / post-mortem) configuration — opt-in module */
#include "../config/trax_config_fault.h"

/*=============================================================================
====================CONFIGURATION VALIDATION===================================
=============================================================================*/

/**
 * Configuration validation is distributed across modular config files.
 * Each module validates its own settings for better separation of concerns.
 * 
 * Validation locations:
 *   - config/trax_config_log.h:      Log level bounds, file-level overrides
 *   - config/trax_config_buffer.h:   Buffer sizes, frame limits
 *   - config/trax_config_format.h:   Format dependencies, boolean values
 *   - config/trax_config_hw_port.h: Timestamp mode, timer direction, sync
 *   - config/trax_config_rtos.h:     RTOS type, version, task config
 * 
 * Benefits:
 *   - Validation co-located with configuration
 *   - Each module is self-contained
 *   - Easy to understand which module owns which validation
 *   - Scalable architecture
 */

/*=============================================================================
====================CONFIGURATION SUMMARY======================================
=============================================================================*/

/**
 * Configuration summary (for documentation and debugging)
 * 
 * Buffer Sizes:
 *   TRAX_CFG_OUT_BUFFER_SIZE32: Output buffer (words)
 *   TRAX_CFG_IN_BUFFER_SIZE32: Input buffer (words)
 *   TRAX_CFG_FRAME_MAX_SIZE32: Max frame size (words)
 * 
 * Log Filtering:
 *   TRAX_LOG_COMPILE_LEVEL: Global compile-time threshold
 *   TRAX_FILE_LOG_LEVEL: Per-file override (default: same as global)
 *  *
 * Platform Settings (hardware-dependent):
 *   TRAX_CFG_TIMESTAMP_TIMER_VAL: Timer counter register
 */

/*=============================================================================
====================END OF CONFIGURATION=======================================
=============================================================================*/

#endif /* TRAX_CONFIG_DEFAULT_H_ */
