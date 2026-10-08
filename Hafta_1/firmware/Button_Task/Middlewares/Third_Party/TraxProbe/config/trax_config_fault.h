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
 * @file           : trax_config_fault.h
 * @brief          : TraxFault (crash / post-mortem) Module Configuration
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 *
 * TraxFault captures the CPU state at a HardFault (registers, fault-status
 * registers, RTOS task) into a RAM region that survives a system reset, and
 * ships it to Traxcope after the reboot, together with the normalized reset
 * reason of every boot.  See inc/trax_fault.h for the full model.
 *
 * REAL-TIME COST: ZERO on the instrumentation hot path.  TraxFault code
 * runs only (a) inside the fault handler — the system is already dead —
 * and (b) once at boot / session start.  No hooks, callbacks, or
 * conditionals are added to any frame-emit path.
 *
 ******************************************************************************
 */

#ifndef TRAX_CONFIG_FAULT_H_
#define TRAX_CONFIG_FAULT_H_

/*=============================================================================
 ====================MODULE ENABLE=============================================
 ============================================================================*/

/**
 * @brief Master switch for the TraxFault module (default: OFF, opt-in).
 *
 * Disabled by default because enabling it makes the hardware port define
 * fault handlers.  On Cortex-M those are HardFault_Handler et al., which
 * collide with the non-weak handlers STM32CubeMX generates into
 * stm32xxxx_it.c — remove those empty handlers (or set
 * TRAX_CFG_FAULT_HANDLERS 0) when opting in.  On Zynq the handlers are
 * exported under TraxProbe-specific names and collide with nothing, but you
 * must route the BSP's abort vectors to them yourself.
 *
 * When 0: every TraxFault translation unit compiles to nothing and the
 * public API becomes inline no-op stubs — zero flash / RAM cost.
 *
 * Override in trax_config.h:
 *   #define TRAX_CFG_FAULT_ENABLE  1
 */
#ifndef TRAX_CFG_FAULT_ENABLE
#define TRAX_CFG_FAULT_ENABLE   0
#endif

/*=============================================================================
 ====================FAULT HANDLER INSTALLATION================================
 ============================================================================*/

/**
 * @brief Let TraxFault define the port's fault handlers (default: 1).
 *
 * ARM_Cortex_M: defines HardFault_Handler — and on Mainline cores
 * (M3/M4/M7/M33/...) also MemManage_Handler, BusFault_Handler and
 * UsageFault_Handler — as naked capture shims.  The vector table picks
 * them up by name (the startup-file defaults are weak).
 *
 * Zynq (Cortex-A9 / Cortex-R5): defines trax_fault_data_abort_shim,
 * trax_fault_prefetch_abort_shim and trax_fault_undef_shim.  These are
 * NOT picked up automatically — the Xilinx BSP owns the vector table, so
 * you point it at them (see hw_port/Zynq/README.md).
 *
 * Set to 0 to keep your own handlers and call the port's capture function
 * from them yourself (trax_fault_capture on Cortex-M,
 * trax_fault_capture_armv7 on Zynq — see trax_fault.h).
 *
 * Override in trax_config.h:
 *   #define TRAX_CFG_FAULT_HANDLERS  0
 */
#ifndef TRAX_CFG_FAULT_HANDLERS
#define TRAX_CFG_FAULT_HANDLERS  1
#endif

/*=============================================================================
 ====================PERSISTENT RECORD PLACEMENT===============================
 ============================================================================*/

/**
 * @brief Name of the linker section holding the persistent fault record.
 *
 * The section must NOT be zero-initialized at startup (NOLOAD, outside the
 * .bss zero-fill range) so the record written by the fault handler survives
 * the reset that follows it.  The TraxProbe linker fragment
 * (linker/trax_probe.ld, the same single -T entry that places the metadata
 * sections) already provides it:
 *
 *   .trax_noinit (NOLOAD) : { KEEP(*(.trax_noinit)) } >RAM
 *
 * Placement guidance: keep the section in a RAM region that is powered in
 * your normal run mode and not wiped by your bootloader.  On parts with
 * multiple RAM banks (e.g. STM32H7) any always-on AXI/AHB SRAM or DTCM
 * works — the record is only touched at fault time and boot time, so its
 * access latency is irrelevant to runtime performance.
 *
 * Override in trax_config.h:
 *   #define TRAX_CFG_FAULT_SECTION  ".noinit"
 */
#ifndef TRAX_CFG_FAULT_SECTION
#define TRAX_CFG_FAULT_SECTION  ".trax_noinit"
#endif

/*=============================================================================
 ====================STACK SNAPSHOT (Phase 2)==================================
 ============================================================================*/

/**
 * @brief Bytes of the faulting stack copied into the persistent record
 *        (default: 512, 0 disables).
 *
 * The snapshot starts at the faulting SP (the exception frame is its first
 * 32 bytes) and walks UP toward the stack base — that is where the caller
 * frames and return addresses live, which is what the host's heuristic
 * backtrace scans.  Cost: this many bytes in TRAX_CFG_FAULT_SECTION plus
 * the same again for the boot-time RAM cache; copied only inside the
 * fault handler.  Must be a multiple of 4.
 *
 * Override in trax_config.h:
 *   #define TRAX_CFG_FAULT_STACK_SNAPSHOT  256
 */
#ifndef TRAX_CFG_FAULT_STACK_SNAPSHOT
#define TRAX_CFG_FAULT_STACK_SNAPSHOT  512
#endif

/**
 * @brief Exclusive end address of the RAM holding the stacks (default: 0 =
 *        auto-detect).
 *
 * The snapshot copy must never read past the end of physical RAM — that
 * would bus-fault INSIDE the fault handler, which at HardFault priority
 * means lockup (no record, no reset).  This matters in practice: a fault
 * early in main() runs with MSP only a few hundred bytes below the top
 * of RAM.
 *
 * Auto-detect (0) reads the initial MSP from slot 0 of the active vector
 * table (SCB->VTOR), which by convention is the top of stack RAM
 * (_estack).  Set this explicitly only when your stacks live in a RAM
 * bank ABOVE the initial MSP (separate stack bank, external RAM).
 *
 * Override in trax_config.h:
 *   #define TRAX_CFG_FAULT_RAM_END  0x20024000u
 */
#ifndef TRAX_CFG_FAULT_RAM_END
#define TRAX_CFG_FAULT_RAM_END  0u
#endif

/*=============================================================================
 ====================FLIGHT RECORDER (Phase 3)=================================
 ============================================================================*/

/**
 * @brief Bytes of pre-crash trace history preserved across the reset
 *        (default: 1024, 0 disables).
 *
 * At fault time the handler walks the trace ring and copies the NEWEST
 * committed frames — the ones the host had not yet received — into a
 * persistent noinit buffer.  On the first session start after the crash
 * they are replayed between TRAX_TID_FAULT_FLIGHT_BEGIN/END brackets, so
 * Traxcope can stitch the very last pre-crash events onto the history it
 * already streamed live.
 *
 * Cost: this many bytes + 16 in TRAX_CFG_FAULT_SECTION.  Copied only
 * inside the fault handler; zero hot-path cost.  Must be a multiple of 4.
 *
 * Sizing note: while the host is connected the ring drains continuously,
 * so the undrained tail is usually small — the value here is a CAP, not
 * a guarantee.  It pays off exactly when the transport was slow or
 * congested in the final moments (which correlates with crashes).
 *
 * Override in trax_config.h:
 *   #define TRAX_CFG_FAULT_TRACE_SNAPSHOT  2048
 */
#ifndef TRAX_CFG_FAULT_TRACE_SNAPSHOT
#define TRAX_CFG_FAULT_TRACE_SNAPSHOT  1024
#endif

/**
 * @brief Payload bytes per TRAX_TID_FAULT_FLIGHT_DATA replay frame
 *        (default: 512).  Must be a multiple of 4.
 *
 * The whole replay (BEGIN + DATA chunks + END) is enqueued into the new
 * session's ring right behind the fault record, so the chunk size only
 * matters for framing overhead — but the TOTAL must fit the ring, which
 * the validation below enforces.
 */
#ifndef TRAX_CFG_FAULT_FLIGHT_CHUNK
#define TRAX_CFG_FAULT_FLIGHT_CHUNK  512
#endif

/*=============================================================================
 ====================DYNAMIC METADATA SNAPSHOT (Phase 3)=======================
 ============================================================================*/

/**
 * @brief RTOS TASK table entries preserved across the reset (default: 16
 *        on RTOS builds; forced 0 on bare metal; 0 disables).
 *
 * At fault time the handler copies the live task table (handles, names,
 * priorities, stack sizes, latched overflow breadcrumbs) into a persistent
 * noinit block, and the first session start after the crash ships it as
 * TRAX_TID_FAULT_DYNMETA_TASKS.  This makes the crash history's task-lane
 * reconstruction exact for the CRASHED run — including tasks created
 * dynamically mid-run that no longer exist after the reboot.
 *
 * CAPACITY ACCOUNTING: this value is a cap, not a guarantee.  When the
 * live table holds more entries than fit, the snapshot keeps the first
 * entries and the wire frame reports BOTH counts (total vs stored) —
 * Traxcope shows a truncation warning naming this option so you know to
 * raise it.  Match it to your TRAX_CFG_MAX_RTOS_TASKS to never truncate.
 *
 * Cost: entries × 40 bytes in TRAX_CFG_FAULT_SECTION (16 → 640 bytes).
 * Written only inside the fault handler; zero hot-path cost.
 *
 * Override in trax_config.h:
 *   #define TRAX_CFG_FAULT_DYNMETA_TASKS  8
 */
#if (TRAX_CFG_RTOS_TYPE == TRAX_RTOS_NONE)
/* Bare metal has no task/object tables — force the snapshot off so it
 * costs nothing (any user override would only capture empty tables). */
#undef  TRAX_CFG_FAULT_DYNMETA_TASKS
#define TRAX_CFG_FAULT_DYNMETA_TASKS    0
#undef  TRAX_CFG_FAULT_DYNMETA_OBJECTS
#define TRAX_CFG_FAULT_DYNMETA_OBJECTS  0
#else
#ifndef TRAX_CFG_FAULT_DYNMETA_TASKS
#define TRAX_CFG_FAULT_DYNMETA_TASKS    16
#endif

/**
 * @brief RTOS OBJECT table entries (queues / semaphores / mutexes)
 *        preserved across the reset (default: 16 on RTOS builds; forced 0
 *        on bare metal; 0 disables).  Same capacity accounting as
 *        TRAX_CFG_FAULT_DYNMETA_TASKS — truncation is reported on the
 *        wire and surfaced in Traxcope.
 *
 * Cost: entries × 32 bytes in TRAX_CFG_FAULT_SECTION (16 → 512 bytes).
 *
 * Override in trax_config.h:
 *   #define TRAX_CFG_FAULT_DYNMETA_OBJECTS  8
 */
#ifndef TRAX_CFG_FAULT_DYNMETA_OBJECTS
#define TRAX_CFG_FAULT_DYNMETA_OBJECTS  16
#endif
#endif /* TRAX_CFG_RTOS_TYPE == TRAX_RTOS_NONE */

/** @brief Convenience: nonzero when the dynmeta snapshot is compiled in. */
#define TRAX_CFG_FAULT_DYNMETA \
	(TRAX_CFG_FAULT_DYNMETA_TASKS > 0 || TRAX_CFG_FAULT_DYNMETA_OBJECTS > 0)

/*=============================================================================
 ====================NOINIT MEMORY BUDGET======================================
 ============================================================================*/

/**
 * @brief Total bytes TraxFault places in TRAX_CFG_FAULT_SECTION, computed
 *        from the configuration above.  READ-ONLY — do not override.
 *
 * Three CRC-sealed blocks, all packed and word-multiple (so the linker
 * inserts no alignment gaps between them):
 *
 *   fault record   164-byte head + TRAX_CFG_FAULT_STACK_SNAPSHOT
 *   flight buffer  16-byte head + TRAX_CFG_FAULT_TRACE_SNAPSHOT   (when > 0)
 *   dynmeta        16-byte head + TASKS × 40 B + OBJECTS × 32 B   (when > 0)
 *
 * Defaults: 164+512 + 16+1024 + 16+640+512 = 2884 bytes.
 *
 * The literals are cross-checked against the real struct sizes by static
 * asserts in src/trax_fault.c — if the wire format grows, the build
 * breaks there until this math is updated in lockstep.
 *
 * Size your linker region with this value (map-file symbol audit:
 * trax_fault_record / trax_fault_flight / trax_fault_dynmeta).
 */
#define TRAX_FAULT_NOINIT_TOTAL \
	((164 + TRAX_CFG_FAULT_STACK_SNAPSHOT) \
	 + ((TRAX_CFG_FAULT_TRACE_SNAPSHOT) > 0 \
	        ? (16 + TRAX_CFG_FAULT_TRACE_SNAPSHOT) : 0) \
	 + ((TRAX_CFG_FAULT_DYNMETA) \
	        ? (16 + (TRAX_CFG_FAULT_DYNMETA_TASKS) * 40 \
	              + (TRAX_CFG_FAULT_DYNMETA_OBJECTS) * 32) : 0))

/**
 * @brief Hard budget for the crash-dump RAM — REQUIRED when
 *        TRAX_CFG_FAULT_ENABLE is 1.
 *
 * Set this to the byte size of the RAM you dedicate to
 * TRAX_CFG_FAULT_SECTION.  The build FAILS (with the actual total in
 * the error) whenever the configured snapshots no longer fit — e.g.
 * after raising TRAX_CFG_FAULT_DYNMETA_TASKS or the flight buffer.
 *
 * The `.trax_noinit` output section in linker/trax_probe.ld is
 * auto-sized, so the linker never needs this number — the budget exists
 * to make the RAM reservation an EXPLICIT, reviewed decision in
 * trax_config.h and to catch silent config growth at compile time.
 * Size it with TRAX_FAULT_NOINIT_TOTAL (above) as the minimum; if the
 * section maps to a fixed region (MEMORY carve-out, bootloader-shared
 * block, battery-backed SRAM), use that region's size instead.
 *
 * Set in trax_config.h (mandatory with the fault module on):
 *   #define TRAX_CFG_FAULT_NOINIT_MAX  3072
 */
#ifndef TRAX_CFG_FAULT_NOINIT_MAX
#define TRAX_CFG_FAULT_NOINIT_MAX  0
#endif

/*=============================================================================
 ====================VALIDATION================================================
 ============================================================================*/

#if (TRAX_CFG_FAULT_NOINIT_MAX < 0)
#error "TRAX_CFG_FAULT_NOINIT_MAX must be >= 0"
#endif

#if TRAX_CFG_FAULT_ENABLE && (TRAX_CFG_FAULT_NOINIT_MAX <= 0)
#error "TraxFault is enabled but no crash-dump RAM budget is declared: \
add  #define TRAX_CFG_FAULT_NOINIT_MAX <bytes>  to trax_config.h, with \
<bytes> >= TRAX_FAULT_NOINIT_TOTAL (see the math above in \
trax_config_fault.h). This keeps the .trax_noinit RAM reservation an \
explicit decision and turns config growth into a compile error."
#endif

#if (TRAX_CFG_FAULT_NOINIT_MAX > 0) && \
    (TRAX_FAULT_NOINIT_TOTAL > TRAX_CFG_FAULT_NOINIT_MAX)
#error "TraxFault noinit footprint exceeds TRAX_CFG_FAULT_NOINIT_MAX: \
shrink TRAX_CFG_FAULT_STACK_SNAPSHOT / TRAX_CFG_FAULT_TRACE_SNAPSHOT / \
TRAX_CFG_FAULT_DYNMETA_TASKS / TRAX_CFG_FAULT_DYNMETA_OBJECTS, or grow \
the reserved region and raise TRAX_CFG_FAULT_NOINIT_MAX (see \
TRAX_FAULT_NOINIT_TOTAL in trax_config_fault.h for the math)"
#endif

#if (TRAX_CFG_FAULT_TRACE_SNAPSHOT < 0) || (TRAX_CFG_FAULT_TRACE_SNAPSHOT > 8192)
#error "TRAX_CFG_FAULT_TRACE_SNAPSHOT must be 0..8192 bytes"
#endif

#if (TRAX_CFG_FAULT_TRACE_SNAPSHOT % 4) != 0
#error "TRAX_CFG_FAULT_TRACE_SNAPSHOT must be a multiple of 4"
#endif

#if (TRAX_CFG_FAULT_FLIGHT_CHUNK < 64) || (TRAX_CFG_FAULT_FLIGHT_CHUNK > 4096) \
    || (TRAX_CFG_FAULT_FLIGHT_CHUNK % 4) != 0
#error "TRAX_CFG_FAULT_FLIGHT_CHUNK must be 64..4096 and a multiple of 4"
#endif

#if (TRAX_CFG_FAULT_STACK_SNAPSHOT < 0) || (TRAX_CFG_FAULT_STACK_SNAPSHOT > 4096)
#error "TRAX_CFG_FAULT_STACK_SNAPSHOT must be 0..4096 bytes"
#endif

#if (TRAX_CFG_FAULT_STACK_SNAPSHOT % 4) != 0
#error "TRAX_CFG_FAULT_STACK_SNAPSHOT must be a multiple of 4"
#endif

#if (TRAX_CFG_FAULT_DYNMETA_TASKS < 0) || (TRAX_CFG_FAULT_DYNMETA_TASKS > 64)
#error "TRAX_CFG_FAULT_DYNMETA_TASKS must be 0..64 entries"
#endif

#if (TRAX_CFG_FAULT_DYNMETA_OBJECTS < 0) || (TRAX_CFG_FAULT_DYNMETA_OBJECTS > 64)
#error "TRAX_CFG_FAULT_DYNMETA_OBJECTS must be 0..64 entries"
#endif

#if (TRAX_CFG_FAULT_ENABLE != 0) && (TRAX_CFG_FAULT_ENABLE != 1)
#error "TRAX_CFG_FAULT_ENABLE must be 0 or 1"
#endif

#if (TRAX_CFG_FAULT_HANDLERS != 0) && (TRAX_CFG_FAULT_HANDLERS != 1)
#error "TRAX_CFG_FAULT_HANDLERS must be 0 or 1"
#endif

#endif /* TRAX_CONFIG_FAULT_H_ */
