/**
 ******************************************************************************
 * @file           : trax_config.h   (preset: MicroBlaze bare metal)
 * @brief          : TraxProbe config for a Xilinx MicroBlaze soft core
 ******************************************************************************
 * USE THIS WHEN
 * -------------
 *   - MicroBlaze in the PL, Xilinx standalone BSP, no RTOS.
 *   - Timestamp from an AXI Timer running free.
 *   - A custom transport (AXI UART-Lite / UART16550 / PL FIFO).
 *
 * HOW TO USE
 * ----------
 *   1. Copy to your include path as `trax_config.h`.
 *   2. Point TRAX_CFG_MB_TIMER_BASE at your AXI Timer and set
 *      TRAX_CFG_TIMER_FREQ_HZ to the TIMER's clock (often not the CPU clock).
 *   3. Start the timer free-running once at boot (snippet below).
 *   4. Call trax_timestamp_poll() from your main loop. Nothing else tracks
 *      counter wrap on bare metal; at 100 MHz it wraps every ~43 s.
 *   5. Add to the build: the src, debug, os/common and os/BareMetal sources,
 *      plus your transport .c file.
 *   6. Compile AND link with -mlittle-endian. mb-gcc still defaults to big
 *      endian, and TraxProbe's wire format is little-endian end to end -- the
 *      port refuses to build big-endian rather than emit undecodable frames.
 *
 * AXI TIMER FREE-RUN SETUP (call once, early in main)
 * ---------------------------------------------------
 *   #define TCSR0 (*(volatile uint32_t *)(TRAX_CFG_MB_TIMER_BASE + 0x00))
 *   #define TLR0  (*(volatile uint32_t *)(TRAX_CFG_MB_TIMER_BASE + 0x04))
 *   TLR0  = 0u;          // reload value
 *   TCSR0 = (1u << 5);   // LOAD0: force the counter to TLR0
 *   TCSR0 = (1u << 7);   // ENT0 only: count UP, no auto-reload, no interrupt
 ******************************************************************************
 */

#ifndef APP_TRAX_CONFIG_H_
#define APP_TRAX_CONFIG_H_

#define TRAX_CFG_STREAM_CNT                0
#include "trax_tid.h"

/* --- Hardware port ------------------------------------------------------- */
#define TRAX_CFG_HW_PORT                TRAX_HW_PORT_MICROBLAZE

/* --- Timestamp: AXI Timer TCR0, free-running, counts UP ------------------
 * The port reads TCR0 at base + 0x08. TICK_COUNTER_PERIOD must NOT be set in
 * FREERUN mode -- it is derived from TICK_BITS. */
#define TRAX_CFG_TIMESTAMP_MODE         TRAX_TIMESTAMP_FREERUN
#define TRAX_CFG_TIMESTAMP_TIMER_DIR    TRAX_TIMER_DIR_UP
#define TRAX_CFG_MB_TIMER_BASE          0x41C00000U   /* <-- XPAR_AXI_TIMER_0_BASEADDR */
#define TRAX_CFG_TIMER_FREQ_HZ          100000000U    /* <-- the TIMER's clock */

/* --- Critical section ----------------------------------------------------
 * Default 0 = portable mfs/andi/mts, works on every MicroBlaze. Set to 1 only
 * if your core was generated with C_USE_MSR_INSTR = 1; the mismatch cannot be
 * detected at build time and shows up as an illegal-instruction exception. */
/* #define TRAX_CFG_MB_USE_MSR_INSTR    1 */

/* --- ISR-context detection ----------------------------------------------
 * Not needed bare metal -- nothing reads the predicate when RTOS_TYPE is NONE.
 * Under FreeRTOS it is mandatory and the build fails without it; see
 * hw_port/MicroBlaze/README.md. */
/* #define TRAX_CFG_MB_OWN_IRQ_NESTING  1 */

/* --- Transport -----------------------------------------------------------
 * Implement the four trax_transport_*() functions (see inc/trax_transport.h).
 * If a DMA engine or PL logic drains the buffer, place it in non-cacheable
 * memory -- a data cache is a MicroBlaze generation option. */
#define TRAX_CFG_TRANSPORT              TRAX_TRANSPORT_CUSTOM

/* --- RTOS ---------------------------------------------------------------- */
#define TRAX_CFG_RTOS_TYPE              TRAX_RTOS_NONE

/* --- Metadata / identification / buffers --------------------------------- */
#define TRAX_CFG_META_STORAGE           TRAX_META_ELF_ONLY
#define TRAX_CFG_PROJECT_NAME           "MicroBlazeApp"
#define TRAX_CFG_LOG_COMPILE_LEVEL      TRAX_LOG_LEVEL_TRACE
#define TRAX_CFG_OUT_BUFFER_SIZE32      2048U   /* 8 KB */
#define TRAX_CFG_IN_BUFFER_SIZE32       64U

/* --- Trace IDs (add your own) -------------------------------------------- */
enum { TID_LOG_APP = TRAX_TID_RANGE_LOG_USER_START };
enum { TID_VAR_COUNTER = TRAX_TID_RANGE_VAR_USER_START };

#endif /* APP_TRAX_CONFIG_H_ */
