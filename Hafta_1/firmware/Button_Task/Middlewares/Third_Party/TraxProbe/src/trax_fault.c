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
 * @file           : trax_fault.c
 * @brief          : TraxFault core — persistent record, boot recovery, emit
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 *
 * Platform-independent half of TraxFault.  The fault-time register capture
 * lives in the hardware port (hw_port/ARM_Cortex_M/src/trax_fault_port.c);
 * this file owns:
 *
 *   - the persistent record storage (TRAX_CFG_FAULT_SECTION)
 *   - trax_fault_commit()      — finalize the record inside the handler
 *   - trax_fault_init()        — boot-time validate / cache / invalidate
 *   - trax_fault_emit_pending()— session-start wire emission
 *   - the weak reset-reason / task-name hooks
 *
 * ZERO HOT-PATH COST: nothing in this file executes during normal
 * operation.  CRC is computed only inside the fault handler and once at
 * boot.
 *
 ******************************************************************************
 */

#include "trax_config_default.h"

/* Whole unit compiles out when the module (or TraxProbe) is disabled. */
#if TRAX_ENABLE && TRAX_CFG_FAULT_ENABLE

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "trax_fault.h"
#include "trax_frame.h"     /* TRAX_FRAME_ARGS_PROTOCOL, TRAX_FRAME_RAW_ARGS_PROTOCOL */
#include "trax_session.h"   /* trax_session.session_id */
#include "trax_timestamp.h" /* TRAX_FRAME_TIMEPACKED_GET */
#include "trax_tid.h"
#include "trax_hw.h"        /* TRAX_PORT_GET_CORE_ID */
#include "trax_buffer.h"    /* trax_buffer — flight-recorder ring walk */
#include "../os/common/trax_rtos_tables.h" /* trax_os_snapshot_*_meta — dynmeta capture */

#if (TRAX_CFG_RTOS_TYPE == TRAX_RTOS_FREERTOS)
#include "FreeRTOS.h"
#include "task.h"
#endif

/*=============================================================================
 ====================NOINIT BUDGET CROSS-CHECK=================================
 ============================================================================*/
/* TRAX_FAULT_NOINIT_TOTAL (trax_config_fault.h) is preprocessor math over
 * config literals so it can gate an #error against the user's reserved
 * region size (TRAX_CFG_FAULT_NOINIT_MAX).  These asserts pin that math
 * to the REAL struct sizes placed in TRAX_CFG_FAULT_SECTION — any wire
 * format growth breaks the build here until both are updated together. */

#if TRAX_CFG_FAULT_TRACE_SNAPSHOT > 0
TRAX_STATIC_ASSERT(sizeof(struct trax_fault_flight_t) ==
                   (16u + TRAX_CFG_FAULT_TRACE_SNAPSHOT),
                   "flight buffer size drifted from the "
                   "TRAX_FAULT_NOINIT_TOTAL budget math");
#endif

#if TRAX_CFG_FAULT_DYNMETA
TRAX_STATIC_ASSERT(sizeof(struct trax_fault_dynmeta_t) ==
                   (16u + (TRAX_CFG_FAULT_DYNMETA_TASKS * 40u)
                        + (TRAX_CFG_FAULT_DYNMETA_OBJECTS * 32u)),
                   "dynmeta snapshot size drifted from the "
                   "TRAX_FAULT_NOINIT_TOTAL budget math");
#endif

#if TRAX_CFG_FAULT_TRACE_SNAPSHOT > 0
#define TRAX_FAULT_SIZEOF_FLIGHT   sizeof(struct trax_fault_flight_t)
#else
#define TRAX_FAULT_SIZEOF_FLIGHT   0u
#endif
#if TRAX_CFG_FAULT_DYNMETA
#define TRAX_FAULT_SIZEOF_DYNMETA  sizeof(struct trax_fault_dynmeta_t)
#else
#define TRAX_FAULT_SIZEOF_DYNMETA  0u
#endif

TRAX_STATIC_ASSERT((sizeof(struct trax_fault_record_t)
                    + TRAX_FAULT_SIZEOF_FLIGHT
                    + TRAX_FAULT_SIZEOF_DYNMETA) == TRAX_FAULT_NOINIT_TOTAL,
                   "TRAX_FAULT_NOINIT_TOTAL no longer matches the structs "
                   "actually placed in TRAX_CFG_FAULT_SECTION — update the "
                   "budget math in trax_config_fault.h in lockstep");

/*=============================================================================
 ====================LOCAL VARIABLES===========================================
 ============================================================================*/

/**
 * @brief The persistent record.  Placed in TRAX_CFG_FAULT_SECTION, which the
 *        user's linker script marks NOLOAD and keeps OUTSIDE the .bss
 *        zero-fill range — that is the whole trick that lets a RAM struct
 *        survive NVIC_SystemReset.
 *
 * Deliberately NOT static: the naked handler shims reference
 * trax_fault_high_regs, and keeping both symbols external makes them
 * visible to map-file audits ("is my fault record where I think it is?").
 */
struct trax_fault_record_t trax_fault_record TRAX_SECTION(TRAX_CFG_FAULT_SECTION);

/** @brief R4-R11 spill slot written by the naked shim before C runs. */
uint32_t trax_fault_high_regs[8];

/** @brief RAM cache of the record recovered at boot (survivor of the crash). */
static struct trax_fault_record_t fault_cache;
static bool fault_cache_valid    = false;
static bool fault_record_pending = false;  /* not yet emitted this boot */

/** @brief Reset reason latched once at trax_fault_init(). */
static enum trax_fault_reset_reason_t reset_reason = TRAX_RESET_REASON_UNKNOWN;
static uint32_t reset_reason_raw = 0u;

#if TRAX_CFG_FAULT_TRACE_SNAPSHOT > 0
/**
 * @brief Flight-recorder snapshot (persistent).  Unlike the fault record
 *        it is NOT cached into normal RAM at boot — it is replayed
 *        straight out of the noinit slot (saves TRACE_SNAPSHOT bytes of
 *        .bss) and its magic is cleared only after the replay finishes.
 */
static struct trax_fault_flight_t trax_fault_flight
	TRAX_SECTION(TRAX_CFG_FAULT_SECTION);

/** @brief Replay state: snapshot valid this boot / armed / progress. */
static bool     flight_valid    = false;  /* validated at boot            */
static bool     flight_armed    = false;  /* BEGIN sent, DATA in progress */
static uint32_t flight_offset   = 0u;     /* next byte to replay          */
#endif

#if TRAX_CFG_FAULT_DYNMETA
/**
 * @brief Dynamic-metadata snapshot (persistent).  Like the flight
 *        snapshot it is NOT cached into normal RAM at boot — it is
 *        emitted straight from the noinit slot on the first session
 *        start after the crash, then invalidated.
 */
static struct trax_fault_dynmeta_t trax_fault_dynmeta
	TRAX_SECTION(TRAX_CFG_FAULT_SECTION);

/** @brief Snapshot validated at boot, awaiting its one-shot emission. */
static bool dynmeta_pending = false;
#endif

/*=============================================================================
 ====================LOCAL FUNCTION DECLARATION================================
 ============================================================================*/

static uint32_t fault_crc32(const uint8_t *p_data, uint32_t len);
static uint32_t fault_record_crc(const struct trax_fault_record_t *p_rec);
#if TRAX_CFG_FAULT_TRACE_SNAPSHOT > 0
static uint32_t fault_flight_crc(const struct trax_fault_flight_t *p_fl);
static void     fault_flight_capture(void);
#endif
#if TRAX_CFG_FAULT_DYNMETA
static uint32_t fault_dynmeta_crc(const struct trax_fault_dynmeta_t *p_dm);
static void     fault_dynmeta_capture(void);
#endif

/*=============================================================================
 ====================GLOBAL FUNCTION IMPLEMANTATION============================
 ============================================================================*/

void trax_fault_init(void)
{
	/* 1. Latch the reset reason first — reading typically also clears the
	 *    vendor flags, and doing it exactly once per boot keeps the
	 *    register semantics simple for the user's own code. */
	reset_reason = trax_fault_port_reset_reason(&reset_reason_raw);

	/* 2. Recover a surviving fault record, if the previous run crashed. */
	if (trax_fault_record.magic == TRAX_FAULT_MAGIC &&
	    trax_fault_record.version == (uint8_t)TRAX_FAULT_WIRE_VERSION &&
	    trax_fault_record.crc == fault_record_crc(&trax_fault_record)) {
		memcpy(&fault_cache, &trax_fault_record, sizeof(fault_cache));
		fault_cache_valid    = true;
		fault_record_pending = true;
	}

	/* 3. Invalidate the persistent slot either way: a stale or corrupt
	 *    record must never be mistaken for a fresh crash on the NEXT
	 *    boot, and the slot must be clean for the next capture. */
	trax_fault_record.magic = 0u;

#if TRAX_CFG_FAULT_TRACE_SNAPSHOT > 0
	/* 4. Validate the flight-recorder snapshot.  It is only meaningful
	 *    when PAIRED with the record recovered above (same crashed
	 *    session) — an unpaired snapshot is a leftover from an older
	 *    crash whose record was already consumed.  Unlike the record it
	 *    is NOT cached: it is replayed straight from the noinit slot and
	 *    invalidated after the replay (or right here when rejected). */
	flight_valid  = false;
	flight_armed  = false;
	flight_offset = 0u;
	if (fault_cache_valid &&
	    trax_fault_flight.magic == TRAX_FAULT_FLIGHT_MAGIC &&
	    trax_fault_flight.session_id == fault_cache.session_id &&
	    trax_fault_flight.used > 0u &&
	    trax_fault_flight.used <= (uint16_t)TRAX_CFG_FAULT_TRACE_SNAPSHOT &&
	    (trax_fault_flight.used % 4u) == 0u &&
	    trax_fault_flight.crc == fault_flight_crc(&trax_fault_flight)) {
		flight_valid = true;
	} else {
		trax_fault_flight.magic = 0u;
	}
#endif

#if TRAX_CFG_FAULT_DYNMETA
	/* 5. Validate the dynamic-metadata snapshot — same pairing rule as
	 *    the flight recorder (must belong to the recovered record's
	 *    session), same emit-straight-from-noinit lifecycle. */
	dynmeta_pending = false;
	if (fault_cache_valid &&
	    trax_fault_dynmeta.magic == TRAX_FAULT_DYNMETA_MAGIC &&
	    trax_fault_dynmeta.session_id == fault_cache.session_id &&
	    trax_fault_dynmeta.task_stored <= (uint8_t)TRAX_CFG_FAULT_DYNMETA_TASKS &&
	    trax_fault_dynmeta.obj_stored <= (uint8_t)TRAX_CFG_FAULT_DYNMETA_OBJECTS &&
	    trax_fault_dynmeta.crc == fault_dynmeta_crc(&trax_fault_dynmeta)) {
		dynmeta_pending = true;
	} else {
		trax_fault_dynmeta.magic = 0u;
	}
#endif
}

void trax_fault_emit_pending(void)
{
	/* Called from trax_meta_tx_send_start() right after the SESSION_START
	 * frame is committed — streaming is already enabled, and these frames
	 * land directly behind the metadata in the ring, so the host decodes
	 * them with full session context.
	 *
	 * PROTOCOL variants: these are protocol-level frames (like SESSION_STOP),
	 * not user trace data — they must not be silently dropped by a racing
	 * streaming-state flip. */

	/* Reset reason — every session start, so a host connecting to an
	 * already-running device still learns how this boot began. */
	TRAX_FRAME_ARGS_PROTOCOL(TRAX_TID_FAULT_RESET_REASON,
	                         (uint32_t)reset_reason,
	                         reset_reason_raw);

	/* Fault record — once per boot, on the first session start.  The wire
	 * frame carries the fixed head plus only the USED stack bytes: an
	 * empty snapshot costs nothing, a partial one (fault near the top of
	 * stack RAM) is not padded to TRAX_CFG_FAULT_STACK_SNAPSHOT. */
	if (fault_record_pending) {
		fault_record_pending = false;
		uint32_t wire_len = TRAX_FAULT_RECORD_HEAD_SIZE
		                    + (uint32_t)fault_cache.stack_used;
		TRAX_FRAME_RAW_ARGS_PROTOCOL(TRAX_TID_FAULT_RECORD,
		                             &fault_cache,
		                             wire_len,
		                             (uint32_t)TRAX_FAULT_WIRE_VERSION);
	}

#if TRAX_CFG_FAULT_DYNMETA
	/* Dynamic metadata — once per boot, right behind the record and
	 * BEFORE the flight replay so the host has the crashed run's
	 * handle→name map by the time the kernel frames arrive.  Each frame
	 * carries total vs stored counts: stored < total means the noinit
	 * caps truncated the snapshot and the host warns the user to raise
	 * TRAX_CFG_FAULT_DYNMETA_TASKS / _OBJECTS. */
	if (dynmeta_pending) {
		dynmeta_pending = false;
#if TRAX_CFG_FAULT_DYNMETA_TASKS > 0
		if (trax_fault_dynmeta.task_stored > 0u) {
			TRAX_FRAME_RAW_ARGS_PROTOCOL(
				TRAX_TID_FAULT_DYNMETA_TASKS,
				&trax_fault_dynmeta.tasks[0],
				(uint32_t)trax_fault_dynmeta.task_stored
					* (uint32_t)sizeof(struct trax_rtos_task_wire_t),
				trax_fault_dynmeta.session_id,
				(uint32_t)trax_fault_dynmeta.task_total,
				(uint32_t)trax_fault_dynmeta.task_stored);
		}
#endif
#if TRAX_CFG_FAULT_DYNMETA_OBJECTS > 0
		if (trax_fault_dynmeta.obj_stored > 0u) {
			TRAX_FRAME_RAW_ARGS_PROTOCOL(
				TRAX_TID_FAULT_DYNMETA_OBJS,
				&trax_fault_dynmeta.objects[0],
				(uint32_t)trax_fault_dynmeta.obj_stored
					* (uint32_t)sizeof(struct trax_rtos_object_wire_t),
				trax_fault_dynmeta.session_id,
				(uint32_t)trax_fault_dynmeta.obj_total,
				(uint32_t)trax_fault_dynmeta.obj_stored);
		}
#endif
		/* Delivered — invalidate so a reboot cannot replay it. */
		trax_fault_dynmeta.magic = 0u;
	}
#endif /* TRAX_CFG_FAULT_DYNMETA */

#if TRAX_CFG_FAULT_TRACE_SNAPSHOT > 0
	/* Flight recorder — announce the replay and arm the pump.  Only the
	 * small BEGIN frame goes out here: the data itself is paced through
	 * trax_fault_flight_pump() (one chunk per trax_process() tick, with
	 * a ring-headroom gate) so a large snapshot cannot overflow the new
	 * session's ring at its most fragile moment.  Re-arming on a LATER
	 * session start of the same boot restarts the replay from offset 0
	 * (the host resets its capture on every BEGIN), because a replay cut
	 * short by a host STOP would otherwise be lost. */
	if (flight_valid) {
		flight_armed  = true;
		flight_offset = 0u;
		TRAX_FRAME_ARGS_PROTOCOL(TRAX_TID_FAULT_FLIGHT_BEGIN,
		                         trax_fault_flight.session_id,
		                         (uint32_t)trax_fault_flight.used);
	}
#endif
}

void trax_fault_flight_pump(void)
{
#if TRAX_CFG_FAULT_TRACE_SNAPSHOT > 0
	if (!flight_armed) {
		return;
	}

	/* Replay only while the session that saw our BEGIN is still live —
	 * after a STOP the frames would drain into a void, and the next
	 * START re-arms from scratch anyway. */
	if (!TRAX_IS_SESSION_ACTIVE()) {
		return;
	}

	/* Ring headroom gate: the replay is history, live data is not — the
	 * pre-crash story must never be the thing that overflows the new
	 * session.  Require room for TWO chunks so one DATA frame can never
	 * be the allocation that trips the overflow stop. */
	{
		const uint32_t *p_rd    = trax_buffer.p_rd32;
		const uint32_t *p_alloc = trax_buffer.p_alloc32;
		size_t free_words = (p_alloc >= p_rd)
			? ((size_t)TRAX_CFG_OUT_BUFFER_SIZE32
			   - (size_t)(p_alloc - p_rd))
			: (size_t)(p_rd - p_alloc);
		if (free_words * 4u <
		    2u * ((size_t)TRAX_CFG_FAULT_FLIGHT_CHUNK + 32u)) {
			return;   /* retry next tick, ring drains meanwhile */
		}
	}

	{
		uint32_t remaining = (uint32_t)trax_fault_flight.used
		                     - flight_offset;
		uint32_t n = (remaining < (uint32_t)TRAX_CFG_FAULT_FLIGHT_CHUNK)
		                 ? remaining
		                 : (uint32_t)TRAX_CFG_FAULT_FLIGHT_CHUNK;
		if (n > 0u) {
			TRAX_FRAME_RAW_ARGS_PROTOCOL(
				TRAX_TID_FAULT_FLIGHT_DATA,
				&trax_fault_flight.data[flight_offset],
				n,
				flight_offset,
				n);
			flight_offset += n;
		}

		if (flight_offset >= (uint32_t)trax_fault_flight.used) {
			TRAX_FRAME_ARGS_PROTOCOL(TRAX_TID_FAULT_FLIGHT_END,
			                         flight_offset);
			/* Replay complete — this history has been delivered.
			 * Invalidate so a reboot cannot replay it again. */
			trax_fault_flight.magic = 0u;
			flight_armed = false;
			flight_valid = false;
		}
	}
#endif
}

const struct trax_fault_record_t *trax_fault_get_last(void)
{
	return fault_cache_valid ? &fault_cache : (const struct trax_fault_record_t *)0;
}

enum trax_fault_reset_reason_t trax_fault_get_reset_reason(uint32_t *p_raw)
{
	if (p_raw != (uint32_t *)0) {
		*p_raw = reset_reason_raw;
	}
	return reset_reason;
}

/**
 * @brief Finalize the persistent record from INSIDE the fault handler.
 *
 * Called by the port's trax_fault_capture() after the architecture fields
 * are filled.  Adds the platform-independent context (timestamp, session,
 * task name), then seals the record with magic + CRC.  The CRC is written
 * LAST and the magic just before it, so a power loss mid-write can only
 * produce a record that fails validation — never a half-truth.
 *
 * Runs at fault priority with the system in an unknown state: no RTOS
 * calls except the guarded task-name read, no transport, no ring buffer.
 */
void trax_fault_commit(enum trax_fault_kind_t kind)
{
	struct trax_fault_record_t *p_rec = &trax_fault_record;

	p_rec->version    = (uint8_t)TRAX_FAULT_WIRE_VERSION;
	p_rec->fault_kind = (uint8_t)kind;
	p_rec->core_id    = TRAX_PORT_GET_CORE_ID();
	p_rec->timepacked = TRAX_FRAME_TIMEPACKED_GET();
	p_rec->session_id = trax_session.session_id;

	memset(p_rec->task_name, 0, sizeof(p_rec->task_name));

	/* RTOS context (wire v3).  On bare metal the fields are still
	 * meaningful: sched_state = NONE tells the host there was no
	 * scheduler, which is itself useful post-mortem information. */
	p_rec->tick_count  = 0u;
	p_rec->sched_state = TRAX_FAULT_SCHED_NONE;
	p_rec->task_prio   = 0u;
	p_rec->rtos_rsvd   = 0u;
	p_rec->flags      |= TRAX_FAULT_FLAG_RTOS_CTX;

#if (TRAX_CFG_RTOS_TYPE == TRAX_RTOS_FREERTOS)
	/* Only touch the TCB when the scheduler has actually run — a fault
	 * during early init has no current task, and a corrupted-TCB walk
	 * inside the fault handler would escalate to lockup. pcTaskGetName /
	 * uxTaskPriorityGet on the current task are pointer chases into the
	 * TCB; acceptable risk, and the CRC seal below bounds the damage to
	 * garbled context — the host cross-checks task_name printability. */
	{
		BaseType_t sched = xTaskGetSchedulerState();
		p_rec->sched_state =
		    (sched == taskSCHEDULER_NOT_STARTED) ? TRAX_FAULT_SCHED_NOT_STARTED
		  : (sched == taskSCHEDULER_SUSPENDED)   ? TRAX_FAULT_SCHED_SUSPENDED
		                                         : TRAX_FAULT_SCHED_RUNNING;
		if (sched != taskSCHEDULER_NOT_STARTED) {
			p_rec->tick_count = (uint32_t)xTaskGetTickCountFromISR();
#if (INCLUDE_uxTaskPriorityGet == 1)
			p_rec->task_prio  =
			    (uint8_t)uxTaskPriorityGet((TaskHandle_t)0);
#endif
			const char *name = pcTaskGetName((TaskHandle_t)0);
			if (name != (const char *)0) {
				uint32_t i;
				for (i = 0u; i < (sizeof(p_rec->task_name) - 1u) && name[i] != '\0'; i++) {
					p_rec->task_name[i] = name[i];
				}
				p_rec->flags |= TRAX_FAULT_FLAG_TASK_NAME;
			}
		}
	}
#endif

#if TRAX_CFG_FAULT_TRACE_SNAPSHOT > 0
	/* Flight recorder: preserve the newest committed-but-undrained trace
	 * frames before the reset wipes the ring's control state.  Sealed
	 * independently of the record — a corrupt ring costs the history,
	 * never the crash record itself. */
	fault_flight_capture();
#endif

#if TRAX_CFG_FAULT_DYNMETA
	/* Dynamic metadata: preserve the crashed run's task / object tables
	 * so the host decodes the flight recorder's kernel frames with the
	 * EXACT handle→name map of this run (a task created dynamically
	 * mid-run does not exist after the reboot).  Sealed independently,
	 * same rationale as the flight snapshot. */
	fault_dynmeta_capture();
#endif

	p_rec->magic = TRAX_FAULT_MAGIC;
	p_rec->crc   = fault_record_crc(p_rec);
}

/*-----------------------------------------------------------------------------
 * Weak port hooks
 *---------------------------------------------------------------------------*/

/**
 * @brief Default reset-reason hook — no vendor register knowledge in the
 *        core library.  Override in your BSP / app (see trax_fault.h for
 *        an STM32 example).  TRAX_WEAK keeps the override linker-based:
 *        no registration call, no function pointer in RAM.
 */
#if defined(__GNUC__) || defined(__clang__) || defined(__IAR_SYSTEMS_ICC__) || defined(__ARMCC_VERSION)
__attribute__((weak))
#endif
enum trax_fault_reset_reason_t trax_fault_port_reset_reason(uint32_t *p_raw)
{
	*p_raw = 0u;
	return TRAX_RESET_REASON_UNKNOWN;
}

/*=============================================================================
 ====================LOCAL FUNCTION IMPLEMANTATION=============================
 ============================================================================*/

/**
 * @brief Bitwise CRC-32 (reflected, poly 0xEDB88320) — no table, ~40 bytes
 *        of code.  Speed is irrelevant: it runs on 148 bytes, once at fault
 *        time and once at boot.
 */
static uint32_t fault_crc32(const uint8_t *p_data, uint32_t len)
{
	uint32_t crc = 0xFFFFFFFFu;
	uint32_t i;
	for (i = 0u; i < len; i++) {
		uint32_t byte = p_data[i];
		uint32_t j;
		crc ^= byte;
		for (j = 0u; j < 8u; j++) {
			uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
			crc = (crc >> 1) ^ (0xEDB88320u & mask);
		}
	}
	return ~crc;
}

/**
 * @brief Record CRC: covers everything AFTER the crc field (offset 8..end),
 *        i.e. version through task_name. magic and crc seal the envelope.
 */
static uint32_t fault_record_crc(const struct trax_fault_record_t *p_rec)
{
	const uint8_t *p_bytes = (const uint8_t *)p_rec;
	const uint32_t skip    = 8u; /* magic (4) + crc (4) */
	return fault_crc32(p_bytes + skip,
	                   (uint32_t)sizeof(*p_rec) - skip);
}

#if TRAX_CFG_FAULT_TRACE_SNAPSHOT > 0

/**
 * @brief Flight-snapshot CRC: session_id + used + rsvd + the USED data
 *        bytes only (the unused tail is never written and never checked,
 *        so the capture does not pay for zeroing up to 8 KB).
 */
static uint32_t fault_flight_crc(const struct trax_fault_flight_t *p_fl)
{
	const uint8_t *p_bytes = (const uint8_t *)p_fl;
	const uint32_t skip    = 8u; /* magic (4) + crc (4) */
	const uint32_t head    = 8u; /* session_id (4) + used (2) + rsvd (2) */
	return fault_crc32(p_bytes + skip, head + (uint32_t)p_fl->used);
}

/**
 * @brief Fault-time ring walk — copy the NEWEST committed frames the host
 *        has not received into the persistent flight snapshot.
 *
 * The span [p_rd32, p_alloc32) holds exactly the committed-but-undrained
 * frames: everything older was already on the wire before the crash.
 * When the span exceeds TRAX_CFG_FAULT_TRACE_SNAPSHOT, whole OLDEST
 * frames are dropped first — the moments closest to the fault matter
 * most, and partial frames would break the host's re-parse.
 *
 * Runs at HardFault priority on a possibly-corrupted system, so every
 * pointer is bounds-checked against the ring's static geometry and both
 * walks carry an iteration fuse: a trampled control block or a garbage
 * Word-0 size aborts the capture (no snapshot) rather than faulting
 * inside the fault handler (lockup — no record at all).
 */
static void fault_flight_capture(void)
{
	struct trax_fault_flight_t *p_fl = &trax_fault_flight;

	p_fl->magic      = 0u;
	p_fl->used       = 0u;
	p_fl->rsvd       = 0u;
	p_fl->session_id = trax_session.session_id;

	const uint32_t *p_ring_start = &trax_buffer.p_mem32[0];
	const uint32_t *p_ring_end   = &trax_buffer.p_mem32[TRAX_CFG_OUT_BUFFER_SIZE32];
	const uint32_t *p_rd    = trax_buffer.p_rd32;
	const uint32_t *p_alloc = trax_buffer.p_alloc32;

	/* Control-block sanity: constants must still be the ring geometry,
	 * cursors must be inside it. */
	if (trax_buffer.p_start32 != p_ring_start ||
	    trax_buffer.p_end32   != p_ring_end   ||
	    p_rd    <  p_ring_start || p_rd    >= p_ring_end ||
	    p_alloc <  p_ring_start || p_alloc >= p_ring_end) {
		return;
	}

	/* Pass 1: measure the committed span and remember each frame's size
	 * indirectly by re-walking (no side storage at fault time). */
	uint32_t total_bytes = 0u;
	{
		const uint32_t *p    = p_rd;
		uint32_t        fuse = (uint32_t)TRAX_CFG_OUT_BUFFER_SIZE32;
		while (p != p_alloc && fuse > 0u) {
			uint32_t word0 = *p;
			if (word0 == 0u) {
				break;                   /* uncommitted — end of story */
			}
			if (word0 == TRAX_BUFF_WRAP_MARKER) {
				p = p_ring_start;
				fuse--;
				continue;
			}
			uint32_t size32 = word0 >> 16;
			if (size32 < (uint32_t)TRAX_CFG_FRAME_MIN_SIZE32 ||
			    (p + size32) > p_ring_end) {
				break;                   /* garbage header — stop here */
			}
			total_bytes += size32 * 4u;
			p += size32;
			fuse = (fuse > size32) ? (fuse - size32) : 0u;
		}
	}

	if (total_bytes == 0u) {
		return;
	}

	/* Pass 2: skip whole oldest frames until the rest fits, then copy
	 * frame by frame (per-frame memcpy also flattens ring wraps). */
	{
		const uint32_t *p    = p_rd;
		uint32_t        fuse = (uint32_t)TRAX_CFG_OUT_BUFFER_SIZE32;
		uint32_t        out  = 0u;
		while (p != p_alloc && fuse > 0u) {
			uint32_t word0 = *p;
			if (word0 == 0u) {
				break;
			}
			if (word0 == TRAX_BUFF_WRAP_MARKER) {
				p = p_ring_start;
				fuse--;
				continue;
			}
			uint32_t size32 = word0 >> 16;
			if (size32 < (uint32_t)TRAX_CFG_FRAME_MIN_SIZE32 ||
			    (p + size32) > p_ring_end) {
				break;
			}
			uint32_t frame_bytes = size32 * 4u;
			if (total_bytes > (uint32_t)TRAX_CFG_FAULT_TRACE_SNAPSHOT) {
				/* Still over budget — drop this (oldest) frame. */
				total_bytes -= frame_bytes;
			} else if ((out + frame_bytes) <=
			           (uint32_t)TRAX_CFG_FAULT_TRACE_SNAPSHOT) {
				memcpy(&p_fl->data[out], p, frame_bytes);
				out += frame_bytes;
			} else {
				break;   /* defensive: budget math off — stop clean */
			}
			p += size32;
			fuse = (fuse > size32) ? (fuse - size32) : 0u;
		}

		if (out == 0u) {
			return;
		}
		p_fl->used = (uint16_t)out;
	}

	p_fl->crc   = fault_flight_crc(p_fl);
	p_fl->magic = TRAX_FAULT_FLIGHT_MAGIC;
}

#endif /* TRAX_CFG_FAULT_TRACE_SNAPSHOT > 0 */

#if TRAX_CFG_FAULT_DYNMETA

/**
 * @brief Dynmeta CRC: covers everything after the magic + crc envelope
 *        (fixed length — the arrays are fixed-capacity, so unstored tail
 *        entries are hashed as-is; they are noinit bytes that do not
 *        change between capture and boot validation).
 */
static uint32_t fault_dynmeta_crc(const struct trax_fault_dynmeta_t *p_dm)
{
	const uint8_t *p_bytes = (const uint8_t *)p_dm;
	const uint32_t skip    = 8u; /* magic (4) + crc (4) */
	return fault_crc32(p_bytes + skip,
	                   (uint32_t)sizeof(*p_dm) - skip);
}

/**
 * @brief Fault-time copy of the RTOS task / object tables into the
 *        persistent snapshot.
 *
 * Uses the bounded fault-safe serializers (no RTOS calls, no task-stack
 * walks) and records BOTH the live count and the stored count per table —
 * the truncation evidence the host turns into a "raise
 * TRAX_CFG_FAULT_DYNMETA_*" hint.  An empty system (no tasks, no objects)
 * leaves the snapshot invalid: nothing to ship, nothing to validate.
 */
static void fault_dynmeta_capture(void)
{
	struct trax_fault_dynmeta_t *p_dm = &trax_fault_dynmeta;

	p_dm->magic       = 0u;
	p_dm->session_id  = trax_session.session_id;
	p_dm->task_total  = 0u;
	p_dm->task_stored = 0u;
	p_dm->obj_total   = 0u;
	p_dm->obj_stored  = 0u;

#if TRAX_CFG_FAULT_DYNMETA_TASKS > 0
	p_dm->task_stored = trax_os_snapshot_task_meta(
		(uint8_t *)&p_dm->tasks[0],
		(uint8_t)TRAX_CFG_FAULT_DYNMETA_TASKS,
		&p_dm->task_total);
#endif
#if TRAX_CFG_FAULT_DYNMETA_OBJECTS > 0
	p_dm->obj_stored = trax_os_snapshot_object_meta(
		(uint8_t *)&p_dm->objects[0],
		(uint8_t)TRAX_CFG_FAULT_DYNMETA_OBJECTS,
		&p_dm->obj_total);
#endif

	if (p_dm->task_total == 0u && p_dm->obj_total == 0u) {
		return;   /* nothing captured — leave the slot invalid */
	}

	p_dm->crc   = fault_dynmeta_crc(p_dm);
	p_dm->magic = TRAX_FAULT_DYNMETA_MAGIC;
}

#endif /* TRAX_CFG_FAULT_DYNMETA */

#endif /* TRAX_ENABLE && TRAX_CFG_FAULT_ENABLE */
