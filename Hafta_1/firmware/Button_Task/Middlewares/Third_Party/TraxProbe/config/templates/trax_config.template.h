/**
 ******************************************************************************
 * @file           : trax_config.h   (canonical template — copy & fill in)
 * @brief          : Application-specific TraxProbe configuration TEMPLATE
 ******************************************************************************
 * HOW TO USE THIS FILE
 * --------------------
 *   1. Copy this file into your application include path and rename it to
 *      exactly `trax_config.h` (e.g. STM32CubeIDE: Core/Inc/trax_config.h).
 *   2. Make sure that folder is on the compiler include path (it already is
 *      for Core/Inc in a CubeMX project).
 *   3. Fill in every line tagged `TODO`. The library will refuse to build
 *      with a clear #error until the REQUIRED values are set.
 *   4. Delete the options you do not need — every OPTIONAL knob below already
 *      has a sane default inside the library.
 *
 * If you just want something that compiles for a common setup, copy one of
 * the ready-made presets in this folder instead:
 *     trax_config.cortexm_baremetal_rtt.h   bare-metal Cortex-M + SEGGER RTT
 *     trax_config.cortexm_freertos_rtt.h    FreeRTOS Cortex-M + SEGGER RTT
 *     trax_config.cortexm_uart_custom.h     Cortex-M + your own UART transport
 *
 * The named constants used below (TRAX_HW_PORT_*, TRAX_TRANSPORT_*,
 * TRAX_RTOS_*, TRAX_TIMESTAMP_*, TRAX_TIMER_DIR_*, TRAX_META_*, TRAX_LOG_*)
 * are all declared in config/trax_config_options.h, which the library
 * includes before this file — so IntelliSense resolves them here.
 ******************************************************************************
 */

#ifndef APP_TRAX_CONFIG_H_
#define APP_TRAX_CONFIG_H_

/*=============================================================================
 * 0. STREAM COUNT (must be a plain integer literal, declared FIRST)
 *=============================================================================
 * TRAX_CFG_STREAM_CNT sizes the high-rate var-stream tables and GATES the
 * TRAX_STREAM_* macros. It MUST be a literal (the preprocessor cannot
 * evaluate enum constants in #if). Set to 0 if you use no streams.
 */
#define TRAX_CFG_STREAM_CNT                0

#include "trax_tid.h"   /* TID range anchors (TRAX_TID_RANGE_*_USER_START) */

/*=============================================================================
 * 1. HARDWARE PORT                                              (REQUIRED)
 *=============================================================================
 * Selects the porting layer that supplies critical sections, the core-id,
 * and the timestamp macros.
 *   TRAX_HW_PORT_ARM_CORTEX_M   all Cortex-M (M0..M85); SysTick everywhere, DWT FREERUN on M3+
 *   TRAX_HW_PORT_ZYNQ           Xilinx Zynq-7000 (A9) / ZynqMP (A53/R5)
 *   TRAX_HW_PORT_GENERIC        bring-your-own primitives
 */
#define TRAX_CFG_HW_PORT                TRAX_HW_PORT_ARM_CORTEX_M   /* TODO */

/* OPTIONAL — hard-real-time ISRs (Cortex-M3/M4/M7/M33+ only).
 * TraxProbe's critical sections are short and O(1) (a few dozen cycles)
 * but by default use PRIMASK, which masks ALL interrupts. Define
 * TRAX_CFG_BASEPRI to mask only priorities >= the value instead: more
 * urgent ISRs are NEVER delayed by TraxProbe — and MUST NOT call any
 * TRAX_* macro (same contract as FreeRTOS FromISR APIs). Raw shifted
 * 8-bit BASEPRI value; on FreeRTOS use the kernel's own mask so the two
 * contracts coincide. Not available on Cortex-M0/M0+/M23. */
/* #define TRAX_CFG_BASEPRI             configMAX_SYSCALL_INTERRUPT_PRIORITY */

/*=============================================================================
 * 2. TIMESTAMP SOURCE                                           (REQUIRED)
 *=============================================================================
 * Two modes:
 *
 *   TRAX_TIMESTAMP_TICK_TIMER (default) — software tick (coarse) + a timer
 *       register (fine). Works on every MCU. Call trax_timestamp_tick() from
 *       your periodic ISR (e.g. SysTick_Handler).
 *       Requires: TIMER_VAL, TIMER_DIR, TICK_COUNTER_PERIOD, TIMER_FREQ_HZ.
 *
 *   TRAX_TIMESTAMP_FREERUN — a 32-bit free-running counter (e.g. DWT->CYCCNT
 *       on M3/M4/M7/M33). Highest resolution. Call trax_timestamp_poll()
 *       from your main loop (or trax_timestamp_tick() from an ISR) to track
 *       wraps. Requires only TIMER_FREQ_HZ (period & direction auto-derived).
 */
#define TRAX_CFG_TIMESTAMP_MODE         TRAX_TIMESTAMP_TICK_TIMER

#if (TRAX_CFG_TIMESTAMP_MODE == TRAX_TIMESTAMP_TICK_TIMER)
    /* Fine-time register read expression. SysTick counts DOWN. */
    #define TRAX_CFG_TIMESTAMP_TIMER_VAL    (SysTick->VAL)            /* TODO */
    #define TRAX_CFG_TIMESTAMP_TIMER_DIR    TRAX_TIMER_DIR_DOWN
    /* Fine cycles per tick increment = SysTick->LOAD + 1 (= cycles per 1 ms). */
    #define TRAX_CFG_TICK_COUNTER_PERIOD    64000U                   /* TODO */
#endif

/* Frequency of the timer above, in Hz. Match your real core/timer clock. */
#define TRAX_CFG_TIMER_FREQ_HZ          64000000U                    /* TODO */
/* #define TRAX_CFG_TIMER_FREQ_DIV      1U */  /* for fractional clocks */

/* Upper bits of the 32-bit timestamp word used as the coarse tick.
 * 8 → [tick:8][fine:24] (good for SysTick's 24-bit reload). Default 8. */
/* #define TRAX_CFG_TICK_BITS           8 */

/*=============================================================================
 * 3. TRANSPORT                                             (COMPILE TIME)
 *=============================================================================
 *   Selected by TRAX_CFG_TRANSPORT (default: TRAX_TRANSPORT_RTT).
 *
 *   Default:  the built-in SEGGER RTT transport. Add transports/RTT to the
 *             build. No user code required.
 *   Custom:   set TRAX_CFG_TRANSPORT = TRAX_TRANSPORT_CUSTOM and define the
 *             three required trax_transport_*() functions in your app — init,
 *             write, read (see trax_transport.h). Bound at link time — no
 *             struct, no register call; any undefined function fails the link
 *             by name. Free-space reporting, TX capacity, occupancy and
 *             session-restart discard are optional TRAX_CFG_TRANSPORT_* knobs.
 */

/*=============================================================================
 * 4. RTOS                                                       (OPTIONAL)
 *=============================================================================
 *   TRAX_RTOS_NONE      bare metal — you call trax_process() in your loop (default)
 *   TRAX_RTOS_FREERTOS  task integration; the library runs its own drain task
 */
#define TRAX_CFG_RTOS_TYPE             TRAX_RTOS_NONE
/* MANDATORY with TRAX_RTOS_FREERTOS — kernel version of your build (see
 * tskKERNEL_VERSION_NUMBER in FreeRTOS include/task.h; supported range
 * V10.2.0 .. V11.2.x):                                                        */
/* #define TRAX_CFG_FREERTOS_VERSION     TRAX_FREERTOS_VERSION(11, 2, 0) */
/* FreeRTOS-only drain-task budget (see config/trax_config_rtos.h):           */
/* #define TRAX_CFG_CTRL_TASK_PRIORITY   1   */
/* #define TRAX_CFG_CTRL_TASK_STACK_SIZE 512 */
/* #define TRAX_CFG_CTRL_TASK_PERIOD_MS  10  */
/* #define TRAX_CFG_RTOS_TASK_NAME_MAX   configMAX_TASK_NAME_LEN */
/* #define TRAX_CFG_ISR_YIELD_TO_SCHEDULER  1 */  /* 1=omit ISR_EXIT when switch pending (default); 0=literal Cortex-M resume */

/*=============================================================================
 * 5. METADATA DELIVERY                                          (OPTIONAL)
 *=============================================================================
 *   TRAX_META_ELF_ONLY   host reads signal/log schema from the .elf (RTT debug).
 *   TRAX_META_IN_FLASH   probe also sends schema on the wire at session start
 *                        (deploy without an .elf, e.g. UART/TCP). (default)
 */
#define TRAX_CFG_META_STORAGE          TRAX_META_IN_FLASH

/*=============================================================================
 * 6. LIBRARY OVERRIDES                                          (OPTIONAL)
 *=============================================================================*/
#define TRAX_CFG_PROJECT_NAME          "MyProject"
/* #define TRAX_CFG_BUILD_VERSION      "1.0.0" */
#define TRAX_CFG_LOG_COMPILE_LEVEL         TRAX_LOG_LEVEL_TRACE   /* WARNING in prod */
#define TRAX_CFG_OUT_BUFFER_SIZE32     2048U   /* 8 KB ring buffer */
#define TRAX_CFG_IN_BUFFER_SIZE32      64U     /* host->device commands */

/*=============================================================================
 * 7. TRACE IDs                                                  (OPTIONAL)
 *=============================================================================
 * Add your own IDs here. Values are auto-assigned by the enum — duplicates
 * are impossible. Anchor each enum at the matching *_USER_START constant.
 */
enum { /* LOG  0x0100-0x1FFF */
    TID_LOG_HELLO = TRAX_TID_RANGE_LOG_USER_START,
};

enum { /* VAR  0x2000-0x2FFF (plotted scalars) */
    TID_VAR_COUNTER = TRAX_TID_RANGE_VAR_USER_START,
};

/* enum { TID_ISR_TIM2 = TRAX_TID_RANGE_ISR_USER_START };       ISR 0x4000 */
/* enum { TID_MARKER_LOOP = TRAX_TID_RANGE_MARKER_USER_START };  MARKER 0x5000 */
/* enum { TID_SM_SYSTEM = TRAX_TID_RANGE_SM_USER_START };        SM 0x7000 */

#endif /* APP_TRAX_CONFIG_H_ */
