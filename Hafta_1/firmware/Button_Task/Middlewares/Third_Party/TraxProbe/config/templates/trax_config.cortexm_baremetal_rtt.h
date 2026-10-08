/**
 ******************************************************************************
 * @file           : trax_config.h   (preset: bare-metal Cortex-M + SEGGER RTT)
 * @brief          : Minimal working TraxProbe config for a bare-metal MCU
 ******************************************************************************
 * USE THIS WHEN
 * -------------
 *   - No RTOS (you call trax_process() in your main loop).
 *   - SEGGER RTT transport over the debug probe (J-Link / ST-Link via RTT).
 *   - Timestamp from SysTick (the OS/HAL tick).
 *
 * HOW TO USE
 * ----------
 *   1. Copy to your include path as `trax_config.h` (e.g. Core/Inc/trax_config.h).
 *   2. Set TRAX_CFG_TIMER_FREQ_HZ and TRAX_CFG_TICK_COUNTER_PERIOD to match
 *      YOUR clock (values below are for a 64 MHz core with a 1 ms SysTick).
 *   3. Add the transports/RTT sources to the build.
 *   4. In your code:
 *         trax_init();
 *         ... in SysTick_Handler():  trax_timestamp_tick();
 *         ... in main loop:          trax_process();
 *
 * WORKED EXAMPLE — STM32G0B0RE @ 64 MHz (HSI16 -> PLL x8 / R2):
 *   SYSCLK = 16 MHz / 1 * 8 / 2 = 64 MHz, SysTick reload for 1 ms = 64000.
 *   -> TRAX_CFG_TIMER_FREQ_HZ      = 64000000U
 *   -> TRAX_CFG_TICK_COUNTER_PERIOD = 64000U     (= SysTick->LOAD + 1)
 ******************************************************************************
 */

#ifndef APP_TRAX_CONFIG_H_
#define APP_TRAX_CONFIG_H_

#define TRAX_CFG_STREAM_CNT                0
#include "trax_tid.h"

/* --- Hardware port ------------------------------------------------------- */
#define TRAX_CFG_HW_PORT                TRAX_HW_PORT_ARM_CORTEX_M

/* --- Timestamp: SysTick (counts down) ------------------------------------ */
#define TRAX_CFG_TIMESTAMP_MODE         TRAX_TIMESTAMP_TICK_TIMER
#define TRAX_CFG_TIMESTAMP_TIMER_VAL    (SysTick->VAL)
#define TRAX_CFG_TIMESTAMP_TIMER_DIR    TRAX_TIMER_DIR_DOWN
#define TRAX_CFG_TICK_COUNTER_PERIOD    64000U        /* <-- edit for your clock */
#define TRAX_CFG_TIMER_FREQ_HZ          64000000U     /* <-- edit for your clock */

/* --- Transport / RTOS / metadata -----------------------------------------
 * No transport macro: the built-in SEGGER RTT transport is the default. */
#define TRAX_CFG_RTOS_TYPE              TRAX_RTOS_NONE
#define TRAX_CFG_META_STORAGE           TRAX_META_ELF_ONLY  /* host reads .elf over RTT */

/* --- Hard-real-time ISRs (optional, M3/M4/M7/M33+ ONLY) -------------------
 * TraxProbe's short O(1) critical sections use PRIMASK by default (all
 * IRQs masked for a few dozen cycles). On cores WITH the BASEPRI register
 * you can exempt your most urgent ISRs by masking only priorities >= a
 * threshold — exempted ISRs are never delayed but MUST NOT call TRAX_*
 * macros. NOT available on Cortex-M0/M0+/M23 (e.g. the STM32G0 in the
 * worked example above) — those cores must keep the PRIMASK default. */
/* #define TRAX_CFG_BASEPRI             (2U << (8U - __NVIC_PRIO_BITS)) */

/* --- Identification / buffers -------------------------------------------- */
#define TRAX_CFG_PROJECT_NAME           "Traxcope_Hello"
#define TRAX_CFG_LOG_COMPILE_LEVEL          TRAX_LOG_LEVEL_TRACE
#define TRAX_CFG_OUT_BUFFER_SIZE32      2048U   /* 8 KB */
#define TRAX_CFG_IN_BUFFER_SIZE32       64U

/* --- Trace IDs (add your own) -------------------------------------------- */
enum { TID_LOG_HELLO = TRAX_TID_RANGE_LOG_USER_START };
enum { TID_VAR_COUNTER = TRAX_TID_RANGE_VAR_USER_START };

#endif /* APP_TRAX_CONFIG_H_ */
