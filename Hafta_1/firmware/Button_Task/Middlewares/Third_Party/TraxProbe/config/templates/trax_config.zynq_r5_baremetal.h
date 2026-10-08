/**
 ******************************************************************************
 * @file           : trax_config.h   (preset: ZynqMP Cortex-R5 bare metal)
 * @brief          : TraxProbe config for a Zynq UltraScale+ RPU application
 ******************************************************************************
 * USE THIS WHEN
 * -------------
 *   - ZynqMP RPU (Cortex-R5F), Xilinx standalone BSP, no RTOS.
 *   - Timestamp from the PMU cycle counter (PMCCNTR) — highest resolution
 *     available on the R5.
 *   - A custom transport (UART / PL FIFO / shared memory to the APU).
 *
 * HOW TO USE
 * ----------
 *   1. Copy to your include path as `trax_config.h`.
 *   2. Set TRAX_CFG_TIMER_FREQ_HZ to the R5 CPU clock.
 *   3. Enable PMCCNTR once at startup (snippet below) — the BSP does not.
 *   4. Call trax_timestamp_poll() from your main loop. On bare metal nothing
 *      else tracks counter wrap, and at 600 MHz the counter wraps every
 *      ~7.2 s. This step is NOT optional; see hw_port/Zynq/README.md.
 *   5. Add to the build: the src, debug, os/common and os/BareMetal sources,
 *      plus your transport .c file.
 *   6. Alias the linker regions in lscript.ld and add trax_probe.ld to the
 *      link line (see hw_port/Zynq/README.md, "Linker script").
 *
 * PMCCNTR ENABLE (call once, early in main)
 * -----------------------------------------
 *   uint32_t pmcr;
 *   __asm volatile("mrc p15,0,%0,c9,c12,0" : "=r"(pmcr));
 *   pmcr |=  (1U << 0) | (1U << 2);   // E = enable, C = reset cycle counter
 *   pmcr &= ~(1U << 3);               // D = 0  -> do NOT divide by 64
 *   __asm volatile("mcr p15,0,%0,c9,c12,0" :: "r"(pmcr));
 *   __asm volatile("mcr p15,0,%0,c9,c12,1" :: "r"(1U << 31));  // PMCNTENSET
 ******************************************************************************
 */

#ifndef APP_TRAX_CONFIG_H_
#define APP_TRAX_CONFIG_H_

#define TRAX_CFG_STREAM_CNT                0
#include "trax_tid.h"

/* --- Hardware port ------------------------------------------------------- */
#define TRAX_CFG_HW_PORT                TRAX_HW_PORT_ZYNQ

/* --- Timestamp: PMU cycle counter, free-running, counts UP ---------------
 * TICK_COUNTER_PERIOD must NOT be set in FREERUN mode (it is derived from
 * TICK_BITS). Keep PMCR.D = 0 or the counter ticks at CPU/64 and this
 * frequency is wrong by that factor. */
#define TRAX_CFG_TIMESTAMP_MODE         TRAX_TIMESTAMP_FREERUN
#define TRAX_CFG_TIMESTAMP_TIMER_DIR    TRAX_TIMER_DIR_UP
#define TRAX_CFG_TIMER_FREQ_HZ          600000000U    /* <-- R5 CPU clock */

/* --- ISR-context detection ----------------------------------------------
 * Not required for a bare-metal build: nothing consumes TRAX_PORT_IN_ISR()
 * when TRAX_CFG_RTOS_TYPE is NONE, so the port leaves it undefined and the
 * consumers degrade cleanly. Uncomment the pair below only if you later add
 * an RTOS or want the predicate for your own instrumentation — then also
 * wrap your GIC dispatch with trax_zynq_irq_enter()/trax_zynq_irq_exit()
 * and define TRAX_ZYNQ_PORT_DEFINE_IRQ_NESTING in exactly one .c file. */
/* #define TRAX_CFG_ZYNQ_OWN_IRQ_NESTING  1 */

/* --- Interrupt masking ---------------------------------------------------
 * Default 1 = critical sections mask IRQ *and* FIQ. R5 designs often reserve
 * FIQ for hard-real-time work; set this to 0 to keep FIQ latency untouched,
 * on the contract that no FIQ-reachable code calls a TRAX_* macro. */
/* #define TRAX_CFG_ZYNQ_MASK_FIQ       0 */

/* --- Transport -----------------------------------------------------------
 * Implement the four trax_transport_*() functions (see inc/trax_transport.h).
 * If the transport DMAs the out-buffer, place it in non-cacheable memory. */
#define TRAX_CFG_TRANSPORT              TRAX_TRANSPORT_CUSTOM

/* --- RTOS ---------------------------------------------------------------- */
#define TRAX_CFG_RTOS_TYPE              TRAX_RTOS_NONE

/* --- Metadata / identification / buffers --------------------------------- */
#define TRAX_CFG_META_STORAGE           TRAX_META_ELF_ONLY
#define TRAX_CFG_PROJECT_NAME           "ZynqMpR5App"
#define TRAX_CFG_LOG_COMPILE_LEVEL      TRAX_LOG_LEVEL_TRACE
#define TRAX_CFG_OUT_BUFFER_SIZE32      2048U   /* 8 KB */
#define TRAX_CFG_IN_BUFFER_SIZE32       64U

/* --- Trace IDs (add your own) -------------------------------------------- */
enum { TID_LOG_APP = TRAX_TID_RANGE_LOG_USER_START };
enum { TID_VAR_COUNTER = TRAX_TID_RANGE_VAR_USER_START };

#endif /* APP_TRAX_CONFIG_H_ */
