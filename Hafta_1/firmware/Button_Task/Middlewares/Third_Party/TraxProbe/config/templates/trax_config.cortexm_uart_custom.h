/**
 ******************************************************************************
 * @file           : trax_config.h   (preset: Cortex-M + custom UART transport)
 * @brief          : TraxProbe config for production / no-debugger deployments
 ******************************************************************************
 * USE THIS WHEN
 * -------------
 *   - You ship over UART/USB-CDC/TCP instead of an RTT debug probe.
 *   - The host has no .elf, so the probe must send the metadata schema on the
 *     wire at session start  ->  TRAX_CFG_META_STORAGE = TRAX_META_IN_FLASH.
 *
 * HOW TO USE
 * ----------
 *   1. Copy to your include path as `trax_config.h`.
 *   2. Set the clock values for your part.
 *   3. Define the transport in any .c in your build (NOT the RTT files; no
 *      struct, no register call — the linker binds them by name). ALL FOUR
 *      are required:
 *
 *         #include <trax_transport.h>
 *         int    trax_transport_init(void)                     { return 0; }
 *         size_t trax_transport_write(const void *p, size_t n) { ... }
 *         size_t trax_transport_read(void *p, size_t n)        { ... }
 *         void   trax_transport_clear_tx(void)                 { }
 *
 *         trax_init();   // calls trax_transport_init() for you
 *
 *      Leave any of them undefined and the link fails naming the exact symbol
 *      (undefined reference to 'trax_transport_…').
 *
 *   4. Optionally point the transport knobs below at your own functions. They
 *      all default to the behaviour a transport had before they existed, so
 *      none of them is needed to get streaming — but TRAX_CFG_TRANSPORT_TX_FREE
 *      is worth wiring up: it lets the core size each drain batch to your real
 *      TX free space instead of collecting, being rejected, and retrying.
 *
 *   5. Drive the engine: bare-metal -> trax_process() in the loop; FreeRTOS ->
 *      switch TRAX_CFG_RTOS_TYPE to TRAX_RTOS_FREERTOS (see the FreeRTOS preset).
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

/* --- Transport: user-provided callbacks ----------------------------------
 * Drop the built-in RTT transport (and its static buffers) and provide your
 * own by implementing the four trax_transport_*() functions declared in
 * <trax_transport.h>. They are bound at link time — no struct, no register. */
#define TRAX_CFG_TRANSPORT              TRAX_TRANSPORT_CUSTOM

/* Optional but recommended: name a size_t(void) function returning your TX
 * ring's free space. The core caps every drain batch at it before collecting
 * frames, so trax_transport_write() never has to reject one. Leave it out and
 * the core assumes free space is unknown and falls back to try-and-retry —
 * correct, but then TRAX_CFG_TX_BATCH_MAX_BYTES must stay below the TX ring
 * capacity below or a max-size batch can never fit. */
/* #define TRAX_CFG_TRANSPORT_TX_FREE   my_uart_tx_free */

/* --- RTOS ---------------------------------------------------------------- */
#define TRAX_CFG_RTOS_TYPE              TRAX_RTOS_NONE

/* --- Hard-real-time ISRs (optional, M3/M4/M7/M33+ only) -------------------
 * Production systems often have one or two ISRs with hard deadlines
 * (motor commutation, ADC trigger). Define TRAX_CFG_BASEPRI to switch
 * TraxProbe's short critical sections from PRIMASK (masks everything)
 * to a BASEPRI raise: ISRs more urgent than the value are NEVER delayed
 * by TraxProbe and MUST NOT call any TRAX_* macro. Raw shifted 8-bit
 * value; not available on Cortex-M0/M0+/M23. */
/* #define TRAX_CFG_BASEPRI             (2U << (8U - __NVIC_PRIO_BITS)) */

/* --- Metadata: send schema on the wire (no .elf needed on host) ---------- */
#define TRAX_CFG_META_STORAGE           TRAX_META_IN_FLASH

/* --- Identification / buffers -------------------------------------------- */
#define TRAX_CFG_PROJECT_NAME           "MyProduct"
#define TRAX_CFG_BUILD_VERSION          "1.0.0"
#define TRAX_CFG_LOG_COMPILE_LEVEL          TRAX_LOG_LEVEL_INFO   /* trim debug noise */
#define TRAX_CFG_OUT_BUFFER_SIZE32      2048U   /* 8 KB; size to your UART rate */
#define TRAX_CFG_IN_BUFFER_SIZE32       64U
#define TRAX_CFG_TRANSPORT_BUF_SIZE     4096U   /* UART TX ring bytes — diag only */

/* --- Trace IDs (add your own) -------------------------------------------- */
enum { TID_LOG_APP = TRAX_TID_RANGE_LOG_USER_START };
enum { TID_VAR_COUNTER = TRAX_TID_RANGE_VAR_USER_START };

#endif /* APP_TRAX_CONFIG_H_ */
