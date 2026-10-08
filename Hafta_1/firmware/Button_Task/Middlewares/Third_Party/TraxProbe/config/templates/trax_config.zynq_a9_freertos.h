/**
 ******************************************************************************
 * @file           : trax_config.h   (preset: Zynq-7000 Cortex-A9 + FreeRTOS)
 * @brief          : TraxProbe config for a Xilinx Zynq-7000 FreeRTOS app
 ******************************************************************************
 * USE THIS WHEN
 * -------------
 *   - Zynq-7000 (Cortex-A9), Xilinx FreeRTOS BSP, built in Vitis.
 *   - Timestamp from the MPCore global timer (free-running, shared by both
 *     A9s, so AMP images land on one coherent host timeline).
 *   - A custom transport (UART / TCP / PL). RTT is possible but assumes a
 *     J-Link rather than the board's platform-cable JTAG.
 *
 * HOW TO USE
 * ----------
 *   1. Copy to your include path as `trax_config.h`.
 *   2. Set TRAX_CFG_TIMER_FREQ_HZ = CPU clock / 2 (see below).
 *   3. Confirm the interrupt-nesting symbol name against your BSP's port.c
 *      (step 4 below) — this one is not optional, see hw_port/Zynq/README.md.
 *   4. Add to the build: the src, debug, os/common and os/FreeRTOS sources,
 *      plus your transport .c file.
 *   5. Alias the linker regions in lscript.ld and add trax_probe.ld to the
 *      link line (see hw_port/Zynq/README.md, "Linker script").
 *   6. Wire FreeRTOS by including `trax.h` at the end of FreeRTOSConfig.h.
 *   7. trax_init() once after the scheduler primitives are available.
 ******************************************************************************
 */

#ifndef APP_TRAX_CONFIG_H_
#define APP_TRAX_CONFIG_H_

#define TRAX_CFG_STREAM_CNT                0
#include "trax_tid.h"

/* --- Hardware port ------------------------------------------------------- */
#define TRAX_CFG_HW_PORT                TRAX_HW_PORT_ZYNQ

/* --- Timestamp: MPCore global timer, free-running, counts UP -------------
 * The Xilinx standalone BSP already enables the global timer (XTime_GetTime
 * reads it). It runs at CPU_3x2x = half the CPU clock, so a 666.66 MHz A9
 * gives 333333333U. TICK_COUNTER_PERIOD must NOT be set in FREERUN mode —
 * it is derived from TICK_BITS. */
#define TRAX_CFG_TIMESTAMP_MODE         TRAX_TIMESTAMP_FREERUN
#define TRAX_CFG_TIMESTAMP_TIMER_DIR    TRAX_TIMER_DIR_UP
#define TRAX_CFG_TIMER_FREQ_HZ          333333333U    /* <-- CPU clock / 2 */

/* --- ISR-context detection (REQUIRED on Cortex-A/R with FreeRTOS) --------
 * Cortex-A/R has no IPSR, and the Cortex-A/R FreeRTOS ports do not call
 * traceISR_ENTER(), so this predicate is the ONLY thing that advances the
 * trace timebase. Borrow the counter the kernel port already maintains.
 *
 * COPY THE DEFINITION LINE FROM YOUR BSP's port.c — type and qualifiers
 * must match exactly, because port.c sees this declaration through
 * FreeRTOSConfig.h and a mismatch is a "conflicting type qualifiers" error.
 * freertos10_xilinx v1.18 (Vitis 2026.1) defines:
 *   ARM_CA9  : volatile uint32_t ulPortInterruptNesting   (this preset)
 *   ARM_CR5  : uint32_t          ulPortInterruptNesting   (no volatile!)
 *   ARM_CA53 : uint64_t          ullPortInterruptNesting
 * Leave the pair out and the build fails with an explanatory #error rather
 * than producing a frozen timeline. */
extern volatile uint32_t ulPortInterruptNesting;
#define TRAX_CFG_IRQ_NESTING_COUNTER    ulPortInterruptNesting

/* --- Interrupt masking ---------------------------------------------------
 * Default 1 = critical sections mask IRQ *and* FIQ. Set to 0 only if FIQ is
 * reserved for hard-real-time work that never calls a TRAX_* macro. */
/* #define TRAX_CFG_ZYNQ_MASK_FIQ       0 */

/* --- Transport -----------------------------------------------------------
 * Implement the four trax_transport_*() functions (see inc/trax_transport.h)
 * over XUartPs, lwIP, or a PL FIFO. */
#define TRAX_CFG_TRANSPORT              TRAX_TRANSPORT_CUSTOM

/* --- RTOS ---------------------------------------------------------------- */
#define TRAX_CFG_RTOS_TYPE              TRAX_RTOS_FREERTOS
/* MANDATORY: the kernel version of YOUR build — see tskKERNEL_VERSION_NUMBER
 * in the BSP's FreeRTOS include/task.h. Vitis 2023.x ships V10.6.x. */
#define TRAX_CFG_FREERTOS_VERSION       TRAX_FREERTOS_VERSION(10, 6, 1)
#define TRAX_CFG_MAX_RTOS_TASKS         16
#define TRAX_CFG_MAX_RTOS_OBJECTS       16
/* A literal, not configMAX_TASK_NAME_LEN: os/common/trax_rtos_tables.c sizes
 * its name field from this and does not include FreeRTOS.h, so the FreeRTOS
 * macro is undeclared there. Keep it >= configMAX_TASK_NAME_LEN (Xilinx BSP
 * default 10). */
#define TRAX_CFG_RTOS_TASK_NAME_MAX     16
#define TRAX_CFG_CTRL_TASK_PRIORITY     1     /* just above IDLE */
#define TRAX_CFG_CTRL_TASK_STACK_SIZE   512

/* --- Multi-core ----------------------------------------------------------
 * Leave at the default 1 for single-core and for AMP (each core runs its own
 * image + buffer + transport). Set to 2 ONLY for a true SMP build sharing one
 * ring buffer; then also define TRAX_ZYNQ_PORT_DEFINE_LOCK in exactly one
 * translation unit. */
/* #define TRAX_CFG_CORE_COUNT          2 */

/* --- Metadata / identification / buffers ---------------------------------
 * ELF_ONLY keeps the metadata out of the image entirely (the host reads it
 * from the .elf), which also sidesteps the FLASH region aliasing. */
#define TRAX_CFG_META_STORAGE           TRAX_META_ELF_ONLY
#define TRAX_CFG_PROJECT_NAME           "ZynqA9App"
#define TRAX_CFG_LOG_COMPILE_LEVEL      TRAX_LOG_LEVEL_TRACE
#define TRAX_CFG_OUT_BUFFER_SIZE32      4096U   /* 16 KB */
#define TRAX_CFG_IN_BUFFER_SIZE32       64U

/* --- Trace IDs (add your own) -------------------------------------------- */
enum { TID_LOG_APP = TRAX_TID_RANGE_LOG_USER_START };
enum { TID_VAR_COUNTER = TRAX_TID_RANGE_VAR_USER_START };

#endif /* APP_TRAX_CONFIG_H_ */
