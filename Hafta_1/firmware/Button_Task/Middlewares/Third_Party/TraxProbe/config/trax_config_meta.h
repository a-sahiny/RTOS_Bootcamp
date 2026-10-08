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
 * @file           : trax_config_meta.h
 * @brief          : TraxProbe Metadata Storage Configuration
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 *
 * This file selects how **static** metadata reaches the host. Both modes are
 * valid for any target; pick one in App/Config/trax_config.h.
 *
 *   TRAX_CFG_META_STORAGE = TRAX_META_ELF_ONLY
 *     - Static tables stay out of the START_TRACE payload (host reads the ELF).
 *     - On MCU: pair with an ELF-only / NOLOAD linker script so images stay small.
 *     - Slightly smaller probe code (no memcpy of static sections into START).
 *
 *   TRAX_CFG_META_STORAGE = TRAX_META_IN_FLASH
 *     - Static tables are **also** serialized in START_TRACE at session start.
 *     - Host can run without pointing at an ELF; Traxcope may still load ELF
 *       for refresh / rebuild workflows.
 *     - On MCU the sections are usually placed in flash — the name refers to
 *       “on device + on wire”, not “ELF vs no ELF”.
 *
 * You can switch between the two at project level; no other source changes are
 * required beyond trax_config.h and matching linker placement (flash vs NOLOAD).
 *
 ******************************************************************************
 */

#ifndef TRAX_CONFIG_META_H_
#define TRAX_CONFIG_META_H_

/*=============================================================================
 ====================METADATA STORAGE CONFIGURATION============================
 ============================================================================*/

/* Mode constants (TRAX_META_ELF_ONLY / TRAX_META_IN_FLASH): trax_config_options.h */

/**
 * @brief Static metadata: ELF-only on host vs also on the wire (START_TRACE)
 *
 * In trax_config.h:
 *   #define TRAX_CFG_META_STORAGE  TRAX_META_ELF_ONLY
 *   #define TRAX_CFG_META_STORAGE  TRAX_META_IN_FLASH
 *
 * Default: TRAX_META_IN_FLASH
 */
#ifndef TRAX_CFG_META_STORAGE
#define TRAX_CFG_META_STORAGE   TRAX_META_IN_FLASH
#endif

/*=============================================================================
 ====================METADATA STRING LENGTH CONFIGURATION======================
 ============================================================================*/

/**
 * @brief Maximum length for metadata string fields
 * 
 * These define the fixed size of inline string arrays in metadata structures.
 * Strings are stored directly in flash (not as pointers to .rodata), enabling
 * zero-copy transmission to the host.
 * 
 * Override in trax_config.h to tune for your application:
 *   - Smaller values: Less flash usage per metadata entry
 *   - Larger values: Support longer descriptive names
 * 
 * Example (in trax_config.h):
 *   #define TRAX_CFG_META_NAME_LEN    24   // Shorter names to save flash
 *   #define TRAX_CFG_META_TAG_LEN     16   // Shorter log filter tags
 *   #define TRAX_CFG_META_FORMAT_LEN  48   // Shorter format strings
 */

/** @brief Max length for signal/marker/ISR names (default: 32) */
#ifndef TRAX_CFG_META_NAME_LEN
#define TRAX_CFG_META_NAME_LEN      32
#endif

/** @brief Max length for log filter tags — shown in Context column in Traxcope (default: 32) */
#ifndef TRAX_CFG_META_TAG_LEN
#define TRAX_CFG_META_TAG_LEN       32
#endif

/** @brief Max length for signal unit strings (default: 8) */
#ifndef TRAX_CFG_META_UNIT_LEN
#define TRAX_CFG_META_UNIT_LEN      8
#endif

/** @brief Max length for log format strings (default: 64) */
#ifndef TRAX_CFG_META_FORMAT_LEN
#define TRAX_CFG_META_FORMAT_LEN    64
#endif

/** @brief Max length for stream description strings (default: 32) */
#ifndef TRAX_CFG_META_DESC_LEN
#define TRAX_CFG_META_DESC_LEN      32
#endif

/** @brief Max length for project name in build metadata (default: 32) */
#ifndef TRAX_CFG_META_PROJECT_LEN
#define TRAX_CFG_META_PROJECT_LEN   32
#endif

/** @brief Project name embedded in the START_TRACE header (default: "Unknown") */
#ifndef TRAX_CFG_PROJECT_NAME
#define TRAX_CFG_PROJECT_NAME       "Unknown"
#endif

/** @brief Max length for application build version (default: 16, e.g. "1.4.2-rc3") */
#ifndef TRAX_CFG_META_VERSION_LEN
#define TRAX_CFG_META_VERSION_LEN   16
#endif

/**
 * @brief Application build version string embedded in START_TRACE header.
 *
 * Human-readable semver-style label for the firmware build (e.g. "1.4.2",
 * "1.4.2-rc3", "2026.05-stable").  Free-form — host treats it as opaque
 * text.  When unset the host renders "(unversioned)" in the report.
 *
 * Override in trax_config.h:
 *   #define TRAX_CFG_BUILD_VERSION  "1.0.0"
 *
 * Default: empty string (unversioned).
 */
#ifndef TRAX_CFG_BUILD_VERSION
#define TRAX_CFG_BUILD_VERSION      ""
#endif

/** @brief Max length for application build identifier (default: 24).
 *  Sized to fit the unset-fallback `__DATE__ "T" __TIME__`
 *  ("May  7 2026T15:48:33" = 20 chars + NUL) with a few bytes of
 *  headroom for short prefixes like "ci-" or "v"; round up to 24
 *  for 4-byte alignment on the wire. */
#ifndef TRAX_CFG_META_BUILD_ID_LEN
#define TRAX_CFG_META_BUILD_ID_LEN  24
#endif

/**
 * @brief Application build identifier (a.k.a. build fingerprint).
 *
 * Vendor-neutral, transport-agnostic short string that uniquely identifies
 * the binary artefact.  Picked from whatever the project has:
 *
 *   git users     :  git rev-parse --short HEAD          ->  "a3f29b1"
 *   svn users     :  svn info --show-item revision        ->  "r4711"
 *   mercurial     :  hg id -i                             ->  "f8c2a91+"
 *   perforce      :  p4 changes -m1 -s submitted          ->  "@98412"
 *   ci-only       :  $CI_PIPELINE_ID                       ->  "ci-1234"
 *   no VCS at all :  date +%Y%m%d.%H%M                     ->  "20260507.1449"
 *   last resort   :  sha256(.elf) | head -c 8              ->  "9f3c7b21"
 *
 * Default falls back to __DATE__ "T" __TIME__ so every clean build still
 * gets a unique fingerprint with zero project-side ceremony.  Authors who
 * want stable ids across rebuilds (audit trail, change-log linking)
 * should override this from their build system.
 *
 * Override in trax_config.h or pass via -D from CMake:
 *   #define TRAX_CFG_BUILD_ID  "a3f29b1"
 *
 * Length capped at TRAX_CFG_META_BUILD_ID_LEN (default 24).
 */
#ifndef TRAX_CFG_BUILD_ID
#define TRAX_CFG_BUILD_ID           __DATE__ "T" __TIME__
#endif

/**
 * @brief Maximum filter order for var transfer-function metadata
 *
 * Bounds the per-filter coefficient arrays b[N+1] and a[N+1] in
 * trax_var_filter_meta_t.  Default 12 covers up to 12th-order IIR/FIR
 * (e.g. cascaded biquads, 13-tap FIR) at a wire cost of:
 *   2 + 1 + 1 + TRAX_CFG_META_NAME_LEN + 2*(N+1)*sizeof(float) bytes
 * = 4 + 32 + 104 = 140 bytes per filter at default settings.
 *
 * This is a WIRE CONTRACT, not a per-project knob.  The filter record
 * carries no length field, so the host strides by its own compile-time
 * record size on both the SESSION_START and the ELF path.  Overriding
 * this in an application trax_config.h without rebuilding Traxcope with
 * a matching FILTER_MAX_ORDER silently corrupts the filter section:
 * record 0 still decodes (var_id, name and b[] sit at order-independent
 * offsets) while every later record lands mid-struct and is dropped, so
 * only the first filter of each var reaches the UI.  Change it here and
 * in the host's MetaWireProtocol.h together, or leave it alone.
 */
#ifndef TRAX_CFG_FILTER_MAX_ORDER
#define TRAX_CFG_FILTER_MAX_ORDER   12
#endif

/*=============================================================================
 ====================LOG ARGUMENT CONFIGURATION================================
 ============================================================================*/

/**
 * @brief Maximum number of format arguments in a log message
 * 
 * Each log can associate up to this many format arguments with signal/channel IDs.
 * This enables the PC application to:
 *   - Link log arguments to signal metadata
 *   - Plot log data as signals
 *   - Provide rich tooltips showing signal names/units
 * 
 * Override in trax_config.h to tune for your application:
 *   #define TRAX_CFG_LOG_MAX_ARGS  4   // If logs have max 4 arguments
 * 
 * Flash cost: 2 bytes per slot (uint16_t channel ID)
 * Default: 24 arguments maximum
 */
#ifndef TRAX_CFG_LOG_MAX_ARGS
#define TRAX_CFG_LOG_MAX_ARGS       24
#endif

/*=============================================================================
 ====================VALIDATION================================================
 ============================================================================*/

#if (TRAX_CFG_META_STORAGE != TRAX_META_ELF_ONLY) && (TRAX_CFG_META_STORAGE != TRAX_META_IN_FLASH)
#error "TRAX_CFG_META_STORAGE must be TRAX_META_ELF_ONLY (0) or TRAX_META_IN_FLASH (1)"
#endif

/* Streams need their channel schema resident on the target: trax_stream_init()
 * walks .trax_var and .trax_stream_* to pre-compute each sequence_size_bytes.
 * ELF_ONLY places those tables at a synthetic VMA that never reaches the MCU,
 * so every sequence size would stay 0 and TRAX_STREAM_UPDATE would divide by
 * zero inside whatever ISR feeds the stream.  Caught here because the runtime
 * alternative is a check in that ISR on every block. */
#if (TRAX_CFG_STREAM_CNT > 0) && (TRAX_CFG_META_STORAGE == TRAX_META_ELF_ONLY)
#error "TRAX_CFG_STREAM_CNT > 0 requires TRAX_CFG_META_STORAGE == TRAX_META_IN_FLASH (streams resolve their channel schema from on-target meta sections)"
#endif

/* String length validation: must be multiples of 4 for word-aligned transmission */
#if (TRAX_CFG_META_NAME_LEN % 4) != 0
#error "TRAX_CFG_META_NAME_LEN must be a multiple of 4"
#endif

#if (TRAX_CFG_META_TAG_LEN % 4) != 0
#error "TRAX_CFG_META_TAG_LEN must be a multiple of 4"
#endif

#if (TRAX_CFG_META_UNIT_LEN % 4) != 0
#error "TRAX_CFG_META_UNIT_LEN must be a multiple of 4"
#endif

#if (TRAX_CFG_META_FORMAT_LEN % 4) != 0
#error "TRAX_CFG_META_FORMAT_LEN must be a multiple of 4"
#endif

#if (TRAX_CFG_META_DESC_LEN % 4) != 0
#error "TRAX_CFG_META_DESC_LEN must be a multiple of 4"
#endif

#if (TRAX_CFG_META_PROJECT_LEN % 4) != 0
#error "TRAX_CFG_META_PROJECT_LEN must be a multiple of 4"
#endif

#if (TRAX_CFG_META_VERSION_LEN % 4) != 0
#error "TRAX_CFG_META_VERSION_LEN must be a multiple of 4"
#endif

#if (TRAX_CFG_META_BUILD_ID_LEN % 4) != 0
#error "TRAX_CFG_META_BUILD_ID_LEN must be a multiple of 4"
#endif

/* Log argument count validation */
#if (TRAX_CFG_LOG_MAX_ARGS < 1) || (TRAX_CFG_LOG_MAX_ARGS > 24)
#error "TRAX_CFG_LOG_MAX_ARGS must be between 1 and 24"
#endif

#if (TRAX_CFG_LOG_MAX_ARGS % 2) != 0
#error "TRAX_CFG_LOG_MAX_ARGS must be even (for word alignment)"
#endif

#endif /* TRAX_CONFIG_META_H_ */
