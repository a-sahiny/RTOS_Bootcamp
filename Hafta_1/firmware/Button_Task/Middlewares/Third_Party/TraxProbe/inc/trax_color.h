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
 * @file           : trax_color.h
 * @brief          : TraxProbe Color Definitions
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 *
 * Color definitions for TraxProbe metadata (VAR, marker, state machine).
 * Colors are packed as 0xFFRRGGBB (with validity marker in high byte).
 * Use TRAX_COLOR_AUTO for Scope to auto-assign from palette.
 *
 ******************************************************************************
 */

#ifndef TRAX_COLOR_H_
#define TRAX_COLOR_H_

#include <stdint.h>

/*=============================================================================
 ====================RGB CONSTRUCTION==========================================
 ============================================================================*/

/**
 * @brief Construct an RGB color value for metadata macros
 *
 * Packs R, G, B (0-255 each) into a uint32_t with a validity marker
 * in the high byte. Use TRAX_COLOR_AUTO for auto-assigned colors.
 *
 * Usage:
 *   TRAX_COLOR_RGB(255, 0, 0)      // Red
 *   TRAX_COLOR_RGB(0, 255, 0)      // Green
 *   TRAX_COLOR_RGB(128, 128, 255)  // Light blue
 */
#define TRAX_COLOR_RGB(_r, _g, _b) \
    (0xFF000000U | ((uint32_t)(_r) << 16) | ((uint32_t)(_g) << 8) | (uint32_t)(_b))

/** @brief Auto-assign color from the host palette */
#define TRAX_COLOR_AUTO         0x00000000U
/** @brief Alias of TRAX_COLOR_AUTO (kept for existing firmware) */
#define TRAX_COLOR_NONE         TRAX_COLOR_AUTO

/*=============================================================================
 ====================PREDEFINED COLORS=========================================
 ============================================================================*/

/** @name Predefined scope colors */
/** @{ */
#define TRAX_COLOR_RED          TRAX_COLOR_RGB(255,   0,   0)
#define TRAX_COLOR_GREEN        TRAX_COLOR_RGB(  0, 255,   0)
#define TRAX_COLOR_BLUE         TRAX_COLOR_RGB(  0,   0, 255)
#define TRAX_COLOR_YELLOW       TRAX_COLOR_RGB(255, 255,   0)
#define TRAX_COLOR_CYAN         TRAX_COLOR_RGB(  0, 255, 255)
#define TRAX_COLOR_MAGENTA      TRAX_COLOR_RGB(255,   0, 255)
#define TRAX_COLOR_ORANGE       TRAX_COLOR_RGB(255, 128,   0)
#define TRAX_COLOR_WHITE        TRAX_COLOR_RGB(255, 255, 255)
#define TRAX_COLOR_LIME         TRAX_COLOR_RGB(128, 255,   0)
#define TRAX_COLOR_PINK         TRAX_COLOR_RGB(255, 128, 128)
#define TRAX_COLOR_TEAL         TRAX_COLOR_RGB(  0, 200, 200)
#define TRAX_COLOR_PURPLE       TRAX_COLOR_RGB(160,  32, 240)
#define TRAX_COLOR_GOLD         TRAX_COLOR_RGB(255, 215,   0)
#define TRAX_COLOR_CORAL        TRAX_COLOR_RGB(255, 127,  80)
#define TRAX_COLOR_SKY_BLUE     TRAX_COLOR_RGB(135, 206, 235)
/** @} */

/*=============================================================================
 ====================INTERNAL HELPERS==========================================
 ============================================================================*/

/* Internal helper — strip validity marker, keep 0x00RRGGBB */
#define TRAX_COLOR_TO_RGB_(_c)    ((_c) & 0x00FFFFFFU)

#endif /* TRAX_COLOR_H_ */
