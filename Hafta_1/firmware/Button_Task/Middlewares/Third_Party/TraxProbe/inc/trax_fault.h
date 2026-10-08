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
 * @file           : trax_fault.h
 * @brief          : TraxFault — crash capture & post-mortem across resets
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 *
 * MODEL
 * -----
 * 1. FAULT TIME  — a fault handler supplied by the hardware port (when
 *    TRAX_CFG_FAULT_HANDLERS is 1) captures the CPU registers, the ARM
 *    fault-status registers, the current RTOS task name, and a timestamp
 *    into a magic-tagged, CRC-protected record placed in the
 *    TRAX_CFG_FAULT_SECTION linker section (default ".trax_noinit").  The
 *    handler then requests a system reset.  The section is NOT
 *    zero-initialized at startup, so the record survives the reboot.
 *
 *    Which handler that is depends on the port:
 *      ARM_Cortex_M — HardFault, plus MemManage/BusFault/UsageFault on
 *                     Mainline cores. Status from CFSR/HFSR/MMFAR/BFAR.
 *      Zynq (A9/R5) — Data Abort, Prefetch Abort, Undefined Instruction.
 *                     Status from CP15 DFSR/IFSR/DFAR/IFAR, carried in the
 *                     same record slots (see enum trax_fault_kind_t).
 *                     The vectors are NOT installed for you there — the
 *                     Xilinx BSP owns the vector table.
 *
 * 2. BOOT TIME   — trax_fault_init() (called from trax_init()) validates
 *    the record (magic + version + CRC), caches it in normal RAM, and
 *    invalidates the persistent copy so the slot is free for the next
 *    fault.  It also latches the reset reason via the
 *    trax_fault_port_reset_reason() hook.
 *
 * 3. SESSION START — after every SESSION_START frame the library emits
 *    TRAX_TID_FAULT_RESET_REASON; if a fault record was recovered at
 *    boot it is emitted once as TRAX_TID_FAULT_RECORD.  Traxcope decodes
 *    the status words bit by bit — as CFSR/HFSR or as DFSR/IFSR, keyed on
 *    fault_kind — and symbolizes PC/LR against the ELF.
 *
 * REAL-TIME COST: zero on the instrumentation hot path — the module runs
 * only inside the fault handler and at boot / session start.
 *
 * INTEGRATION CHECKLIST
 * ---------------------
 *   trax_config.h:    #define TRAX_CFG_FAULT_ENABLE 1
 *                     #define TRAX_CFG_FAULT_NOINIT_MAX <bytes>  (RAM budget,
 *                     >= TRAX_FAULT_NOINIT_TOTAL — build fails otherwise)
 *   Linker script:    nothing extra — the standard TraxProbe fragment
 *                     (linker/trax_probe.ld, already on the link line for
 *                     the metadata sections) provides .trax_noinit
 *   CubeMX users:     delete the generated empty HardFault_Handler et al.
 *                     from stm32xxxx_it.c (or set TRAX_CFG_FAULT_HANDLERS 0
 *                     and call trax_fault_capture() from your own shim)
 *   Zynq users:       route the BSP's abort vectors to the port's
 *                     trax_fault_*_shim symbols, and confirm your boot flow
 *                     leaves .trax_noinit intact — see hw_port/Zynq/README.md
 *   Reset reason:     optionally implement trax_fault_port_reset_reason()
 *                     (vendor-specific register; weak default = UNKNOWN)
 *
 ******************************************************************************
 */

#ifndef TRAX_FAULT_H_
#define TRAX_FAULT_H_

#include <stdint.h>
#include "trax_compiler.h"
#include "trax_config_default.h"
#if TRAX_CFG_FAULT_DYNMETA
#include "trax_meta_type.h"   /* trax_rtos_task_wire_t / trax_rtos_object_wire_t */
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 ====================WIRE FORMAT CONSTANTS=====================================
 ============================================================================*/

/** @brief Magic tagging a valid persistent fault record ("TXFT"). */
#define TRAX_FAULT_MAGIC          0x54465854u

/** @brief Wire format version carried as param[0] of TRAX_TID_FAULT_RECORD.
 *         v2: stack snapshot fields + variable-length stack tail appended.
 *         v3: RTOS context (tick count, scheduler state, task priority). */
#define TRAX_FAULT_WIRE_VERSION   3u

/** @brief Fixed size of the RTOS task-name copy inside the record. */
#define TRAX_FAULT_TASK_NAME_LEN  16u

/** @brief Size of the fixed (stack-less) part of the record — the wire
 *         frame carries this head plus stack_used snapshot bytes. */
#define TRAX_FAULT_RECORD_HEAD_SIZE  164u

/** @brief Magic tagging a valid flight-recorder snapshot ("TXFL"). */
#define TRAX_FAULT_FLIGHT_MAGIC   0x4C465854u

/** @brief Magic tagging a valid dynamic-metadata snapshot ("TXDM"). */
#define TRAX_FAULT_DYNMETA_MAGIC  0x4D445854u

/*=============================================================================
 ====================GLOBAL TYPEDEF============================================
 ============================================================================*/

/**
 * @brief Which fault handler captured the record.
 *
 * On Cortex-M this is derived from ICSR.VECTACTIVE at capture time, so the
 * value is correct even when all four handlers funnel into the same capture
 * function or when a configurable fault escalated to HardFault.
 *
 * The value ALSO tells the host which architecture wrote the record, which
 * is what lets one 164-byte wire format serve both families: kinds 1-4 are
 * Cortex-M, kinds 5-7 are Cortex-A/R (ARMv7-A/R). On an A/R record the
 * architecture-named fields carry their A/R counterparts:
 *
 *   cfsr  -> DFSR  (Data Fault Status,       CP15 c5,c0,0)
 *   hfsr  -> IFSR  (Instruction Fault Status,CP15 c5,c0,1)
 *   mmfar -> DFAR  (Data Fault Address,      CP15 c6,c0,0)
 *   bfar  -> IFAR  (Instruction Fault Addr,  CP15 c6,c0,2)
 *   xpsr  -> SPSR of the aborted context (i.e. its CPSR)
 *   exc_return -> CPSR.M[4:0] of the aborted mode (no EXC_RETURN on A/R)
 *   icsr / afsr / shcsr / msp / psp -> 0 (no architectural equivalent)
 *
 * r[0..12], sp, lr and pc are the real faulting values on A/R — better
 * fidelity than Cortex-M, where R0-R3/R12 come from the stacked frame.
 */
enum trax_fault_kind_t {
	TRAX_FAULT_KIND_UNKNOWN    = 0,
	/* --- Cortex-M --- */
	TRAX_FAULT_KIND_HARDFAULT  = 1,
	TRAX_FAULT_KIND_MEMMANAGE  = 2,
	TRAX_FAULT_KIND_BUSFAULT   = 3,
	TRAX_FAULT_KIND_USAGEFAULT = 4,
	/* --- Cortex-A/R (ARMv7-A/R): see the field remapping above --- */
	TRAX_FAULT_KIND_DATA_ABORT     = 5,
	TRAX_FAULT_KIND_PREFETCH_ABORT = 6,
	TRAX_FAULT_KIND_UNDEF_INSTR    = 7,
};

/**
 * @brief Normalized reset reason, param[0] of TRAX_TID_FAULT_RESET_REASON.
 *
 * Vendor reset-status registers differ; the port hook maps them onto this
 * common enum and also passes the raw register value through as param[1]
 * so nothing is lost.
 */
enum trax_fault_reset_reason_t {
	TRAX_RESET_REASON_UNKNOWN   = 0,
	TRAX_RESET_REASON_POWER_ON  = 1,  /**< Power-on reset */
	TRAX_RESET_REASON_PIN       = 2,  /**< External reset pin (NRST) */
	TRAX_RESET_REASON_BROWNOUT  = 3,  /**< Brown-out reset */
	TRAX_RESET_REASON_SOFTWARE  = 4,  /**< NVIC_SystemReset / SYSRESETREQ */
	TRAX_RESET_REASON_IWDG      = 5,  /**< Independent watchdog timeout */
	TRAX_RESET_REASON_WWDG      = 6,  /**< Window watchdog */
	TRAX_RESET_REASON_LOW_POWER = 7,  /**< Illegal low-power mode entry */
	TRAX_RESET_REASON_LOCKUP    = 8,  /**< Core lockup (double fault) */
};

/**
 * @brief Validity flags for optional record fields (record.flags bits).
 *
 * Lets the host degrade gracefully: an ARMv6-M capture (Cortex-M0/M0+/M23)
 * has no CFSR/HFSR/MMFAR/BFAR, a pre-scheduler fault has no task name, and
 * a fault with a corrupted SP cannot provide the stacked frame.
 */
#define TRAX_FAULT_FLAG_FAULT_REGS     0x01u  /**< cfsr..shcsr fields valid (ARMv7-M+) */
#define TRAX_FAULT_FLAG_STACKED_FRAME  0x02u  /**< r0-r3, r12, lr, pc, xpsr read from stack */
#define TRAX_FAULT_FLAG_TASK_NAME      0x04u  /**< task_name[] valid (RTOS running) */
#define TRAX_FAULT_FLAG_STACK          0x08u  /**< stack[] snapshot valid (stack_used bytes) */
#define TRAX_FAULT_FLAG_RTOS_CTX       0x10u  /**< tick_count/sched_state/task_prio valid */

/** @brief record.sched_state values (RTOS-agnostic mapping). */
#define TRAX_FAULT_SCHED_NONE          0u  /**< bare metal — no scheduler compiled in */
#define TRAX_FAULT_SCHED_NOT_STARTED   1u  /**< RTOS present, scheduler not started yet */
#define TRAX_FAULT_SCHED_RUNNING       2u  /**< scheduler running normally */
#define TRAX_FAULT_SCHED_SUSPENDED     3u  /**< scheduler suspended (critical section) */

/**
 * @brief Persistent fault record — written by the fault handler, shipped
 *        as the TRAX_TID_FAULT_RECORD frame payload (the fixed head plus
 *        stack_used snapshot bytes; unused stack capacity stays home).
 *
 * Lives in TRAX_CFG_FAULT_SECTION so it survives the post-fault reset.
 * All fields little-endian on the wire (Cortex-M native).  The CRC covers
 * every byte AFTER the crc field itself (i.e. offset 8 .. end), including
 * the full stack buffer (unused bytes are zeroed at capture).
 *
 * Mirrored on the host in probe/protocol/TraxProtocol.h (WireFaultRecord).
 * Grow only by appending fields AND bumping TRAX_FAULT_WIRE_VERSION.
 */
struct trax_fault_record_t {
	uint32_t magic;        /**< TRAX_FAULT_MAGIC when the record is valid */
	uint32_t crc;          /**< CRC-32 (reflected, poly 0xEDB88320) of bytes 8..sizeof-1 */
	uint8_t  version;      /**< TRAX_FAULT_WIRE_VERSION */
	uint8_t  fault_kind;   /**< enum trax_fault_kind_t */
	uint8_t  flags;        /**< TRAX_FAULT_FLAG_* validity bits */
	uint8_t  core_id;      /**< Faulting core (TRAX_PORT_GET_CORE_ID) */
	uint32_t timepacked;   /**< TRAX timestamp at capture (same encoding as frames) */
	uint32_t session_id;   /**< trax_session.session_id of the crashed run */
	uint32_t r[13];        /**< R0-R12 at fault (R0-R3/R12 from stacked frame) */
	uint32_t sp;           /**< Faulting stack pointer (MSP or PSP per EXC_RETURN) */
	uint32_t lr;           /**< Stacked LR (return address of faulting context) */
	uint32_t pc;           /**< Stacked PC (faulting instruction) */
	uint32_t xpsr;         /**< Stacked xPSR */
	uint32_t exc_return;   /**< EXC_RETURN of the fault handler entry */
	uint32_t msp;          /**< MSP at fault */
	uint32_t psp;          /**< PSP at fault */
	uint32_t icsr;         /**< SCB->ICSR (VECTACTIVE = active exception) */
	uint32_t cfsr;         /**< SCB->CFSR  (0 on ARMv6-M — see flags) */
	uint32_t hfsr;         /**< SCB->HFSR  (0 on ARMv6-M) */
	uint32_t dfsr;         /**< SCB->DFSR  (0 on ARMv6-M) */
	uint32_t mmfar;        /**< SCB->MMFAR (0 on ARMv6-M) */
	uint32_t bfar;         /**< SCB->BFAR  (0 on ARMv6-M) */
	uint32_t afsr;         /**< SCB->AFSR  (0 on ARMv6-M) */
	uint32_t shcsr;        /**< SCB->SHCSR (0 on ARMv6-M) */
	char     task_name[TRAX_FAULT_TASK_NAME_LEN]; /**< Current task at fault (NUL-padded) */
	/* ---- v2 fields (stack snapshot) ---- */
	uint32_t stack_addr;   /**< RAM address of stack[0] (== sp when captured) */
	uint16_t stack_used;   /**< Valid bytes in stack[] (multiple of 4, may be 0) */
	uint16_t stack_cap;    /**< TRAX_CFG_FAULT_STACK_SNAPSHOT of the crashed build */
	/* ---- v3 fields (RTOS context) ---- */
	uint32_t tick_count;   /**< RTOS tick count at capture (0 on bare metal) */
	uint8_t  sched_state;  /**< TRAX_FAULT_SCHED_* at capture */
	uint8_t  task_prio;    /**< Priority of the faulting task */
	uint16_t rtos_rsvd;    /**< Reserved (alignment / future growth) */
#if TRAX_CFG_FAULT_STACK_SNAPSHOT > 0
	uint8_t  stack[TRAX_CFG_FAULT_STACK_SNAPSHOT]; /**< Snapshot from sp upward
	                        (exception frame first, then caller frames).
	                        Only stack_used bytes travel on the wire.
	                        MUST stay the last field: the wire frame is
	                        head + used stack bytes. */
#endif
} TRAX_PACKED;

TRAX_STATIC_ASSERT(sizeof(struct trax_fault_record_t) ==
                   (TRAX_FAULT_RECORD_HEAD_SIZE + TRAX_CFG_FAULT_STACK_SNAPSHOT),
                   "trax_fault_record_t is wire format v3 = 164-byte head + "
                   "stack snapshot; growth requires a TRAX_FAULT_WIRE_VERSION "
                   "bump and a host WireFaultRecord update in lockstep");

#if TRAX_CFG_FAULT_TRACE_SNAPSHOT > 0
/**
 * @brief Flight-recorder snapshot — the newest committed-but-undrained
 *        frames copied out of the trace ring by the fault handler.
 *
 * These are exactly the frames the host never received: everything older
 * was already streamed live before the crash.  Lives in
 * TRAX_CFG_FAULT_SECTION next to the fault record; replayed once between
 * TRAX_TID_FAULT_FLIGHT_BEGIN/END on the first session start after the
 * crash, then invalidated.  data[] holds whole wire frames (Word-0
 * headers included) in oldest-to-newest order, so the host re-parses
 * them with its normal frame parser.
 */
struct trax_fault_flight_t {
	uint32_t magic;        /**< TRAX_FAULT_FLIGHT_MAGIC when valid */
	uint32_t crc;          /**< CRC-32 of bytes 8..sizeof-1 (same poly as record) */
	uint32_t session_id;   /**< Session of the crashed run (pairs with record) */
	uint16_t used;         /**< Valid bytes in data[] (multiple of 4) */
	uint16_t rsvd;
	uint8_t  data[TRAX_CFG_FAULT_TRACE_SNAPSHOT];
} TRAX_PACKED;
#endif

#if TRAX_CFG_FAULT_DYNMETA
/**
 * @brief Dynamic-metadata snapshot — the crashed run's RTOS task and
 *        object tables, copied by the fault handler so the host can
 *        decode the flight recorder's kernel frames with the CRASHED
 *        run's exact handle→name/priority/stack map (not the next
 *        boot's — which misses tasks created dynamically mid-run).
 *
 * Lives in TRAX_CFG_FAULT_SECTION next to the record; emitted once as
 * TRAX_TID_FAULT_DYNMETA_TASKS / _OBJS on the first session start after
 * the crash, then invalidated.
 *
 * CAPACITY ACCOUNTING: task_total / obj_total are the LIVE table counts
 * at fault time; task_stored / obj_stored what actually fit the
 * TRAX_CFG_FAULT_DYNMETA_TASKS / _OBJECTS caps.  stored < total ⇒ the
 * snapshot was truncated — both counts travel on the wire so Traxcope
 * can tell the user which config option to raise.
 */
struct trax_fault_dynmeta_t {
	uint32_t magic;        /**< TRAX_FAULT_DYNMETA_MAGIC when valid */
	uint32_t crc;          /**< CRC-32 of bytes 8..sizeof-1 (same poly as record) */
	uint32_t session_id;   /**< Session of the crashed run (pairs with record) */
	uint8_t  task_total;   /**< Live task-table count at fault time */
	uint8_t  task_stored;  /**< Entries captured into tasks[] */
	uint8_t  obj_total;    /**< Live object-table count at fault time */
	uint8_t  obj_stored;   /**< Entries captured into objects[] */
#if TRAX_CFG_FAULT_DYNMETA_TASKS > 0
	struct trax_rtos_task_wire_t   tasks[TRAX_CFG_FAULT_DYNMETA_TASKS];
#endif
#if TRAX_CFG_FAULT_DYNMETA_OBJECTS > 0
	struct trax_rtos_object_wire_t objects[TRAX_CFG_FAULT_DYNMETA_OBJECTS];
#endif
} TRAX_PACKED;
#endif /* TRAX_CFG_FAULT_DYNMETA */

/*=============================================================================
 ====================GLOBAL FUNCTION DECLARATION===============================
 ============================================================================*/

#if TRAX_ENABLE && TRAX_CFG_FAULT_ENABLE

/**
 * @brief Boot-time recovery. Called from trax_init() — not by the user.
 *
 * Validates + caches a surviving fault record, invalidates the persistent
 * slot, latches the reset reason.  Cheap (one CRC over the record) and
 * runs exactly once per boot.
 */
void trax_fault_init(void);

/**
 * @brief Emit pending post-mortem frames. Called from
 *        trax_meta_tx_send_start() after the SESSION_START frame — not by
 *        the user.
 *
 * Emits TRAX_TID_FAULT_RESET_REASON on every session start, and
 * TRAX_TID_FAULT_RECORD once per boot when a record was recovered.
 * When a dynamic-metadata snapshot paired with the record survived, it
 * is emitted once as TRAX_TID_FAULT_DYNMETA_TASKS / _OBJS (with total
 * vs stored counts so the host can flag truncation).  When a
 * flight-recorder snapshot survived, also emits
 * TRAX_TID_FAULT_FLIGHT_BEGIN and arms the chunked replay (see
 * trax_fault_flight_pump()).
 */
void trax_fault_emit_pending(void);

/**
 * @brief Flight-recorder replay pacer. Called from trax_process() — not
 *        by the user.  No-op unless a replay was armed by
 *        trax_fault_emit_pending().
 *
 * Emits at most ONE TRAX_TID_FAULT_FLIGHT_DATA chunk per call, and only
 * when the ring has comfortable headroom — the pre-crash history must
 * never overflow (and thereby kill) the new session.  After the last
 * chunk it emits TRAX_TID_FAULT_FLIGHT_END and invalidates the
 * persistent snapshot.
 */
void trax_fault_flight_pump(void);

/**
 * @brief Access the fault record recovered at this boot, if any.
 *
 * @return Pointer to the cached record from the PREVIOUS run, or NULL if
 *         the previous run did not end in a captured fault.
 */
const struct trax_fault_record_t *trax_fault_get_last(void);

/**
 * @brief Normalized reset reason of THIS boot (latched in trax_fault_init).
 * @param[out] p_raw Optional: receives the raw reset-status register value.
 */
enum trax_fault_reset_reason_t trax_fault_get_reset_reason(uint32_t *p_raw);

/**
 * @brief Fault-time capture entry — call ONLY from a naked fault-handler
 *        shim (the hw_port provides one when TRAX_CFG_FAULT_HANDLERS is 1).
 *
 * Required shim (both ARMv6-M and ARMv7-M+ encodable; the MSP switch to
 * trax_fault_stack_top makes stack-overflow faults capturable — without it
 * the C prologue would push onto the already-blown stack and lock up):
 *
 *   __attribute__((naked)) void HardFault_Handler(void)
 *   {
 *       __asm volatile(
 *           " ldr  r2, =trax_fault_high_regs \n"
 *           " stmia r2!, {r4-r7}             \n"
 *           " mov  r4, r8                    \n"
 *           " mov  r5, r9                    \n"
 *           " mov  r6, r10                   \n"
 *           " mov  r7, r11                   \n"
 *           " stmia r2!, {r4-r7}             \n"
 *           " mrs  r0, msp                   \n"
 *           " mrs  r1, psp                   \n"
 *           " mov  r2, lr                    \n"
 *           " ldr  r3, =trax_fault_stack_top \n"
 *           " ldr  r3, [r3]                  \n"
 *           " msr  msp, r3                   \n"
 *           " ldr  r3, =trax_fault_capture   \n"
 *           " bx   r3                        \n");
 *   }
 *
 * Fills the persistent record and issues a system reset. Never returns.
 *
 * @param msp        MSP at handler entry
 * @param psp        PSP at handler entry
 * @param exc_return EXC_RETURN value (LR at handler entry)
 */
void trax_fault_capture(uint32_t msp, uint32_t psp, uint32_t exc_return);

/**
 * @brief Fault-time capture entry for ARMv7-A/R (Zynq hw_port).
 *
 * Different architecture, different entry point: Cortex-A/R has no stacked
 * exception frame and no EXC_RETURN, but it DOES leave R0-R12 of the aborted
 * context live in the shared register bank, so the naked shim spills them
 * directly. Provided by hw_port/Zynq/src/trax_fault_port.c; called only from
 * that file's shims (or from your own, if you install the vectors yourself).
 *
 * @param p_regs  Pointer to 13 spilled words: R0..R12 of the aborted context
 * @param spsr    SPSR of the abort/undef mode (== the aborted CPSR)
 * @param lr_abt  Banked LR at handler entry (faulting PC plus the
 *                architectural per-vector offset, removed inside)
 * @param kind    enum trax_fault_kind_t: DATA_ABORT / PREFETCH_ABORT /
 *                UNDEF_INSTR — the shim knows which vector it came from
 *
 * Fills the persistent record and requests a reset. Never returns.
 */
void trax_fault_capture_armv7(const uint32_t *p_regs, uint32_t spsr,
                              uint32_t lr_abt, uint32_t kind);

/**
 * @brief System reset requested after a capture (WEAK, Zynq hw_port).
 *
 * There is no architectural SYSRESETREQ on Cortex-A/R — the reset path is a
 * PS-specific register write — so the default spins forever and relies on
 * your watchdog to reboot. The record is already sealed in noinit RAM by the
 * time this runs, so a watchdog reset loses nothing. Override it in your BSP
 * to reset immediately; hw_port/Zynq/README.md has the Zynq-7000 SLCR and
 * ZynqMP snippets.
 */
void trax_fault_port_system_reset(void);

/**
 * @brief R4-R11 spill slot for the naked shim (see trax_fault_capture()).
 */
extern uint32_t trax_fault_high_regs[8];

/**
 * @brief Top of the dedicated fault-capture stack (provided by the hw_port;
 *        the shim switches MSP here so stack-overflow faults are capturable).
 */
extern const uint32_t trax_fault_stack_top;

/**
 * @brief The persistent record itself (TRAX_CFG_FAULT_SECTION). Exposed for
 *        map-file audits and custom capture paths; do not touch at runtime.
 */
extern struct trax_fault_record_t trax_fault_record;

/**
 * @brief Finalize + seal the persistent record (library-internal; called by
 *        the hw_port capture function after the arch fields are filled).
 */
void trax_fault_commit(enum trax_fault_kind_t kind);

/*-----------------------------------------------------------------------------
 * Port hooks (weak defaults in src/trax_fault.c — override in your BSP)
 *---------------------------------------------------------------------------*/

/**
 * @brief Read + normalize the vendor reset-status register (WEAK).
 *
 * Default returns TRAX_RESET_REASON_UNKNOWN / raw 0.  Override with your
 * device's register, e.g. STM32G0/H7 RCC->CSR RMVF-style:
 *
 *   enum trax_fault_reset_reason_t
 *   trax_fault_port_reset_reason(uint32_t *p_raw)
 *   {
 *       uint32_t csr = RCC->CSR;
 *       *p_raw = csr;
 *       RCC->CSR |= RCC_CSR_RMVF;         // clear flags for next boot
 *       if (csr & RCC_CSR_IWDGRSTF) return TRAX_RESET_REASON_IWDG;
 *       if (csr & RCC_CSR_SFTRSTF)  return TRAX_RESET_REASON_SOFTWARE;
 *       ...
 *   }
 *
 * Called once, from trax_fault_init() (boot context, before the app loop).
 */
enum trax_fault_reset_reason_t trax_fault_port_reset_reason(uint32_t *p_raw);

#else /* !TRAX_ENABLE || !TRAX_CFG_FAULT_ENABLE — inline no-op stubs */

static inline void trax_fault_init(void) {}
static inline void trax_fault_emit_pending(void) {}
static inline void trax_fault_flight_pump(void) {}
static inline const struct trax_fault_record_t *trax_fault_get_last(void)
{
	return (const struct trax_fault_record_t *)0;
}
static inline enum trax_fault_reset_reason_t
trax_fault_get_reset_reason(uint32_t *p_raw)
{
	if (p_raw) { *p_raw = 0u; }
	return TRAX_RESET_REASON_UNKNOWN;
}

#endif /* TRAX_ENABLE && TRAX_CFG_FAULT_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* TRAX_FAULT_H_ */
