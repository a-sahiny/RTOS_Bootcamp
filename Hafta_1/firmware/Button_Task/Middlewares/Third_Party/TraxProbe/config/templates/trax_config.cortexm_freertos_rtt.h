/**
 ******************************************************************************
 * @file           : trax_config.h   (preset: FreeRTOS Cortex-M + SEGGER RTT)
 * @brief          : TraxProbe config for a FreeRTOS application
 ******************************************************************************
 * USE THIS WHEN
 * -------------
 *   - You run FreeRTOS. The library creates its own low-priority drain task,
 *     so you do NOT call trax_process() yourself.
 *   - SEGGER RTT transport over the debug probe.
 *   - Timestamp from the SysTick that also drives the FreeRTOS tick.
 *
 * HOW TO USE
 * ----------
 *   1. Copy to your include path as `trax_config.h`.
 *   2. Set the clock values (TRAX_CFG_TIMER_FREQ_HZ / TICK_COUNTER_PERIOD).
 *   3. Add the transports/RTT and os/FreeRTOS sources to the build.
 *   4. Wire FreeRTOS by including `trax.h` at the end of `FreeRTOSConfig.h`
 *      (see os/FreeRTOS/README.md). Do not add a `vApplicationTickHook` —
 *      TraxProbe owns it. Override `trax_app_tick_hook()` only if you need
 *      extra tick work. CubeMX: leave USE_TICK_HOOK unchecked.
 *   5. trax_init() once after the scheduler primitives are available.
 *
 * The drain task budget below follows the TraxProbe observability contract:
 * priority 1 (just above IDLE) never perturbs your real-time tasks. Raise it
 * only if your app has no idle slack (that competition becomes visible in the
 * trace). Stack is bumped to 512 words because the trace-start metadata
 * transmit path is the deepest call chain.
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

/* --- Transport -----------------------------------------------------------
 * No transport macro: the built-in SEGGER RTT transport is used by default
 * (register a custom transport before trax_init() to override it). */

/* --- RTOS ---------------------------------------------------------------- */
#define TRAX_CFG_RTOS_TYPE              TRAX_RTOS_FREERTOS
/* MANDATORY: kernel version of YOUR build — see tskKERNEL_VERSION_NUMBER in
 * FreeRTOS include/task.h. Supported range V10.2.0 .. V11.2.x (ST CubeMX
 * bundles V10.3.1). Drives version-specific trace hooks + START_TRACE
 * metadata. */
#define TRAX_CFG_FREERTOS_VERSION       TRAX_FREERTOS_VERSION(11, 2, 0)
#define TRAX_CFG_MAX_RTOS_TASKS         16
#define TRAX_CFG_MAX_RTOS_OBJECTS       16
#define TRAX_CFG_RTOS_TASK_NAME_MAX     16    /* literal, >= configMAX_TASK_NAME_LEN — trax_rtos_tables.c has no FreeRTOS.h */
#define TRAX_CFG_CTRL_TASK_PRIORITY     1     /* just above IDLE — see contract */
#define TRAX_CFG_CTRL_TASK_STACK_SIZE   512   /* 2 KB — covers trace-start path */
/* #define TRAX_CFG_CTRL_TASK_PERIOD_MS 10 */
/* #define TRAX_CFG_ISR_YIELD_TO_SCHEDULER 1 */  /* 1=omit ISR_EXIT on pending switch (default) */

/* --- Hard-real-time ISRs (optional, M3/M4/M7/M33+) ------------------------
 * By default TraxProbe's short O(1) critical sections use PRIMASK (mask
 * everything, a few dozen cycles). If you have ISRs above
 * configMAX_SYSCALL_INTERRUPT_PRIORITY that cannot tolerate even that,
 * align TraxProbe's mask with the kernel's — those ISRs are then NEVER
 * delayed by TraxProbe, and (same rule as FreeRTOS FromISR APIs) they
 * MUST NOT call any TRAX_* macro. */
/* #define TRAX_CFG_BASEPRI             configMAX_SYSCALL_INTERRUPT_PRIORITY */

/* --- Metadata / identification / buffers --------------------------------- */
#define TRAX_CFG_META_STORAGE           TRAX_META_ELF_ONLY
#define TRAX_CFG_PROJECT_NAME           "MyProject"
#define TRAX_CFG_LOG_COMPILE_LEVEL          TRAX_LOG_LEVEL_TRACE
#define TRAX_CFG_OUT_BUFFER_SIZE32      4096U   /* 16 KB — RTOS apps log more */
#define TRAX_CFG_IN_BUFFER_SIZE32       64U

/* --- Trace IDs (add your own) -------------------------------------------- */
enum { TID_LOG_APP = TRAX_TID_RANGE_LOG_USER_START };
enum { TID_VAR_COUNTER = TRAX_TID_RANGE_VAR_USER_START };

#endif /* APP_TRAX_CONFIG_H_ */
