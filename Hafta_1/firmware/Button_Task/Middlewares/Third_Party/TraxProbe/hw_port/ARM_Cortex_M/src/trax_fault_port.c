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
 * @file           : trax_fault_port.c
 * @brief          : ARM Cortex-M fault-time capture for TraxFault
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 *
 * Fault handlers + register capture for every Cortex-M profile:
 *
 *   ARMv7-M / ARMv8-M Mainline (M3/M4/M7/M33/M55/M85):
 *     HardFault + MemManage + BusFault + UsageFault handlers, full
 *     fault-status capture (CFSR/HFSR/DFSR/MMFAR/BFAR/AFSR/SHCSR).
 *
 *   ARMv6-M / ARMv8-M Baseline (M0/M0+/M1/M23):
 *     HardFault only (the architecture has no other fault exceptions and
 *     no fault-status registers).  The record carries the stacked frame
 *     (R0-R3, R12, LR, PC, xPSR) + EXC_RETURN; TRAX_FAULT_FLAG_FAULT_REGS
 *     stays clear so the host degrades gracefully.
 *
 * System-control registers are accessed by architectural address rather
 * than through CMSIS so this file has zero device-header dependencies.
 *
 * The handlers are NAKED shims: the compiler must not touch any register
 * before R4-R11 and the stack pointers are spilled, or the pre-fault
 * context would be lost.  The shim instruction sequence is restricted to
 * the ARMv6-M subset so ONE encoding serves every profile.
 *
 ******************************************************************************
 */

#include "trax_config_default.h"

#if TRAX_ENABLE && TRAX_CFG_FAULT_ENABLE && \
    (TRAX_CFG_HW_PORT == TRAX_HW_PORT_ARM_CORTEX_M)

#include <stdint.h>
#include <string.h>

#include "trax_fault.h"

/*=============================================================================
 ====================ARCHITECTURE DETECTION====================================
 ============================================================================*/

/* Baseline profiles (M0/M0+/M1/M23) have no CFSR/HFSR/MMFAR/BFAR and no
 * configurable-fault exceptions. Same detection ladder as trax_hw_port.h. */
#if defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_8M_BASE__)
    #define TRAX_FAULT_ARCH_MAINLINE  0
#elif defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || \
      defined(__ARM_ARCH_8M_MAIN__) || defined(__ARM_ARCH_8_1M_MAIN__)
    #define TRAX_FAULT_ARCH_MAINLINE  1
#elif defined(__CORTEX_M)
    #if (__CORTEX_M == 0) || (__CORTEX_M == 1) || (__CORTEX_M == 23)
        #define TRAX_FAULT_ARCH_MAINLINE  0
    #else
        #define TRAX_FAULT_ARCH_MAINLINE  1
    #endif
#else
    /* Unknown core: assume Baseline — reading the Mainline fault registers
     * on a core that lacks them would itself bus-fault inside the handler. */
    #define TRAX_FAULT_ARCH_MAINLINE  0
#endif

/*=============================================================================
 ====================SYSTEM CONTROL BLOCK (architectural addresses)===========
 ============================================================================*/

#define TRAX_SCB_REG(addr)  (*(volatile uint32_t *)(addr))

#define TRAX_SCB_ICSR   TRAX_SCB_REG(0xE000ED04u)  /* VECTACTIVE = bits [8:0] */
#define TRAX_SCB_VTOR   TRAX_SCB_REG(0xE000ED08u)
#define TRAX_SCB_AIRCR  TRAX_SCB_REG(0xE000ED0Cu)
#define TRAX_SCB_SHCSR  TRAX_SCB_REG(0xE000ED24u)
#define TRAX_SCB_CFSR   TRAX_SCB_REG(0xE000ED28u)
#define TRAX_SCB_HFSR   TRAX_SCB_REG(0xE000ED2Cu)
#define TRAX_SCB_DFSR   TRAX_SCB_REG(0xE000ED30u)
#define TRAX_SCB_MMFAR  TRAX_SCB_REG(0xE000ED34u)
#define TRAX_SCB_BFAR   TRAX_SCB_REG(0xE000ED38u)
#define TRAX_SCB_AFSR   TRAX_SCB_REG(0xE000ED3Cu)

/* AIRCR: VECTKEY (0x05FA << 16) | SYSRESETREQ (bit 2) */
#define TRAX_AIRCR_SYSRESET  0x05FA0004u

/*=============================================================================
 ====================DEDICATED FAULT STACK=====================================
 ============================================================================*/

/**
 * On a stack overflow the faulting MSP points below RAM: the exception
 * entry already failed to stack the frame (STKERR), and any C prologue
 * push in the handler would fault again — at HardFault priority that is
 * LOCKUP, i.e. no record and no reset without a watchdog.  The naked shim
 * therefore switches MSP to this dedicated stack before calling C.
 * 256 bytes covers trax_fault_capture + trax_fault_commit (measured
 * worst path is well under half of that; the CRC loop is iterative).
 */
#define TRAX_FAULT_STACK_WORDS  64u

static uint32_t fault_stack[TRAX_FAULT_STACK_WORDS]
    __attribute__((aligned(8)));

/* Referenced by name from the shim asm only — 'used' keeps it alive under
 * LTO / -ffunction-sections garbage collection. */
__attribute__((used)) const uint32_t trax_fault_stack_top =
    (uint32_t)&fault_stack[TRAX_FAULT_STACK_WORDS];

/*=============================================================================
 ====================STACKED-FRAME PLAUSIBILITY WINDOW=========================
 ============================================================================*/

/**
 * Reading the stacked exception frame back from a corrupted SP would
 * fault inside the fault handler (lockup).  Before dereferencing, SP is
 * checked against a plausibility window; the default covers the
 * architectural SRAM region (0x20000000-0x3FFFFFFF), which is where the
 * stack lives on virtually every Cortex-M part.  Parts that run their
 * stack elsewhere (F4 CCM at 0x1000_0000, some external-RAM setups) can
 * override the window in trax_config.h.
 */
#ifndef TRAX_CFG_FAULT_SP_MIN
#define TRAX_CFG_FAULT_SP_MIN  0x20000000u
#endif
#ifndef TRAX_CFG_FAULT_SP_MAX
#define TRAX_CFG_FAULT_SP_MAX  0x3FFFFFFFu
#endif

/* CFSR stacking-error bits (Mainline): MSTKERR (1<<4), STKERR (1<<12).
 * When the entry stacking itself failed, the frame contents are garbage
 * even if SP happens to look plausible. */
#define TRAX_CFSR_STKERR_MASK  ((1u << 4) | (1u << 12))

/*=============================================================================
 ====================LOCAL FUNCTION DECLARATION================================
 ============================================================================*/

static void trax_fault_system_reset(void);

/*=============================================================================
 ====================FAULT-TIME CAPTURE========================================
 ============================================================================*/

void trax_fault_capture(uint32_t msp, uint32_t psp, uint32_t exc_return)
{
	struct trax_fault_record_t *p_rec = &trax_fault_record;

	/* Deterministic zeros for everything this profile cannot provide. */
	memset(p_rec, 0, sizeof(*p_rec));

	p_rec->exc_return = exc_return;
	p_rec->msp        = msp;
	p_rec->psp        = psp;

	/* EXC_RETURN bit 2: 0 = frame stacked on MSP, 1 = on PSP. */
	uint32_t sp = ((exc_return & 0x4u) != 0u) ? psp : msp;
	p_rec->sp = sp;

	/* R4-R11 were spilled by the naked shim before any C code ran. */
	uint32_t i;
	for (i = 0u; i < 8u; i++) {
		p_rec->r[4u + i] = trax_fault_high_regs[i];
	}

	/* Stacked exception frame: [R0 R1 R2 R3 R12 LR PC xPSR] at SP.
	 * Guard the dereference — a fault caused by a corrupted or overflowed
	 * stack can leave SP pointing anywhere, and a second fault in here
	 * (at HardFault priority) means lockup: no record, no reset without a
	 * watchdog.  Three checks: alignment, the SRAM plausibility window
	 * (TRAX_CFG_FAULT_SP_MIN/MAX, overridable), and — on Mainline — the
	 * CFSR stacking-error bits, because a failed entry stacking leaves
	 * garbage at a perfectly plausible-looking SP. */
	uint32_t stacking_failed = 0u;
#if TRAX_FAULT_ARCH_MAINLINE
	stacking_failed = TRAX_SCB_CFSR & TRAX_CFSR_STKERR_MASK;
#endif
	uint32_t sp_ok = ((sp & 0x3u) == 0u &&
	                  sp >= TRAX_CFG_FAULT_SP_MIN &&
	                  sp <= (TRAX_CFG_FAULT_SP_MAX - 32u) &&
	                  stacking_failed == 0u) ? 1u : 0u;
	if (sp_ok != 0u) {
		const uint32_t *p_frame = (const uint32_t *)sp;
		p_rec->r[0]  = p_frame[0];
		p_rec->r[1]  = p_frame[1];
		p_rec->r[2]  = p_frame[2];
		p_rec->r[3]  = p_frame[3];
		p_rec->r[12] = p_frame[4];
		p_rec->lr    = p_frame[5];
		p_rec->pc    = p_frame[6];
		p_rec->xpsr  = p_frame[7];
		p_rec->flags |= TRAX_FAULT_FLAG_STACKED_FRAME;
	}

	/* Stack snapshot (wire v2): copy from the faulting SP UP toward the
	 * stack base — caller frames and return addresses live above SP, and
	 * that is what the host's heuristic backtrace scans.  The copy is
	 * clamped to the end of stack RAM: reading past physical RAM would
	 * bus-fault in here, and at HardFault priority that is lockup.  The
	 * RAM end comes from TRAX_CFG_FAULT_RAM_END, or — when 0 — from the
	 * initial MSP in slot 0 of the active vector table (top of stack RAM
	 * by convention on every Cortex-M part). */
	p_rec->stack_addr = sp;
	p_rec->stack_cap  = (uint16_t)TRAX_CFG_FAULT_STACK_SNAPSHOT;
#if TRAX_CFG_FAULT_STACK_SNAPSHOT > 0
	if (sp_ok != 0u) {
		uint32_t ram_end = TRAX_CFG_FAULT_RAM_END;
		if (ram_end == 0u) {
			ram_end = *(const uint32_t *)TRAX_SCB_VTOR;
		}
		if (ram_end > sp) {
			uint32_t avail = ram_end - sp;
			uint32_t len   = (avail < (uint32_t)TRAX_CFG_FAULT_STACK_SNAPSHOT)
			                     ? (avail & ~0x3u)
			                     : (uint32_t)TRAX_CFG_FAULT_STACK_SNAPSHOT;
			if (len > 0u) {
				memcpy(p_rec->stack, (const void *)sp, len);
				p_rec->stack_used = (uint16_t)len;
				p_rec->flags |= TRAX_FAULT_FLAG_STACK;
			}
		}
	}
#endif

	/* Active exception number tells us WHICH fault ran, even when a
	 * configurable fault escalated to HardFault or all handlers funnel
	 * into this one function. */
	p_rec->icsr = TRAX_SCB_ICSR;
	enum trax_fault_kind_t kind;
	switch (p_rec->icsr & 0x1FFu) {
	case 3u:  kind = TRAX_FAULT_KIND_HARDFAULT;  break;
	case 4u:  kind = TRAX_FAULT_KIND_MEMMANAGE;  break;
	case 5u:  kind = TRAX_FAULT_KIND_BUSFAULT;   break;
	case 6u:  kind = TRAX_FAULT_KIND_USAGEFAULT; break;
	default:  kind = TRAX_FAULT_KIND_UNKNOWN;    break;
	}

#if TRAX_FAULT_ARCH_MAINLINE
	p_rec->cfsr  = TRAX_SCB_CFSR;
	p_rec->hfsr  = TRAX_SCB_HFSR;
	p_rec->dfsr  = TRAX_SCB_DFSR;
	p_rec->mmfar = TRAX_SCB_MMFAR;
	p_rec->bfar  = TRAX_SCB_BFAR;
	p_rec->afsr  = TRAX_SCB_AFSR;
	p_rec->shcsr = TRAX_SCB_SHCSR;
	p_rec->flags |= TRAX_FAULT_FLAG_FAULT_REGS;
#endif

	/* Timestamp, session, task name, magic + CRC seal. */
	trax_fault_commit(kind);

	trax_fault_system_reset();
}

/*=============================================================================
 ====================FAULT HANDLERS (naked shims)==============================
 ============================================================================*/

#if TRAX_CFG_FAULT_HANDLERS

/**
 * Shim contract (see trax_fault.h): spill R4-R11 to trax_fault_high_regs,
 * capture MSP/PSP/EXC_RETURN, switch to the dedicated fault stack, tail-
 * call trax_fault_capture. Never returns.
 *
 * Every instruction is ARMv6-M-encodable:
 *   - high registers reach memory via MOV-to-low + STMIA (STR R8.. has no
 *     Baseline encoding),
 *   - the tail call is LDR+BX because Baseline B.W does not exist and a
 *     plain B has only ±2 KB reach.
 * MSP is read BEFORE the switch to the dedicated stack, so the record
 * shows the real faulting MSP. The LDR= literals are materialized in this
 * section's literal pool; the unconditional BX ends the function so the
 * assembler can place the pool immediately after it.
 */
#define TRAX_FAULT_NAKED_SHIM()                          \
	__asm volatile(                                      \
	    ".syntax unified                  \n"            \
	    " ldr   r2, =trax_fault_high_regs \n"            \
	    " stmia r2!, {r4-r7}              \n"            \
	    " mov   r4, r8                    \n"            \
	    " mov   r5, r9                    \n"            \
	    " mov   r6, r10                   \n"            \
	    " mov   r7, r11                   \n"            \
	    " stmia r2!, {r4-r7}              \n"            \
	    " mrs   r0, msp                   \n"            \
	    " mrs   r1, psp                   \n"            \
	    " mov   r2, lr                    \n"            \
	    " ldr   r3, =trax_fault_stack_top \n"            \
	    " ldr   r3, [r3]                  \n"            \
	    " msr   msp, r3                   \n"            \
	    " ldr   r3, =trax_fault_capture   \n"            \
	    " bx    r3                        \n")

__attribute__((naked)) void HardFault_Handler(void)
{
	TRAX_FAULT_NAKED_SHIM();
}

#if TRAX_FAULT_ARCH_MAINLINE
/* Separate configurable-fault handlers exist on Mainline only. They are
 * taken instead of HardFault when the user enables them in SHCSR
 * (MEMFAULTENA / BUSFAULTENA / USGFAULTENA); otherwise the fault escalates
 * to HardFault and the shim above catches it — either way VECTACTIVE
 * inside trax_fault_capture records the true kind. */
__attribute__((naked)) void MemManage_Handler(void)
{
	TRAX_FAULT_NAKED_SHIM();
}

__attribute__((naked)) void BusFault_Handler(void)
{
	TRAX_FAULT_NAKED_SHIM();
}

__attribute__((naked)) void UsageFault_Handler(void)
{
	TRAX_FAULT_NAKED_SHIM();
}
#endif /* TRAX_FAULT_ARCH_MAINLINE */

#endif /* TRAX_CFG_FAULT_HANDLERS */

/*=============================================================================
 ====================LOCAL FUNCTION IMPLEMANTATION=============================
 ============================================================================*/

/**
 * @brief Architectural system reset (AIRCR.SYSRESETREQ). The barriers
 *        guarantee the record stores retire before the reset request and
 *        the spin absorbs the cycles until it takes effect.
 */
static void trax_fault_system_reset(void)
{
	__asm volatile("dsb" ::: "memory");
	TRAX_SCB_AIRCR = TRAX_AIRCR_SYSRESET;
	__asm volatile("dsb" ::: "memory");
	for (;;) {
		/* wait for reset */
	}
}

#endif /* TRAX_ENABLE && TRAX_CFG_FAULT_ENABLE && ARM_CORTEX_M */
