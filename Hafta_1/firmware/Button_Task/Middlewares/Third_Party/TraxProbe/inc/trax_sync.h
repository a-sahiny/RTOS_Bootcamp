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
 * @file           : trax_sync.h
 * @brief          : Multi-probe timestamp synchronization (peer-to-peer).
 * @version        : 5.0.0
 ******************************************************************************
 * @attention
 *
 * Peer-to-peer timestamp synchronization across multiple probes — driven
 * entirely by firmware. The host (Traxcope) is a passive observer:
 * every probe emits TRAX_TID_SYNC_TIMESTAMP frames whenever the shared sync
 * line edges, and the host uses the first / last sync events of the
 * session to align probe-local time axes for display.
 *
 * Hardware setup:
 *   - All probes:    GPIO open-drain output + EXTI on falling edge
 *   - Single wire connecting all probes with external pull-up (e.g. 10 kΩ)
 *   - Exactly one probe (the MASTER) periodically pulls the line LOW;
 *     everyone (including the master) detects the falling edge via EXTI.
 *
 * User responsibilities (zero library involvement in GPIO):
 *
 *   MASTER probe
 *   ────────────
 *   1. Configure the GPIO as open-drain output, release HIGH at startup.
 *   2. Enable the falling-edge EXTI interrupt on that pin.
 *   3. In your EXTI ISR call trax_sync_triggered() as early as possible.
 *   4. Define TRAX_CFG_SYNC_GPIO_SET_LOW() and TRAX_CFG_SYNC_GPIO_SET_HIGH()
 *      in trax_config.h (see below).
 *   5. At each session boundary (start / before stop), or from a slow
 *      periodic context, drive one pulse:
 *
 *          trax_sync_trigger_start();
 *          // hold the line LOW long enough for every slave EXTI / input
 *          // filter to latch — busy-wait, HAL_Delay, vTaskDelay, ...
 *          trax_sync_trigger_end();
 *
 *   SLAVE probe
 *   ───────────
 *   1. Configure the GPIO as open-drain output (released HIGH), same pin.
 *   2. Enable the falling-edge EXTI interrupt on that pin.
 *   3. In your EXTI ISR call trax_sync_triggered() as early as possible.
 *      (Nothing else — TRAX_CFG_SYNC_GPIO_SET_* are not used on slaves.)
 *
 *   NONE (single-probe build)
 *   ─────────────────────────
 *   No action needed. trax_sync_trigger_start() / trax_sync_trigger_end()
 *   are safe no-ops.
 *
 * GPIO macros — define in trax_config.h (MASTER only):
 *
 *   #define TRAX_CFG_SYNC_GPIO_SET_LOW()   my_gpio_sync_low()
 *   #define TRAX_CFG_SYNC_GPIO_SET_HIGH()  my_gpio_sync_high()
 *
 *   Defaults to empty no-ops so slave/none builds compile without changes.
 *   Both macros must be ISR-safe (called inside a critical section).
 *
 * Wire frame:
 *   TRAX_TID_SYNC_TIMESTAMP, payload = [raw_timestamp:4][tick_overflow_cntr:4]
 *
 ******************************************************************************
 */

#ifndef TRAX_SYNC_H_
#define TRAX_SYNC_H_

#include <stdint.h>
#include "trax_config_default.h"  /* pulls in trax_config.h for user macros */

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 * Sync role (compile-time)
 *============================================================================*/

#define TRAX_SYNC_ROLE_NONE    0u
#define TRAX_SYNC_ROLE_MASTER  1u
#define TRAX_SYNC_ROLE_SLAVE   2u

/**
 * @brief Compile-time sync role for this firmware build.
 *
 * Override in trax_config.h, e.g.:
 *   #define TRAX_CFG_SYNC_ROLE  TRAX_SYNC_ROLE_MASTER
 *
 * Default is NONE so existing single-probe builds keep working without
 * touching their config. The role is emitted on the wire inside the
 * SESSION_START header; the host uses it to auto-designate the
 * reference probe (no UI step required).
 */
#ifndef TRAX_CFG_SYNC_ROLE
#define TRAX_CFG_SYNC_ROLE  TRAX_SYNC_ROLE_NONE
#endif

/**
 * @brief Return the compile-time sync role for this build.
 *
 * Constant — folded by the compiler. Used by trax_meta_tx_send_start()
 * to populate the SESSION_START header.
 */
static inline uint8_t trax_sync_get_role(void)
{
    return (uint8_t)TRAX_CFG_SYNC_ROLE;
}

/*=============================================================================
 * GPIO macros (MASTER only — define in trax_config.h)
 *============================================================================*/

/**
 * @brief Pull the shared sync GPIO line LOW (open-drain assert).
 *
 * Called from inside trax_sync_trigger_start() within a critical section.
 * Must be ISR-safe and as short as possible.
 *
 * Example (trax_config.h):
 *   #define TRAX_CFG_SYNC_GPIO_SET_LOW()   my_gpio_set_low(MY_SYNC_PIN)
 */
#ifndef TRAX_CFG_SYNC_GPIO_SET_LOW
#define TRAX_CFG_SYNC_GPIO_SET_LOW()    do { } while (0)
#endif

/**
 * @brief Release the shared sync GPIO line (open-drain deassert → HIGH via pull-up).
 *
 * Called from trax_sync_trigger_end().
 * Must be ISR-safe.
 *
 * Example (trax_config.h):
 *   #define TRAX_CFG_SYNC_GPIO_SET_HIGH()  my_gpio_set_high(MY_SYNC_PIN)
 */
#ifndef TRAX_CFG_SYNC_GPIO_SET_HIGH
#define TRAX_CFG_SYNC_GPIO_SET_HIGH()   do { } while (0)
#endif

/*=============================================================================
 * API
 *============================================================================*/

/**
 * @brief Sync edge captured — emit TRAX_TID_SYNC_TIMESTAMP frame.
 *
 * Call from the GPIO EXTI ISR as early as possible. Captures the
 * current timestamp and emits a TRAX_TID_SYNC_TIMESTAMP frame on this
 * probe's transport.
 *
 * Frame payload: [raw_timestamp:4] [tick_overflow_cntr:4]
 *
 * ISR-safe. Call on BOTH master and slave builds from the EXTI handler.
 */
#if TRAX_ENABLE
void trax_sync_triggered(void);
#else
static inline void trax_sync_triggered(void) { }
#endif

/**
 * @brief Begin a sync pulse — assert the line LOW and emit the sync frame.
 *        (MASTER only.)
 *
 * Enters a critical section, calls TRAX_CFG_SYNC_GPIO_SET_LOW() to pull
 * the shared bus LOW, captures the master's own timestamp via
 * trax_sync_triggered(), then exits the critical section. The two
 * operations MUST be atomic w.r.t. other ISRs so the timestamp the
 * master logs matches the falling edge the slaves see.
 *
 * After this returns the line is held LOW. The caller is responsible
 * for keeping it LOW long enough for every slave to detect the falling
 * edge (and clear any EXTI / bus input-filter latency), then calling
 * trax_sync_trigger_end() to release it.
 *
 * Recommended call pattern:
 *
 *   trax_sync_trigger_start();
 *   // hold LOW — pick whatever fits your bus / filter / RTOS:
 *   //   bare-metal:   for (volatile uint32_t d = 1000; d--; ) { }
 *   //   FreeRTOS:     vTaskDelay(pdMS_TO_TICKS(1));
 *   //   HAL:          HAL_Delay(1);
 *   trax_sync_trigger_end();
 *
 * Typical call sites (MASTER):
 *   - Once early after stream start (provides "first" sync anchor)
 *   - Periodically (e.g. 1 Hz) or before stream stop (provides "last" anchor)
 *
 * Safe no-op on SLAVE / NONE builds (compile-time guard).
 * Safe no-op when the GPIO macros are not defined.
 */
#if TRAX_ENABLE
void trax_sync_trigger_start(void);
#else
static inline void trax_sync_trigger_start(void) { }
#endif

/**
 * @brief End a sync pulse — release the line HIGH. (MASTER only.)
 *
 * Calls TRAX_CFG_SYNC_GPIO_SET_HIGH() so the open-drain bus floats back
 * to its idle HIGH level via the external pull-up. Pairs 1:1 with a
 * preceding trax_sync_trigger_start().
 *
 * No critical section is needed here — the timestamp has already been
 * captured by trax_sync_trigger_start() and the rising edge is not a
 * sync event.
 *
 * Safe no-op on SLAVE / NONE builds (compile-time guard).
 * Safe no-op when the GPIO macros are not defined.
 */
#if TRAX_ENABLE
void trax_sync_trigger_end(void);
#else
static inline void trax_sync_trigger_end(void) { }
#endif

#ifdef __cplusplus
}
#endif

#endif /* TRAX_SYNC_H_ */
