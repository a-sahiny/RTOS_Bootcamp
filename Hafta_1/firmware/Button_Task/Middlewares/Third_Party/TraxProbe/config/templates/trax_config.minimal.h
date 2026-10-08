/**
 ******************************************************************************
 * @file           : trax_config.h   (preset: MINIMAL — required macros only)
 * @brief          : Smallest valid TraxProbe config; everything else defaults
 ******************************************************************************
 * This is the smallest config that compiles. It sets ONLY the macros the
 * library has no default for, and deliberately overrides nothing else — so
 * you inherit every library default. Copy to your include path as
 * `trax_config.h`, point the device header at your MCU, and edit the three
 * clock-related values.
 *
 * Want to change a default? See `trax_config.reference.h` in this folder — it
 * lists every user-settable macro with its default value.
 *
 * DEFAULTS YOU INHERIT BY NOT SETTING THEM HERE:
 *   Transport                    = built-in SEGGER RTT (runtime fallback;
 *                                  register a custom transport to override)
 *   TRAX_CFG_RTOS_TYPE           = TRAX_RTOS_NONE            (bare metal)
 *   TRAX_CFG_TIMESTAMP_MODE      = TRAX_TIMESTAMP_TICK_TIMER
 *   TRAX_CFG_TIMESTAMP_TIMER_DIR = TRAX_TIMER_DIR_DOWN       (SysTick counts down)
 *   TRAX_CFG_TICK_BITS           = 8
 *   TRAX_CFG_META_STORAGE        = TRAX_META_IN_FLASH
 *   TRAX_CFG_LOG_COMPILE_LEVEL       = TRAX_LOG_LEVEL_TRACE
 *   TRAX_CFG_OUT_BUFFER_SIZE32   = 2048  (8 KB)
 *   TRAX_CFG_STREAM_CNT             = 0     (no high-rate streams)
 *   TRAX_CFG_BASEPRI (unset)     = PRIMASK critical sections (all IRQs
 *                                  masked for short O(1) windows). Define
 *                                  it on M3/M4/M7/M33+ to exempt hard-RT
 *                                  ISRs — see trax_config.reference.h.
 *                                  (No BASEPRI on M0/M0+/M23 like the G0
 *                                  below — keep the default there.)
 ******************************************************************************
 */

#ifndef APP_TRAX_CONFIG_H_
#define APP_TRAX_CONFIG_H_

/* MCU CMSIS header — provides `SysTick`, referenced by the timer macro below.
 * Replace with your device family header (e.g. stm32f4xx.h, stm32h7xx.h,
 * NuMicro.h, ...). */
#include "stm32g0xx.h"

/* REQUIRED — hardware port (the library has no default for this). */
#define TRAX_CFG_HW_PORT                TRAX_HW_PORT_ARM_CORTEX_M

/* REQUIRED in the default tick-timer mode — the fine-time register and the
 * clock that drives it. Values shown are for a 64 MHz core with a 1 ms
 * SysTick; edit all three for your clock:
 *   TRAX_CFG_TIMER_FREQ_HZ       = core/timer clock in Hz
 *   TRAX_CFG_TICK_COUNTER_PERIOD = SysTick->LOAD + 1 (cycles per tick) */
#define TRAX_CFG_TIMESTAMP_TIMER_VAL    (SysTick->VAL)
#define TRAX_CFG_TICK_COUNTER_PERIOD    64000U
#define TRAX_CFG_TIMER_FREQ_HZ          64000000U

#endif /* APP_TRAX_CONFIG_H_ */
