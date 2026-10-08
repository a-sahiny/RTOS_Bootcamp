/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Embedya.
 *
 * TraxProbe porting layer (reference port). Licensed under the Apache License,
 * Version 2.0 (see LICENSE-Apache-2.0.txt). You may copy and modify this file
 * to support additional hardware or RTOS targets.
 *
 * This file depends on TraxProbe core headers, which remain licensed under the
 * TraxProbe Commercial License and are NOT relicensed by this notice.
 */

/**
 ******************************************************************************
 * @file           : trax_hw_port.h
 * @brief          : ARM Cortex-M Hardware Port Abstraction
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 * 
 * This file provides hardware abstraction for ARM Cortex-M platforms.
 * It includes critical section macros and timestamp/timepacked macros.
 * 
 ******************************************************************************
 */

#ifndef TRAX_HW_PORT_H_
#define TRAX_HW_PORT_H_

#include "trax_config_default.h"
#include "trax_timebase.h"   /* trax_timebase.tick_cntr for TICK_TIMER packing */

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 ====================ISR PRIORITY ORDERING CONVENTION==========================
 ============================================================================*/

/* ARM Cortex-M NVIC convention: **lower priority number = higher priority**
 * (priority 0 is the highest user-controllable priority; SysTick / PendSV
 * default to the lowest). This is a hardware fact of the architecture —
 * the NVIC priority register simply *is* lower-number-wins. There is no
 * legitimate scenario in which an ARM-Cortex-M application would want to
 * override this byte: doing so would not change the hardware, it would
 * only mislead the host into rendering ISR lanes upside-down. The macro
 * is therefore fixed (no `#ifndef` guard) so the build fails loudly if
 * someone tries to redefine it elsewhere. */
#define TRAX_CFG_ISR_PRIO_ASCENDING   0  /* ARM NVIC: lower number wins */

/*=============================================================================
 ====================CRITICAL SECTION MACROS===================================
 ============================================================================*/

/**
 * @brief Enter / exit critical section — block-style macro pair.
 *
 * Saves PRIMASK, disables IRQs, opens an inner block; the matching EXIT
 * macro closes the inner block and restores PRIMASK.  Used as:
 *
 *     TRAX_PORT_ENTER_CRITICAL_SECTION { ... } TRAX_PORT_EXIT_CRITICAL_SECTION
 *
 * SMP variant
 * -----------
 * On single-core Cortex-M parts (M0/M0+/M3/M4/M7/M33 in their default
 * configurations) `__disable_irq()` is sufficient because there is no
 * second harvester to race the trace ringbuffer.  On multi-core parts
 * that run a single TraxProbe image across more than one core
 * (RP2040 dual-M0+, future Cortex-M55-MP, lock-step Cortex-R5/R52
 * cluster, etc.) the macro pair MUST be redefined in the user's
 * trax_config.h (or a downstream hardware port) so the body looks
 * approximately like:
 *
 *     extern volatile uint32_t trax_smp_spinlock;
 *     #define TRAX_PORT_ENTER_CRITICAL_SECTION \
 *         { \
 *             uint32_t primask = __get_PRIMASK(); \
 *             __disable_irq(); \
 *             while (__LDREXW(&trax_smp_spinlock) != 0U) { } \
 *             __STREXW(1U, &trax_smp_spinlock); \
 *             __DMB(); \
 *             {
 *     #define TRAX_PORT_EXIT_CRITICAL_SECTION \
 *             } \
 *             __DMB(); \
 *             trax_smp_spinlock = 0U; \
 *             __set_PRIMASK(primask); \
 *         }
 *
 * The pair MUST disable interrupts FIRST and acquire the spinlock
 * SECOND.  Reversing the order means a higher-priority ISR on the same
 * core can preempt a thread that already holds the spinlock and then
 * try to re-acquire it (deadlock against itself).  On RP2040 there is
 * a hardware spinlock peripheral (SIO_SPINLOCK0..31) that is the
 * cheapest implementation of the inner pair.
 *
 * Note on Baseline cores: ARMv6-M (M0/M0+) and ARMv8-M Baseline (M23) do
 * NOT implement the LDREX/STREX exclusives used in the example above, so a
 * multi-core image on those parts must use a hardware spinlock peripheral
 * (as RP2040's dual-M0+ does) rather than __LDREXW/__STREXW. Single-core
 * builds are unaffected — the default macro below uses only PRIMASK, which
 * exists on every Cortex-M profile.
 *
 * Single-core builds pay zero runtime cost for SMP-readiness — the
 * default macro below is a plain irq-disable / restore pair.
 *
 * BASEPRI variant (hard-real-time ISRs)
 * -------------------------------------
 * The PRIMASK default masks ALL interrupts for the (short, O(1),
 * size-independent) alloc/commit windows.  For applications with
 * hard-timing ISRs that cannot tolerate even those few dozen cycles,
 * define TRAX_CFG_BASEPRI in trax_config.h to switch the critical
 * section to a BASEPRI raise instead:
 *
 *     // FreeRTOS: align TraxProbe's mask with the kernel's — any ISR
 *     // allowed to call FromISR APIs may also call TRAX macros.
 *     #define TRAX_CFG_BASEPRI  configMAX_SYSCALL_INTERRUPT_PRIORITY
 *
 * The value is the RAW 8-bit BASEPRI register value (already shifted
 * into the implemented priority bits — exactly like FreeRTOS's
 * configMAX_SYSCALL_INTERRUPT_PRIORITY), NOT a logical 0..15 priority.
 *
 * CONTRACT: interrupts with priority numerically LOWER than
 * TRAX_CFG_BASEPRI (= more urgent than the mask) are NEVER delayed by
 * TraxProbe — and in exchange they MUST NOT call any TRAX_* macro or
 * API: unmasked, they could preempt a producer mid-allocation and
 * corrupt the ring.  This is the same rule FreeRTOS imposes for its
 * FromISR APIs, so on FreeRTOS the two contracts coincide when the
 * values match.
 *
 * The DSB+ISB pair after the raise guarantees the mask is
 * architecturally in effect before the ring pointers are touched
 * (same sequence as FreeRTOS's vPortRaiseBASEPRI).  BASEPRI exists on
 * ARMv7-M / ARMv8-M Mainline only (M3/M4/M7/M33/M55/M85); baseline
 * cores (M0/M0+/M23) must stay on the PRIMASK default.
 */
#ifndef TRAX_PORT_ENTER_CRITICAL_SECTION
#if defined(TRAX_CFG_BASEPRI)

#if defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_8M_BASE__)
#error "TRAX_CFG_BASEPRI requires the BASEPRI register (ARMv7-M / ARMv8-M Mainline). Cortex-M0/M0+/M23 must use the PRIMASK default — remove TRAX_CFG_BASEPRI from trax_config.h."
#endif

#if (TRAX_CFG_BASEPRI == 0)
#error "TRAX_CFG_BASEPRI must be non-zero: BASEPRI = 0 disables masking entirely and would let every ISR race the trace ring. Use the raw shifted register value (e.g. configMAX_SYSCALL_INTERRUPT_PRIORITY)."
#endif

#define TRAX_PORT_ENTER_CRITICAL_SECTION \
    { \
        uint32_t basepri = __get_BASEPRI(); \
        __set_BASEPRI(TRAX_CFG_BASEPRI); \
        __DSB(); \
        __ISB(); \
        {

#define TRAX_PORT_EXIT_CRITICAL_SECTION \
        } \
        __set_BASEPRI(basepri); \
    }

#else /* !TRAX_CFG_BASEPRI — PRIMASK default (all Cortex-M profiles) */

#define TRAX_PORT_ENTER_CRITICAL_SECTION \
    { \
        uint32_t primask = __get_PRIMASK(); \
        __disable_irq(); \
        {

#define TRAX_PORT_EXIT_CRITICAL_SECTION \
        } \
        __set_PRIMASK(primask); \
    }

#endif /* TRAX_CFG_BASEPRI */
#endif /* TRAX_PORT_ENTER_CRITICAL_SECTION */

/*=============================================================================
 ====================FRAME COMMIT BARRIER (SMP)================================
 ============================================================================*/

/**
 * @brief Store barrier issued immediately before the Word-0 commit store.
 *
 * The ring reader (trax_collect_iov) walks the buffer LOCK-FREE: it never
 * takes the trace critical section, it only tests Word 0 (the commit
 * marker) for non-zero. On a single core that is safe — the reader cannot
 * run while the writer's critical section holds PRIMASK. On SMP (one image,
 * TRAX_CFG_CORE_COUNT > 1, e.g. RP2040) the reader on the other core races
 * the writer: without a barrier, both the compiler (within the critical
 * section) and a weakly-ordered CPU may reorder the Word-0 store AHEAD of
 * the frame-body stores (timestamp / tid / params), letting the reader ship
 * a frame whose body is not yet globally visible.
 *
 * __DMB() orders all prior stores before the commit store. Compiled out
 * entirely on single-core builds — zero cost on the common path.
 */
#ifndef TRAX_PORT_COMMIT_BARRIER
  #if (TRAX_CFG_CORE_COUNT > 1)
    #define TRAX_PORT_COMMIT_BARRIER()   __DMB()
  #else
    #define TRAX_PORT_COMMIT_BARRIER()
  #endif
#endif

/*=============================================================================
 ====================CORE ID (SMP) ============================================
 ============================================================================*/

/**
 * @brief Return the index of the CPU core executing this call (0-based).
 *
 * Single-core Cortex-M parts ALWAYS execute on core 0 from the firmware's
 * point of view, so the default expansion is a compile-time constant and
 * the C compiler folds it away to zero everywhere it is used (the index
 * into trax_in_tick_isr[], the OR with the tid wire word in
 * TRAX_FRAME_CORE_ID_BITS()).
 *
 * Multi-core parts (RP2040, future Cortex-M cluster designs, AMP setups
 * where each core needs to identify itself) MUST override this macro in
 * the user's trax_config.h or in a downstream hardware port BEFORE
 * including trax_hw_port.h.  The override returns a uint8_t in
 * [0 .. TRAX_CFG_CORE_COUNT-1].  Examples:
 *
 *   RP2040 (SIO_CPUID register):
 *     #define TRAX_PORT_GET_CORE_ID() \
 *         ((uint8_t)(*(volatile uint32_t *)(0xD0000000U + 0x000U)))
 *
 *   Cortex-A SMP (MPIDR_EL1[0..1] aff0):
 *     static inline uint8_t trax_port_get_core_id(void) { ... }
 *     #define TRAX_PORT_GET_CORE_ID() trax_port_get_core_id()
 *
 *   AMP STM32H7 (dual M7+M4, each image hard-coded):
 *     #define TRAX_PORT_GET_CORE_ID()  ((uint8_t)0U)   // M7 image
 *     #define TRAX_PORT_GET_CORE_ID()  ((uint8_t)1U)   // M4 image
 *
 * The host extracts coreId from the upper byte of the id wire word
 * (UI/.../FrameParser.cpp: `frame.coreId = (idWord >> 24) & 0xFF`),
 * so ID values written here travel verbatim to the host swimlane it
 * picks for the event.
 */
#ifndef TRAX_PORT_GET_CORE_ID
    #define TRAX_PORT_GET_CORE_ID()  ((uint8_t)0U)
#endif

/*=============================================================================
 ====================ISR CONTEXT PREDICATE=====================================
 ============================================================================*/

/**
 * @brief Evaluates non-zero when executing in exception (ISR) context.
 *
 * Reads IPSR: zero in Thread mode, the active exception number in Handler
 * mode. Available on every Cortex-M profile (M0/M0+/M3/.../M85).
 *
 * Consumed by the FreeRTOS port's pre-V10.4 tick fallback
 * (traceTASK_INCREMENT_TICK in os/FreeRTOS/trax_rtos_port.h) to distinguish
 * the real tick interrupt from xTaskResumeAll()'s pended-tick replay, which
 * re-runs xTaskIncrementTick() in task context.
 */
#ifndef TRAX_PORT_IN_ISR
    #define TRAX_PORT_IN_ISR()  (__get_IPSR() != 0U)
#endif

/*=============================================================================
 ====================CORE FEATURE DETECTION (DWT CYCLE COUNTER)================
 ============================================================================*/

/**
 * @brief Whether this Cortex-M core provides the DWT cycle counter (DWT->CYCCNT).
 *
 * FREERUN timestamp mode reads DWT->CYCCNT as its 32-bit free-running source.
 * That counter exists on the "Mainline" Cortex-M profiles only:
 *
 *   Has DWT->CYCCNT : Cortex-M3, M4, M7, M33, M35P, M55, M85
 *                     (ARMv7-M / ARMv7E-M / ARMv8(-.1)-M Mainline)
 *   No  DWT->CYCCNT : Cortex-M0, M0+, M1, M23
 *                     (ARMv6-M / ARMv8-M Baseline) -> use TICK_TIMER (SysTick)
 *
 * The default critical section (PRIMASK), __DMB() and SysTick are available on
 * every Cortex-M profile, so TICK_TIMER mode works everywhere; only FREERUN is
 * core-gated.
 *
 * Detection order: the CMSIS per-device feature macro __DWT_PRESENT is taken as
 * authoritative when present, then the compiler architecture macros, then the
 * CMSIS core number. To force the result (e.g. a TrustZone M33 whose DWT is
 * secure-only, or a core the heuristics can't classify) define
 * TRAX_PORT_HAS_DWT_CYCCNT to 0/1 before including this header.
 */
#ifndef TRAX_PORT_HAS_DWT_CYCCNT
  #if defined(__DWT_PRESENT)
    #define TRAX_PORT_HAS_DWT_CYCCNT   __DWT_PRESENT
  #elif defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_8M_BASE__)
    #define TRAX_PORT_HAS_DWT_CYCCNT   0   /* M0 / M0+ / M1 / M23 */
  #elif defined(__ARM_ARCH_7M__)  || defined(__ARM_ARCH_7EM__) || \
        defined(__ARM_ARCH_8M_MAIN__) || defined(__ARM_ARCH_8_1M_MAIN__)
    #define TRAX_PORT_HAS_DWT_CYCCNT   1   /* M3 / M4 / M7 / M33 / M55 / M85 */
  #elif defined(__CORTEX_M)
    #if (__CORTEX_M == 0) || (__CORTEX_M == 1) || (__CORTEX_M == 23)
      #define TRAX_PORT_HAS_DWT_CYCCNT 0
    #else
      #define TRAX_PORT_HAS_DWT_CYCCNT 1
    #endif
  #else
    /* Core not recognized — assume no cycle counter (TICK_TIMER works on every
     * Cortex-M). Define -DTRAX_PORT_HAS_DWT_CYCCNT=1 if your core has DWT. */
    #define TRAX_PORT_HAS_DWT_CYCCNT   0
  #endif
#endif

/*=============================================================================
 ====================FREE-RUNNING COUNTER======================================
 ============================================================================*/

/**
 * @brief ARM Cortex-M free-running counter: DWT->CYCCNT
 *
 * 32-bit CPU cycle counter, available on Mainline cores (M3/M4/M7/M33/M55/M85).
 * Defined only when TRAX_PORT_HAS_DWT_CYCCNT is true; on Baseline cores
 * (M0/M0+/M23) there is no such counter, so FREERUN mode is unavailable and
 * TICK_TIMER (SysTick) must be used instead.
 *
 * Must be enabled at startup before first TRAX log:
 *   CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
 *   DWT->CYCCNT = 0;
 *   DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
 */
#if TRAX_PORT_HAS_DWT_CYCCNT
#define TRAX_HW_PORT_FREERUN_COUNTER  (DWT->CYCCNT)
#endif

/*=============================================================================
 ====================TIMESTAMP/TIMETRACK MACROS===============================
 ============================================================================*/

/* Note: TRAX_CFG_TICK_OVERFLOW_PERIOD is defined in trax_config_hw_port.h (not hardware-specific) */
/* Note: Timer frequency is defined via TRAX_CFG_TIMER_FREQ_HZ and TRAX_CFG_TIMER_FREQ_DIV */

/* Note: TRAX_TIMESTAMP_VAL_MASK, TRAX_TIMESTAMP_TICK_MASK, TRAX_TIMESTAMP_TICK_SHIFT 
 * are defined in trax_timestamp.h (protocol-specific, not hardware-specific) */

/*=============================================================================
 ====================TIMESTAMP TIMER VALIDATION================================
 ============================================================================*/

#if (TRAX_CFG_TIMESTAMP_MODE == TRAX_TIMESTAMP_FREERUN) && !TRAX_PORT_HAS_DWT_CYCCNT
    #error "TRAX_TIMESTAMP_FREERUN needs the DWT cycle counter (Cortex-M3/M4/M7/M33/M55/M85). This core (Cortex-M0/M0+/M23, ARMv6-M / ARMv8-M Baseline) has no DWT->CYCCNT. Set TRAX_CFG_TIMESTAMP_MODE = TRAX_TIMESTAMP_TICK_TIMER (SysTick), or define TRAX_PORT_HAS_DWT_CYCCNT=1 if your core actually provides DWT."
#endif

#if (TRAX_CFG_TIMESTAMP_MODE == TRAX_TIMESTAMP_TICK_TIMER)
    #if !defined(TRAX_CFG_TIMESTAMP_TIMER_VAL)
        #error "TRAX_CFG_TIMESTAMP_TIMER_VAL is not defined"
    #endif
    /* TRAX_CFG_TIMESTAMP_TIMER_CHECK_IRQ_PENDING used to be required for the
     * tick-race compensation that this header performed inside
     * TRAX_HW_PORT_TIMEPACKED_GET32/PUT32.  That compensation has been moved
     * to the host (Traxcope MessageDecoder), so the macro is no longer
     * referenced here.  Existing user configs that still define it are
     * harmless — the definition is simply ignored.  */
#endif

/*=============================================================================
 ====================TIMETRACK MACROS=========================================
 ============================================================================*/

#if (TRAX_CFG_TIMESTAMP_MODE == TRAX_TIMESTAMP_FREERUN)

/* ─── FREERUN MODE ───────────────────────────────────────────────────
 * TRAX_HW_PORT_FREERUN_COUNTER is a 32-bit free-running counter whose
 * upper TICK_BITS naturally map to the tick field and lower fine_bits
 * to the fine field. Single atomic read — no DMB, no packing.
 */

#define TRAX_HW_PORT_TIMEPACKED_GET32() \
    (TRAX_HW_PORT_FREERUN_COUNTER)

#define TRAX_HW_PORT_TIMEPACKED_PUT32(p_wr) \
    do { *p_wr++ = TRAX_HW_PORT_FREERUN_COUNTER; } while(0)

#else /* TRAX_TIMESTAMP_TICK_TIMER */

/* ─── TICK_TIMER MODE ────────────────────────────────────────────────
 * Software tick counter (coarse) + timer register (fine), packed into
 * [tick:TICK_BITS][fine:(32-TICK_BITS)]. Raw timer value is stored as-is
 * (the host normalizes direction if DOWN).
 *
 * Tick-boundary race
 * ──────────────────
 * Reading tick_cntr and the fine timer register is not atomic. If the
 * timer wraps between the two reads (the SysTick race is the canonical
 * case), the captured tick bits are stale and the reconstructed time
 * lands ~one full tick period in the past.
 *
 * Earlier versions tried to compensate here using PENDSTSET / a generic
 * "is the timer IRQ pending?" probe, but that probe is unreliable: the
 * NVIC clears PENDSTSET when the SysTick handler is entered, so reads
 * taken from inside the SysTick handler (which is exactly where the
 * race fires for that timer) cannot detect the pending wrap. The
 * compensation now lives on the host, in MessageDecoder, where it
 * detects backwards reconstructed time per core and nudges the
 * offending frame forward by an integer number of tick periods. That
 * means this macro can stay simple and the firmware no longer needs
 * an "IRQ pending" predicate at all.
 *
 * The __DMB() is kept so the two reads are not reordered with each
 * other or with surrounding stores in the trace path — preserving the
 * tick→fine ordering that the host's per-core monotonicity check
 * relies on.
 */

/**
 * @brief Get current timestamp without writing to buffer
 * @return uint32_t Timestamp value [tick:TICK_BITS][fine:(32-TICK_BITS)]
 *
 * Implemented as a static inline function, NOT a GNU statement expression
 * (`({ ... })`): statement expressions are a GCC/Clang extension that IAR,
 * Keil AC5, TI and MSVC reject outright. An always-inlined function gives
 * identical code on every optimization level while staying standard C.
 */
static inline uint32_t trax_hw_port_timepacked_get32(void)
{
    uint32_t tick_cntr = trax_timebase.tick_cntr;
    /* Keep the tick read ordered before the fine-timer read — the host's
     * per-core monotonicity check relies on tick-then-fine capture order. */
    __DMB();
    uint32_t timer_val = TRAX_CFG_TIMESTAMP_TIMER_VAL;
    return (tick_cntr << TRAX_TIMESTAMP_TICK_SHIFT) |
           (timer_val & TRAX_TIMESTAMP_VAL_MASK);
}

#define TRAX_HW_PORT_TIMEPACKED_GET32()  trax_hw_port_timepacked_get32()

/**
 * @brief Write current timestamp to buffer pointer
 * @param p_wr Pointer to write location (will be incremented)
 */
#define TRAX_HW_PORT_TIMEPACKED_PUT32(p_wr) \
    do { *p_wr++ = trax_hw_port_timepacked_get32(); } while(0)

#endif /* TRAX_CFG_TIMESTAMP_MODE */

#ifdef __cplusplus
}
#endif

#endif /* TRAX_HW_PORT_H_ */
