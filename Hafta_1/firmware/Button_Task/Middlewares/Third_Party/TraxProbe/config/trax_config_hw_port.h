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
 * @file           : trax_config_hw_port.h
 * @brief          : TRAX Hardware Port Configuration
 * @version        : 2.0.0
 ******************************************************************************
 * @attention
 * 
 * This file contains hardware-port-dependent configuration:
 *   - Timestamp mode, bit split, and timer configuration
 *   - Timer frequency and tick counter period
 *   - Multi-probe sync configuration
 *
 * RTOS configuration has been moved to trax_config_rtos.h
 * 
 * THESE VALUES MUST BE DEFINED BY USER in App/Config/trax_config.h!
 * 
 * DO NOT MODIFY THIS FILE!
 * To configure, define values in App/Config/trax_config.h
 * 
 ******************************************************************************
 */

#ifndef TRAX_CONFIG_HW_PORT_H_
#define TRAX_CONFIG_HW_PORT_H_

/*=============================================================================
====================TIMESTAMP MODE CONFIGURATION================================
============================================================================*/

/**
 * @brief Timestamp mode constants
 *
 * TRAX_TIMESTAMP_FREERUN    — 32-bit free-running HW counter (e.g., ARM DWT->CYCCNT,
 *                             RISC-V mcycle). Single atomic read, no packing needed.
 * TRAX_TIMESTAMP_TICK_TIMER — Software tick counter + timer register with pending-IRQ
 *                             race correction. For platforms without a 32-bit freerun counter.
 *
 * The mode selects the PUT macro implementation on the embedded side.
 * It is NOT sent to the PC — the protocol is self-describing via
 * tick_bits, tick_counter_period, and direction.
 */
#define TRAX_TIMESTAMP_FREERUN     0
#define TRAX_TIMESTAMP_TICK_TIMER  1

/**
 * @brief Timestamp mode selection
 *
 * Set in App/Config/trax_config.h:
 *   #define TRAX_CFG_TIMESTAMP_MODE  TRAX_TIMESTAMP_FREERUN     // 32-bit free-running counter
 *   #define TRAX_CFG_TIMESTAMP_MODE  TRAX_TIMESTAMP_TICK_TIMER  // tick + timer register
 *
 * Default: TRAX_TIMESTAMP_TICK_TIMER — works on every supported MCU
 *          out of the box (only requires the OS tick + the active
 *          timer register that already drives it).  Override to
 *          TRAX_TIMESTAMP_FREERUN when a 32-bit free-running counter
 *          is available (DWT->CYCCNT, RISC-V mcycle, etc.) for
 *          higher temporal resolution.
 */
#ifndef TRAX_CFG_TIMESTAMP_MODE
    #define TRAX_CFG_TIMESTAMP_MODE  TRAX_TIMESTAMP_TICK_TIMER
#endif

/*=============================================================================
====================TIMESTAMP BIT SPLIT CONFIGURATION===========================
============================================================================*/

/**
 * @brief Timer direction constants
 */
#define TRAX_TIMER_DIR_UP    0   /**< Timer counts up (free-running counters, most HW timers) */
#define TRAX_TIMER_DIR_DOWN  1   /**< Timer counts down (SysTick->VAL) */

/**
 * @brief Number of upper bits used for the tick counter (1-31)
 *
 * The 32-bit timestamp word is split as [tick:TICK_BITS][fine:(32-TICK_BITS)].
 * Default: 8 (compatible with 32-bit free-running counter upper-8-bit wrap).
 *
 * Examples:
 *   8  → [tick:8][fine:24]  — 32-bit freerun counter, SysTick (24-bit)
 *   16 → [tick:16][fine:16] — 16-bit HW timer
 */
#ifndef TRAX_CFG_TICK_BITS
    #define TRAX_CFG_TICK_BITS  8
#endif

/**
 * @brief Timer counting direction
 *
 * TRAX_TIMER_DIR_UP   (0) — Timer counts up. Free-running counters, most HW timers.
 * TRAX_TIMER_DIR_DOWN (1) — Timer counts down (default). SysTick->VAL.
 *
 * The embedded PUT macro writes raw timer values without conversion.
 * The PC normalizes down-counting fine values after parsing.
 */
/* Default per mode:
 *   FREERUN    -> UP.   Every free-running counter we ship a port for counts
 *                 up (DWT->CYCCNT, CNTVCT_EL0, the Zynq-7000 global timer,
 *                 PMCCNTR, RISC-V mcycle). A DOWN default here is actively
 *                 harmful in this mode: trax_timestamp_poll() would read
 *                 `current > last` as a wrap and fire on nearly EVERY call,
 *                 flooding the host with bogus wrap frames and inverting the
 *                 fine value in trax_timestamp_get_us() (`fine = ~counter`).
 *                 A genuine 32-bit down-counter can still say so explicitly.
 *   TICK_TIMER -> DOWN. The common case is SysTick->VAL, which counts down. */
#ifndef TRAX_CFG_TIMESTAMP_TIMER_DIR
    #if (TRAX_CFG_TIMESTAMP_MODE == TRAX_TIMESTAMP_FREERUN)
        #define TRAX_CFG_TIMESTAMP_TIMER_DIR  TRAX_TIMER_DIR_UP
    #else
        #define TRAX_CFG_TIMESTAMP_TIMER_DIR  TRAX_TIMER_DIR_DOWN
    #endif
#endif

/*=============================================================================
====================TIMESTAMP TIMER CONFIGURATION===============================
============================================================================*/

/**
 * @brief Timestamp timer value and IRQ pending check
 *
 * WIRE FORMAT (identical for both modes):
 *   32-bit timestamp word: [tick:TICK_BITS][fine:(32-TICK_BITS)]
 *
 * ─── FREERUN MODE (TRAX_TIMESTAMP_FREERUN) ───────────────────────────
 *
 *   A 32-bit free-running hardware counter. Its upper TICK_BITS naturally
 *   serve as the tick, lower bits as fine. Single atomic read, no packing.
 *
 *   The hardware port must define TRAX_HW_PORT_FREERUN_COUNTER as the
 *   register read expression (e.g., DWT->CYCCNT on ARM, read_csr(mcycle)
 *   on RISC-V).
 *
 *   No user-defined timer macros (TIMER_VAL, CHECK_IRQ_PENDING) required.
 *
 *   In trax_config.h:
 *     #define TRAX_CFG_TIMESTAMP_MODE   TRAX_TIMESTAMP_FREERUN
 *     #define TRAX_CFG_TICK_BITS        8               // [tick:8][fine:24]
 *     #define TRAX_CFG_TIMER_FREQ_HZ    168000000U      // counter clock
 *
 *   Wrap tracking (still required):
 *     Call trax_timestamp_tick() from a periodic ISR, or
 *     trax_timestamp_poll() from the main loop.
 *
 * ─── TICK_TIMER MODE (TRAX_TIMESTAMP_TICK_TIMER) ─────────────────────
 *
 *   Uses software tick counter (driven by periodic ISR) as coarse time
 *   and a timer register as fine time.
 *
 *   The MCU-side pending-IRQ check that used to live here has been
 *   removed (Apr 2026). The host now detects backward jumps in the
 *   reconstructed timeline (per-core) inside
 *   MessageDecoder::calculateDeviceTime and nudges stale ticks forward,
 *   using timestamp-wrap frames as ground-truth resync points. This handles
 *   the timer-fired-before-ISR-ran race without needing the
 *   PENDSTSET / equivalent peripheral check on every timestamp read,
 *   and is robust against the case where the wrap check itself races
 *   with a higher-priority ISR.
 *
 *   In trax_config.h:
 *     #define TRAX_CFG_TIMESTAMP_MODE       TRAX_TIMESTAMP_TICK_TIMER
 *     #define TRAX_CFG_TICK_BITS            8
 *     #define TRAX_CFG_TIMESTAMP_TIMER_VAL  (SysTick->VAL)
 *     #define TRAX_CFG_TIMESTAMP_TIMER_DIR  TRAX_TIMER_DIR_DOWN
 *     #define TRAX_CFG_TICK_COUNTER_PERIOD  48000U
 *     #define TRAX_CFG_TIMER_FREQ_HZ        48000000U
 *
 *   Wrap tracking (call from timer ISR or RTOS tick hook):
 *     Call trax_timestamp_tick() from the timer ISR.
 *
 * NOTES:
 *   - Both modes produce the same wire format [tick:TICK_BITS][fine:(32-TICK_BITS)]
 *   - TRAX_CFG_TIMER_FREQ_HZ must match the actual counter/timer frequency
 *   - trax_timestamp_poll() (FREERUN) or trax_timestamp_tick() (TICK_TIMER) must be called periodically
 */
#if (TRAX_CFG_TIMESTAMP_MODE == TRAX_TIMESTAMP_TICK_TIMER)
    #ifndef TRAX_CFG_TIMESTAMP_TIMER_VAL
        #error "TRAX_CFG_TIMESTAMP_TIMER_VAL must be defined in App/Config/trax_config.h for TICK_TIMER mode!"
    #endif
#endif

/**
 * @brief Timer frequency configuration (REQUIRED)
 * 
 * These MUST be defined in App/Config/trax_config.h
 * 
 * PURPOSE:
 *   Specifies the frequency of the timer used for timestamps.
 *   This value is sent to the host in metadata frames so it can
 *   correctly interpret timestamp values.
 *   
 *   Actual frequency = TRAX_CFG_TIMER_FREQ_HZ / TRAX_CFG_TIMER_FREQ_DIV
 * 
 * RATIONALE FOR NUMERATOR/DIVISOR:
 *   Some MCUs (e.g., STM32H7 at 480 MHz with fractional PLLs) can have
 *   non-integer timer frequencies. Using numerator/divisor allows exact
 *   representation of any rational frequency.
 * 
 * EXAMPLES:
 *   Timer at 1 MHz (integer frequency):
 *     #define TRAX_CFG_TIMER_FREQ_HZ   1000000U
 *     #define TRAX_CFG_TIMER_FREQ_DIV  1U
 *   
 *   Timer at 48.000123 MHz (non-integer):
 *     #define TRAX_CFG_TIMER_FREQ_HZ   48000123U
 *     #define TRAX_CFG_TIMER_FREQ_DIV  1000U
 *     // Actual freq = 48000123 / 1000 = 48000.123 kHz
 *   
 *   Timer at 480 MHz (H7 series):
 *     #define TRAX_CFG_TIMER_FREQ_HZ   480000000U
 *     #define TRAX_CFG_TIMER_FREQ_DIV  1U
 * 
 * NOTES:
 *   - TRAX_CFG_TIMER_FREQ_DIV defaults to 1 if not defined
 *   - Must match the actual timer frequency configured in your hardware
 *   - Used in metadata transmission for host timestamp decoding
 */
#ifndef TRAX_CFG_TIMER_FREQ_HZ
    #error "TRAX_CFG_TIMER_FREQ_HZ must be defined in trax_config.h! \
Example: #define TRAX_CFG_TIMER_FREQ_HZ  1000000U  /* 1 MHz */"
#endif

#ifndef TRAX_CFG_TIMER_FREQ_DIV
    #define TRAX_CFG_TIMER_FREQ_DIV  1U  /**< Default divisor is 1 (no division) */
#endif

/* Validate divisor is not zero */
#if (TRAX_CFG_TIMER_FREQ_DIV == 0)
    #error "TRAX_CFG_TIMER_FREQ_DIV must be >= 1!"
#endif

/*=============================================================================
====================TICK COUNTER PERIOD CONFIGURATION===========================
============================================================================*/

/**
 * @brief Tick counter period — fine ticks per tick increment
 *
 * FREERUN:    auto-derived as 2^fine_bits = 2^(32-TICK_BITS). Do not override.
 * TICK_TIMER: must be set by the user (e.g., SysTick->LOAD + 1 or ARR + 1).
 *
 * Examples:
 *   SysTick at 48 MHz, 1ms tick: #define TRAX_CFG_TICK_COUNTER_PERIOD 48000U
 *   16-bit TIM at 1 MHz, ARR=999: #define TRAX_CFG_TICK_COUNTER_PERIOD 1000U
 */
#if (TRAX_CFG_TIMESTAMP_MODE == TRAX_TIMESTAMP_FREERUN)
    #ifdef TRAX_CFG_TICK_COUNTER_PERIOD
        #error "TRAX_CFG_TICK_COUNTER_PERIOD must NOT be defined in FREERUN mode (auto-derived from TICK_BITS)"
    #endif
    #define TRAX_CFG_TICK_COUNTER_PERIOD  (1UL << (32U - TRAX_CFG_TICK_BITS))
#else
    #ifndef TRAX_CFG_TICK_COUNTER_PERIOD
        #error "TRAX_CFG_TICK_COUNTER_PERIOD must be defined in trax_config.h for TICK_TIMER mode. \
It is the number of fine timer cycles per tick increment (e.g., SysTick->LOAD + 1 or TIM ARR + 1). \
Example: SysTick at 64 MHz, 1ms tick: #define TRAX_CFG_TICK_COUNTER_PERIOD 64000U"
    #endif
#endif

/*=============================================================================
====================TIMESTAMP FORMAT DEFINITIONS (derived)======================
============================================================================*/

/**
 * @brief Timestamp bit field masks, shifts, and overflow period
 *
 * All derived from TRAX_CFG_TICK_BITS. Timestamp encoding (32 bits):
 *   [31 : 32-TICK_BITS]  = tick counter
 *   [fine_bits-1 : 0]    = fine timer value
 *
 * TRAX_CFG_TICK_OVERFLOW_PERIOD = 2^TICK_BITS - 1.
 * Uses (2^N - 1) instead of 2^N so that the pending-IRQ tick_cntr++
 * compensation in TIMEPACKED_GET32 can never overflow the tick bit field.
 * Traxcope derives this from tick_bits — it is not sent on the wire.
 */
#define TRAX_TIMESTAMP_TICK_SHIFT      (32U - TRAX_CFG_TICK_BITS)
#define TRAX_TIMESTAMP_VAL_MASK        ((1UL << TRAX_TIMESTAMP_TICK_SHIFT) - 1U)
#define TRAX_TIMESTAMP_TICK_MASK       ((1UL << TRAX_CFG_TICK_BITS) - 1U)
#define TRAX_CFG_TICK_OVERFLOW_PERIOD  ((1UL << TRAX_CFG_TICK_BITS) - 1U)

/**
 * @brief SysTick (or equivalent periodic interrupt) rate in Hz.
 *
 * Used by trax_diag to convert tick_overflow_cntr deltas into wall-clock
 * seconds for the throughput calculation:
 *
 *   elapsed_s = overflow_delta * TICK_OVERFLOW_PERIOD / TICK_RATE_HZ
 *
 * where TICK_OVERFLOW_PERIOD = 2^TICK_BITS - 1 and each overflow
 * represents that many tick-counter increments (one per SysTick IRQ).
 *
 * Auto-derived from TRAX_CFG_TIMER_FREQ_HZ / TRAX_CFG_TICK_COUNTER_PERIOD
 * when the user does not define it explicitly.  Both of those macros are
 * always available at this point: TRAX_CFG_TIMER_FREQ_HZ is set by the
 * user in trax_config.h (required), and TRAX_CFG_TICK_COUNTER_PERIOD
 * defaults to 1000 if not overridden.
 *
 * Override in trax_config.h only if your SysTick rate differs from the
 * ratio of the hardware-timer frequency to the counter period (e.g. when
 * you use an external low-power oscillator for the tick).
 */
#if !defined(TRAX_CFG_TICK_RATE_HZ) && defined(TRAX_CFG_TIMER_FREQ_HZ)
#define TRAX_CFG_TICK_RATE_HZ (TRAX_CFG_TIMER_FREQ_HZ / TRAX_CFG_TICK_COUNTER_PERIOD)
#endif

/*=============================================================================
====================EXAMPLE CONFIGURATIONS (commented)=========================
============================================================================*/

/*
 * EXAMPLE 1: Free-running 32-bit counter (ARM DWT, RISC-V mcycle, etc.)
 *
 * Hardware port defines TRAX_HW_PORT_FREERUN_COUNTER (e.g., DWT->CYCCNT).
 * See your hardware port's trax_hw_port.h for setup instructions.
 *
 * Configuration (in trax_config.h):
 *   #define TRAX_CFG_TIMESTAMP_MODE   TRAX_TIMESTAMP_FREERUN
 *   #define TRAX_CFG_TICK_BITS        8               // [tick:8][fine:24]
 *   #define TRAX_CFG_TIMER_FREQ_HZ    168000000U      // counter clock
 *   // tick_counter_period auto-derived: 1 << 24 = 16777216
 *   // direction auto-derived: UP (free-running counters count up)
 *
 * Wrap tracking (periodic ISR or main loop):
 *   trax_timestamp_tick();   // from ISR — preferred
 *   trax_timestamp_poll();   // from main loop — alternative
 */

/*
 * EXAMPLE 2: Tick + timer register (e.g., SysTick on Cortex-M0/M0+)
 *
 * Configuration (in trax_config.h):
 *   #define TRAX_CFG_TIMESTAMP_MODE       TRAX_TIMESTAMP_TICK_TIMER
 *   #define TRAX_CFG_TICK_BITS            8
 *   #define TRAX_CFG_TIMESTAMP_TIMER_VAL  (SysTick->VAL)
 *   #define TRAX_CFG_TIMESTAMP_TIMER_DIR  TRAX_TIMER_DIR_DOWN
 *   #define TRAX_CFG_TICK_COUNTER_PERIOD  48000U
 *   #define TRAX_CFG_TIMER_FREQ_HZ        48000000U
 *
 * Wrap tracking (in timer ISR):
 *   trax_timestamp_tick();
 */

/*
 * EXAMPLE 3: 16-bit HW Timer (up-counting, 1 MHz, ARR=999)
 *
 * Configuration (in trax_config.h):
 *   #define TRAX_CFG_TIMESTAMP_MODE       TRAX_TIMESTAMP_TICK_TIMER
 *   #define TRAX_CFG_TICK_BITS            16
 *   #define TRAX_CFG_TIMESTAMP_TIMER_VAL  (TIM3->CNT)
 *   #define TRAX_CFG_TIMESTAMP_TIMER_DIR  TRAX_TIMER_DIR_UP
 *   #define TRAX_CFG_TICK_COUNTER_PERIOD  1000U
 *   #define TRAX_CFG_TIMER_FREQ_HZ        1000000U
 */

/*=============================================================================
====================CONFIGURATION VALIDATION===================================
============================================================================*/

#if (TRAX_CFG_TIMESTAMP_MODE != TRAX_TIMESTAMP_FREERUN) && \
    (TRAX_CFG_TIMESTAMP_MODE != TRAX_TIMESTAMP_TICK_TIMER)
    #error "TRAX_CFG_TIMESTAMP_MODE must be TRAX_TIMESTAMP_FREERUN (0) or TRAX_TIMESTAMP_TICK_TIMER (1)"
#endif

#if (TRAX_CFG_TICK_BITS < 1) || (TRAX_CFG_TICK_BITS > 31)
    #error "TRAX_CFG_TICK_BITS must be between 1 and 31"
#endif

#if (TRAX_CFG_TIMESTAMP_TIMER_DIR != TRAX_TIMER_DIR_UP) && \
    (TRAX_CFG_TIMESTAMP_TIMER_DIR != TRAX_TIMER_DIR_DOWN)
    #error "TRAX_CFG_TIMESTAMP_TIMER_DIR must be TRAX_TIMER_DIR_UP (0) or TRAX_TIMER_DIR_DOWN (1)"
#endif

/* Removed FREERUN UP-direction restriction to support 32-bit down-counters */

/*=============================================================================
====================SYNC INPUT FILTER DELAY====================================
============================================================================*/

/**
 * @brief Sync input filter delay, expressed as a rational NUM/DEN pair.
 *
 *   delay_seconds = TRAX_CFG_SYNC_FILTER_DELAY_NUM / TRAX_CFG_SYNC_FILTER_DELAY_DEN
 *
 * Forward from the BSP in trax_config.h:
 *   #define TRAX_CFG_SYNC_FILTER_DELAY_NUM  BSP_SYNC_FILTER_DELAY_NUM
 *   #define TRAX_CFG_SYNC_FILTER_DELAY_DEN  BSP_SYNC_FILTER_DELAY_DEN
 *
 * If neither macro is defined the default is zero delay (NUM=0, DEN=1).
 */
#ifndef TRAX_CFG_SYNC_FILTER_DELAY_NUM
#  define TRAX_CFG_SYNC_FILTER_DELAY_NUM  0U
#endif

#ifndef TRAX_CFG_SYNC_FILTER_DELAY_DEN
#  define TRAX_CFG_SYNC_FILTER_DELAY_DEN  1U
#endif

/*=============================================================================
====================MULTI-CORE / SMP CONFIGURATION=============================
============================================================================*/

/**
 * @brief Number of CPU cores executing THIS firmware image
 *
 * Default: 1.  Override in App/Config/trax_config.h for SMP / multi-core
 * targets where a single firmware image runs on more than one core.
 *
 * What this knob controls
 * -----------------------
 *   1. Sizes the per-core ISR-tick guard array `trax_in_tick_isr[]` in
 *      trax_rtos_tables.c so each core gets its own portYIELD-from-ISR
 *      desync guard.  On single-core (== 1) it degenerates to a
 *      one-element array — same memory and code as the previous scalar.
 *   2. Selects whether `TRAX_FRAME_CORE_ID_BITS()` (in trax_frame.h)
 *      compiles to the constant `0U` (single-core, optimises away) or
 *      to a runtime `(TRAX_PORT_GET_CORE_ID() << 24)` OR'd into the
 *      tid wire word.  The host's FrameParser unconditionally extracts
 *      the upper byte as `coreId` (see UI/.../FrameParser.cpp::55), so
 *      single-core firmware sends 0x00 in that byte and the host
 *      naturally lands every event on core 0.
 *   3. Hard upper bound of 63 because the wire `coreId` byte is shared
 *      with potential future flag bits in the [reserved:8] middle slot
 *      of the id word; we keep practical headroom.  No real SMP target
 *      we ship to has > 16 cores.
 *
 * What this knob does NOT control
 * --------------------------------
 *   - Whether the trace ringbuffer is per-core or shared.  The current
 *     design is a single shared buffer protected by
 *     TRAX_PORT_ENTER_CRITICAL_SECTION, which on SMP must be redefined
 *     in the hardware port to add a spinlock around the irq-disable
 *     (see the documentation block above TRAX_ENTER_CRITICAL in
 *     hw_port/ARM_Cortex_M/include/trax_hw_port.h).  Per-core
 *     ringbuffers were considered and rejected: the host's gap-detector
 *     relies on a single monotonic global trans_counter (see
 *     UI/.../MessageDecoder.cpp::checkGap), so per-core counters /
 *     buffers would force a host rework with no observable benefit at
 *     our scale.
 *   - Whether the timestamp source is coherent across cores.  That is
 *     a hardware-port concern (RP2040: shared system timer is coherent;
 *     STM32H7 dual-core M7+M4: per-core cycle counters are NOT, host-
 *     side per-core wrap reconciliation via timestamp-wrap frames handles it).
 *
 * Topology examples
 * -----------------
 *   STM32H7 dual-core M7+M4 (AMP, two separate firmware images):
 *     Each image is single-core from its own perspective.
 *     #define TRAX_CFG_CORE_COUNT 1   // (the default — no override needed)
 *
 *   RP2040 (SMP, one firmware image runs on both cores):
 *     #define TRAX_CFG_CORE_COUNT 2
 *     // and define TRAX_PORT_GET_CORE_ID() in trax_hw_port.h
 *     // as a SIO_CPUID register read.
 */
#ifndef TRAX_CFG_CORE_COUNT
    #define TRAX_CFG_CORE_COUNT  1U
#endif

#if (TRAX_CFG_CORE_COUNT < 1) || (TRAX_CFG_CORE_COUNT > 63)
    #error "TRAX_CFG_CORE_COUNT must be in [1..63] (wire id-word coreId byte is uint8_t and we reserve headroom)"
#endif

/*=============================================================================
====================PRIORITY ORDER CONFIGURATION================================
============================================================================*/

/* The two priority-ordering bytes that travel in the SESSION_START header
 * were intentionally moved out of this file because they are not user
 * "configuration" — they are facts owned by the hardware port and RTOS
 * port respectively, and exposing them as `#ifndef`-guarded knobs in a
 * config aggregate would falsely suggest they were tunable.
 *
 *   - TRAX_CFG_ISR_PRIO_ASCENDING is a *platform* fact (silicon-level on
 *     embedded targets). It now lives in
 *     `hw_port/<arch>/include/trax_hw_port.h` as a hard `#define`
 *     (no override) for known-architecture ports, and as a guarded
 *     fallback only in `hw_port/Generic/`.
 *   - TRAX_CFG_TASK_PRIO_ASCENDING is an *RTOS API* fact. It now lives
 *     in `os/<rtos>/trax_rtos_port.h` as a hard `#define`, with a
 *     guarded bare-metal fallback in the dispatcher
 *     `inc/trax_rtos_port.h` (where there is no RTOS to ask).
 *
 * Hard-coding the facts means the build fails loudly if anyone tries to
 * redefine them elsewhere — preventing silent "I lied to the host about
 * what my hardware does" bugs that render trace lanes upside-down. */

#endif /* TRAX_CONFIG_HW_PORT_H_ */

