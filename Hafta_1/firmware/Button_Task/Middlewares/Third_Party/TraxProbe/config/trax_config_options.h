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
 * @file           : trax_config_options.h
 * @brief          : TraxProbe Configuration Option Constants
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 *
 * Central include for all named constants used in trax_config.h.
 * Included BEFORE the user config so IntelliSense can resolve them.
 *
 * DO NOT put default values here — only option constants.
 *
 ******************************************************************************
 */

#ifndef TRAX_CONFIG_OPTIONS_H_
#define TRAX_CONFIG_OPTIONS_H_

/*--- Log level constants (TRAX_LOG_LEVEL_*) ---*/
#include "trax_log_levels.h"

/*--- RTOS/OS type constants (TRAX_RTOS_*, TRAX_OS_*) ---*/
#include "trax_rtos_types.h"

/*=============================================================================
 ====================HARDWARE PORT TYPES=======================================
 ============================================================================*/
#define TRAX_HW_PORT_NONE              0
#define TRAX_HW_PORT_ARM_CORTEX_M      1
#define TRAX_HW_PORT_GENERIC           3
#define TRAX_HW_PORT_ZYNQ              4   /* Xilinx Zynq-7000 (A9) / ZynqMP (A53/R5) */
#define TRAX_HW_PORT_XTENSA            5   /* Espressif Xtensa LX6/LX7 (ESP32 / -S2 / -S3), ESP-IDF + SMP FreeRTOS */
#define TRAX_HW_PORT_MICROBLAZE        6   /* Xilinx MicroBlaze soft core (little-endian), AXI Timer timestamp */

/*=============================================================================
 ====================TRANSPORT TYPES===========================================
 ============================================================================*/

/**
 * Transport backend selection (TRAX_CFG_TRANSPORT).
 *
 * TRAX_TRANSPORT_RTT (default)
 *   - Built-in SEGGER RTT transport is compiled in and used automatically.
 *   - Zero configuration: no trax_config.h entry and no user code required.
 *
 * TRAX_TRANSPORT_CUSTOM
 *   - The RTT transport is compiled out entirely, reclaiming its static
 *     buffers (TRAX_RTT_BUFFER_SIZE_UP + TRAX_RTT_BUFFER_SIZE_DOWN of SRAM).
 *   - You MUST implement the trax_transport_*() functions (see
 *     trax_transport.h). The transport is bound at link time by name — there is
 *     no ops struct and no register() call.
 *
 * Selection is compile-time only: exactly one backend (RTT or custom) is built.
 */
#define TRAX_TRANSPORT_RTT      0
#define TRAX_TRANSPORT_CUSTOM   1

/*=============================================================================
 ====================METADATA STORAGE MODES====================================
 ============================================================================*/

/**
 * Static metadata (logs/vars/streams/…) always ends up in the **ELF** when you
 * link with the usual `.trax_*` sections. The choice here is whether the probe
 * also **copies those tables into the START_TRACE frame** on the wire.
 *
 * TRAX_META_ELF_ONLY (0)
 *   - Probe does not embed static metadata arrays in START_TRACE (counts can be
 *     zero in the header; see protocol).
 *   - Traxcope (or other host) loads static schema from the **ELF file** path.
 *   - On MCUs: use a NOLOAD / ELF-only linker fragment so `.hex`/`.bin` stay small.
 *
 * TRAX_META_IN_FLASH (1)  [historical name: “in flash” on MCU]
 *   - Probe includes static metadata in **START_TRACE** (sent at session start).
 *   - Host can work without an ELF, or merge ELF + wire data as implemented.
 */
#define TRAX_META_ELF_ONLY    0
#define TRAX_META_IN_FLASH    1

#endif /* TRAX_CONFIG_OPTIONS_H_ */
