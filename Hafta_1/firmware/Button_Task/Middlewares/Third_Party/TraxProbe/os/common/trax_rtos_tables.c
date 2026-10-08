/*
 * SPDX-License-Identifier: LicenseRef-TraxProbe-Commercial
 * Copyright (c) 2026 Embedya. All rights reserved.
 *
 * TraxProbe core engine. CONFIDENTIAL and proprietary to Embedya.
 * Licensed under the TraxProbe Commercial License (see LICENSE-COMMERCIAL.txt).
 * No use, copying, modification, redistribution, decompilation, or reverse
 * engineering is permitted except as expressly authorized by that agreement.
 */

#include "trax_config_default.h"
#if (TRAX_CFG_RTOS_TYPE != TRAX_RTOS_NONE) && (TRAX_ENABLE)

/**
 ******************************************************************************
 * @file           : trax_rtos_tables.c
 * @brief          : RTOS-independent task and object table implementation
 * @version        : 1.0.0
 ******************************************************************************
 *
 * Static RAM tables for RTOS task and object metadata with O(1) operations:
 *   - Free-list stacks for O(1) slot allocation / deallocation
 *   - Tables are always maintained regardless of tracing state
 *   - On stream start both tables are serialized into the START_TRACE frame
 *
 * This module contains NO RTOS-specific code. Each RTOS port (FreeRTOS,
 * Zephyr, etc.) provides trace hook macros that call these functions.
 *
 ******************************************************************************
 */

#include "trax_config_default.h"   /* picks up TRAX_CFG_RTOS_TYPE */

#include "trax_utility.h"  /* trax_strnlen */
#include "trax_rtos_tables.h"
#include "trax_meta_tx.h"       /* trax_rtos_task_wire_t, trax_rtos_object_wire_t */

/*=============================================================================
 ====================ISR GUARD FLAG=============================================
 ============================================================================*/

/* Per-core ISR-tick guard.  Indexed by TRAX_PORT_GET_CORE_ID() in the
 * RTOS port's traceISR_ENTER / traceISR_EXIT / traceISR_EXIT_TO_SCHEDULER
 * macros (see os/<rtos>/trax_rtos_port.h).  Single-core builds set
 * TRAX_CFG_CORE_COUNT == 1 (the default), so the array degenerates to
 * a one-element scalar — same memory and code as the previous
 * `volatile uint8_t trax_in_tick_isr;` form, but the per-core
 * structure is in place for SMP without a future redesign.
 *
 * We do NOT make this static — it is referenced from macro expansions
 * in os/<rtos>/trax_rtos_port.h, which the application includes
 * indirectly.
 */
volatile uint8_t trax_in_tick_isr[TRAX_CFG_CORE_COUNT];  /* see trax_rtos_port.h */

/* Set with trax_in_tick_isr when TraxProbe opens a synthetic SysTick ISR
 * frame from traceTASK_INCREMENT_TICK (kernels whose port.c lacks
 * traceISR_ENTER). Cleared by trax_freertos_on_tick() / immediate EXIT.
 * Native V10.4+ path never sets this — prevents double ENTER/EXIT. */
volatile uint8_t trax_synthetic_tick_isr[TRAX_CFG_CORE_COUNT];

/* Set from traceMOVED_TASK_TO_READY_STATE while the tick ISR is open
 * when the readied task can preempt the running one.  Pre-V10.4
 * kernels have no traceISR_EXIT_TO_SCHEDULER in port.c; the tick hook
 * uses this flag to omit ISR_EXIT the same way. */
volatile uint8_t trax_tick_yield_pending[TRAX_CFG_CORE_COUNT];

/* Latched when traceTASK_INCREMENT_TICK finds the previous tick's synthetic
 * ISR frame still open, which can only mean the tick hook never reached
 * trax_freertos_on_tick().  Reported to the host as a configuration fault;
 * see the self-heal branch in os/FreeRTOS/trax_rtos_port.h. */
volatile uint8_t trax_tick_hook_missing[TRAX_CFG_CORE_COUNT];

/*=============================================================================
 ====================LOCAL MACRO DEFINITIONS===================================
 ============================================================================*/
#ifndef TRAX_CFG_MAX_RTOS_TASKS
#define TRAX_CFG_MAX_RTOS_TASKS 16
#endif

#ifndef TRAX_CFG_MAX_RTOS_OBJECTS
#define TRAX_CFG_MAX_RTOS_OBJECTS 16
#endif

#ifndef TRAX_CFG_RTOS_TASK_NAME_MAX
#define TRAX_CFG_RTOS_TASK_NAME_MAX 16
#endif

/*=============================================================================
 ====================LOCAL STUCTURES===========================================
 ============================================================================*/
struct trax_rtos_task_entry_t {
	uint32_t handle;
	uint32_t stack_size;
	uint32_t flags;          /* per-task latched bits (TRAX_TASK_FLAG_*) */
	/* First-fire snapshot of trax_timebase.tick_overflow_cntr and the packed
	 * [tick][timer] reading at the moment vApplicationStackOverflowHook
	 * latched OVERFLOWED for this task.  Pair decodes on the host through the
	 * same TimerConfig path used for live frames, so the Memory View can show
	 * the exact source-time of the overflow even after a reconnect.  Both 0
	 * means "no overflow latched yet". */
	uint32_t overflow_tick_overflow_cntr;
	uint32_t overflow_timepacked;
	uint8_t priority;
	char p_name[TRAX_CFG_RTOS_TASK_NAME_MAX];
};

struct trax_rtos_object_entry_t {
	uint32_t handle;
	uint32_t length;
	uint32_t item_size;
	uint8_t obj_type;
	char p_name[TRAX_RTOS_OBJ_NAME_LEN];
};

/*=============================================================================
 ====================LOCAL VARIABLES===========================================
 ============================================================================*/
static uint8_t p_free_stack[TRAX_CFG_MAX_RTOS_TASKS];
static uint8_t free_count;
static uint8_t freelist_ready;

static uint8_t p_obj_free_stack[TRAX_CFG_MAX_RTOS_OBJECTS];
static uint8_t obj_free_count;
static uint8_t obj_freelist_ready;

static struct trax_rtos_object_entry_t p_object_table[TRAX_CFG_MAX_RTOS_OBJECTS];
static struct trax_rtos_task_entry_t p_task_table[TRAX_CFG_MAX_RTOS_TASKS];

/*=============================================================================
 ====================LOCAL FUNCTION DECLARATION================================
 ============================================================================*/
static void trax_task_freelist_init(void);
static int trax_task_freelist_pop(void);
static void trax_task_freelist_push(uint8_t index);
static void trax_obj_freelist_init(void);
static int trax_obj_freelist_pop(void);
static void trax_obj_freelist_push(uint8_t index);

/*=============================================================================
 ====================GLOBAL FUNCTION IMPLEMENTATION============================
 ============================================================================*/
void trax_os_init(void)
{
	trax_task_freelist_init();
	trax_obj_freelist_init();
}

uint8_t trax_os_get_task_count(void)
{
	uint8_t count = 0;
	for (uint8_t i = 0; i < TRAX_CFG_MAX_RTOS_TASKS; i++) {
		if (p_task_table[i].handle != 0) {
			count++;
		}
	}
	return count;
}

void trax_os_write_task_meta(uint8_t *dst, uint8_t max_entries)
{
	struct trax_rtos_task_wire_t *wire = (struct trax_rtos_task_wire_t *)dst;
	uint8_t stored = 0;

	/* Cap at max_entries: the caller allocated exactly that many records
	 * from an earlier trax_os_get_task_count() read, and this loop runs
	 * preemptible — a task created in between must NOT overrun the
	 * caller's section (see the contract in trax_rtos_tables.h). */
	for (uint8_t i = 0; i < TRAX_CFG_MAX_RTOS_TASKS && stored < max_entries; i++) {
		const struct trax_rtos_task_entry_t *e = &p_task_table[i];
		if (e->handle == 0) {
			continue;
		}
		wire->handle                       = e->handle;
		wire->stack_size                   = e->stack_size;
		wire->priority                     = e->priority;
		wire->current_stack_hwm_words      = trax_os_get_task_stack_hwm_words(e->handle);
		wire->p_reserved                   = 0;
		wire->flags                        = e->flags;
		wire->overflow_tick_overflow_cntr  = e->overflow_tick_overflow_cntr;
		wire->overflow_timepacked          = e->overflow_timepacked;

		uint8_t copy_len = trax_strnlen(e->p_name, TRAX_RTOS_TASK_NAME_LEN);
		memcpy(wire->p_name, e->p_name, copy_len);
		if (copy_len < TRAX_RTOS_TASK_NAME_LEN) {
			memset(wire->p_name + copy_len, 0, TRAX_RTOS_TASK_NAME_LEN - copy_len);
		}
		wire++;
		stored++;
	}

	/* Zero-fill the shortfall (task deleted between count and write) so
	 * the section length matches the header count; the host drops
	 * handle==0 records. */
	if (stored < max_entries) {
		memset(wire, 0,
		       (size_t)(max_entries - stored) * sizeof(struct trax_rtos_task_wire_t));
	}
}

uint8_t trax_os_get_object_count(void)
{
	uint8_t count = 0;
	for (uint8_t i = 0; i < TRAX_CFG_MAX_RTOS_OBJECTS; i++) {
		if (p_object_table[i].handle != 0) {
			count++;
		}
	}
	return count;
}

uint8_t trax_os_get_core_count(void)
{
	return (uint8_t)TRAX_CFG_CORE_COUNT;
}

void trax_os_write_object_meta(uint8_t *dst, uint8_t max_entries)
{
	struct trax_rtos_object_wire_t *wire = (struct trax_rtos_object_wire_t *)dst;
	uint8_t stored = 0;

	/* Same cap + zero-fill contract as trax_os_write_task_meta(). */
	for (uint8_t i = 0; i < TRAX_CFG_MAX_RTOS_OBJECTS && stored < max_entries; i++) {
		const struct trax_rtos_object_entry_t *e = &p_object_table[i];
		if (e->handle == 0) {
			continue;
		}
		wire->handle        = e->handle;
		wire->length        = e->length;
		wire->item_size     = e->item_size;
		wire->obj_type      = e->obj_type;
		wire->p_reserved[0] = 0;
		wire->p_reserved[1] = 0;
		wire->p_reserved[2] = 0;

		uint8_t copy_len = trax_strnlen(e->p_name, TRAX_RTOS_OBJ_NAME_LEN);
		memcpy(wire->p_name, e->p_name, copy_len);
		if (copy_len < TRAX_RTOS_OBJ_NAME_LEN) {
			memset(wire->p_name + copy_len, 0, TRAX_RTOS_OBJ_NAME_LEN - copy_len);
		}
		wire++;
		stored++;
	}

	if (stored < max_entries) {
		memset(wire, 0,
		       (size_t)(max_entries - stored) * sizeof(struct trax_rtos_object_wire_t));
	}
}


uint8_t trax_os_snapshot_task_meta(uint8_t *dst, uint8_t max_entries, uint8_t *p_total)
{
	/* Fault-handler safe: plain table copy, no RTOS calls.  The HWM wire
	 * field is 0 (= unknown) — trax_os_get_task_stack_hwm_words() walks
	 * task stack RAM, which may be the very memory that just faulted. */
	struct trax_rtos_task_wire_t *wire = (struct trax_rtos_task_wire_t *)dst;
	uint8_t total  = 0;
	uint8_t stored = 0;

	for (uint8_t i = 0; i < TRAX_CFG_MAX_RTOS_TASKS; i++) {
		const struct trax_rtos_task_entry_t *e = &p_task_table[i];
		if (e->handle == 0) {
			continue;
		}
		total++;
		if (stored >= max_entries) {
			continue;   /* keep counting for the truncation report */
		}
		wire->handle                      = e->handle;
		wire->stack_size                  = e->stack_size;
		wire->priority                    = e->priority;
		wire->current_stack_hwm_words     = 0;
		wire->p_reserved                  = 0;
		wire->flags                       = e->flags;
		wire->overflow_tick_overflow_cntr = e->overflow_tick_overflow_cntr;
		wire->overflow_timepacked         = e->overflow_timepacked;

		uint8_t copy_len = trax_strnlen(e->p_name, TRAX_RTOS_TASK_NAME_LEN);
		memcpy(wire->p_name, e->p_name, copy_len);
		if (copy_len < TRAX_RTOS_TASK_NAME_LEN) {
			memset(wire->p_name + copy_len, 0, TRAX_RTOS_TASK_NAME_LEN - copy_len);
		}
		wire++;
		stored++;
	}

	if (p_total != (uint8_t *)0) {
		*p_total = total;
	}
	return stored;
}

uint8_t trax_os_snapshot_object_meta(uint8_t *dst, uint8_t max_entries, uint8_t *p_total)
{
	struct trax_rtos_object_wire_t *wire = (struct trax_rtos_object_wire_t *)dst;
	uint8_t total  = 0;
	uint8_t stored = 0;

	for (uint8_t i = 0; i < TRAX_CFG_MAX_RTOS_OBJECTS; i++) {
		const struct trax_rtos_object_entry_t *e = &p_object_table[i];
		if (e->handle == 0) {
			continue;
		}
		total++;
		if (stored >= max_entries) {
			continue;
		}
		wire->handle        = e->handle;
		wire->length        = e->length;
		wire->item_size     = e->item_size;
		wire->obj_type      = e->obj_type;
		wire->p_reserved[0] = 0;
		wire->p_reserved[1] = 0;
		wire->p_reserved[2] = 0;

		uint8_t copy_len = trax_strnlen(e->p_name, TRAX_RTOS_OBJ_NAME_LEN);
		memcpy(wire->p_name, e->p_name, copy_len);
		if (copy_len < TRAX_RTOS_OBJ_NAME_LEN) {
			memset(wire->p_name + copy_len, 0, TRAX_RTOS_OBJ_NAME_LEN - copy_len);
		}
		wire++;
		stored++;
	}

	if (p_total != (uint8_t *)0) {
		*p_total = total;
	}
	return stored;
}

int trax_rtos_task_create(uint32_t handle, uint32_t priority,
                          uint32_t stack_size, const char *name)
{
	int idx = trax_task_freelist_pop();
	if (idx < 0) {
		return -1;
	}

	struct trax_rtos_task_entry_t *entry = &p_task_table[idx];
	entry->handle                      = handle;
	entry->priority                    = (uint8_t)priority;
	entry->stack_size                  = stack_size;
	entry->flags                       = 0U;
	entry->overflow_tick_overflow_cntr = 0U;
	entry->overflow_timepacked         = 0U;
	entry->p_name[0]                   = '\0';

	if (name != NULL) {
		uint8_t len = trax_strnlen(name, TRAX_CFG_RTOS_TASK_NAME_MAX - 1);
		memcpy(entry->p_name, name, len);
		entry->p_name[len] = '\0';
	}

	if (TRAX_IS_SESSION_ACTIVE()) {
		uint8_t name_len = trax_strnlen(entry->p_name, TRAX_CFG_RTOS_TASK_NAME_MAX);
		TRAX_FRAME_RAW_ARGS_PROTOCOL(TRAX_TID_TASK_CREATE,
			entry->p_name, name_len,
			entry->handle, (uint32_t)entry->priority, entry->stack_size);
	}

	return idx;
}

void trax_rtos_task_delete(uint8_t table_index, uint32_t handle)
{
	if (table_index >= TRAX_CFG_MAX_RTOS_TASKS) {
		return;
	}

	struct trax_rtos_task_entry_t *entry = &p_task_table[table_index];
	if (entry->handle != handle) {
		return;
	}

	memset(entry, 0, sizeof(*entry));
	trax_task_freelist_push(table_index);

	if (TRAX_IS_SESSION_ACTIVE()) {
		TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_TASK_DELETE, handle);
	}
}

/* Shared body for the three priority-update entry points (explicit set,
 * kernel inheritance, kernel disinheritance).  Same table update path; the
 * only diff is the TID emitted on the wire so the host can render
 * application-driven priority changes separately from mutex-inheritance
 * boosts.  Inlined to keep the call site identical in cost to the original
 * single-purpose function. */
static inline void emit_priority_change(uint8_t table_index, uint32_t handle,
                                        uint8_t new_priority, uint16_t tid)
{
	if (table_index >= TRAX_CFG_MAX_RTOS_TASKS) {
		return;
	}

	struct trax_rtos_task_entry_t *entry = &p_task_table[table_index];
	if (entry->handle != handle) {
		return;
	}

	entry->priority = new_priority;

	if (TRAX_IS_SESSION_ACTIVE()) {
		TRAX_FRAME_ARGS_ATOMIC(tid, handle, (uint32_t)new_priority);
	}
}

void trax_rtos_task_update_priority(uint8_t table_index, uint32_t handle,
                                    uint8_t new_priority)
{
	emit_priority_change(table_index, handle, new_priority,
	                     TRAX_TID_TASK_PRIORITY_SET);
}

void trax_rtos_task_priority_inherit(uint8_t table_index, uint32_t handle,
                                     uint8_t inherited_priority)
{
	emit_priority_change(table_index, handle, inherited_priority,
	                     TRAX_TID_TASK_PRIORITY_INHERIT);
}

void trax_rtos_task_priority_disinherit(uint8_t table_index, uint32_t handle,
                                        uint8_t restored_priority)
{
	emit_priority_change(table_index, handle, restored_priority,
	                     TRAX_TID_TASK_PRIORITY_DISINHERIT);
}

/* Linear-scan flag accessors.  Cannot use the uxTCBNumber-stash O(1)
 * pattern because the call sites (vApplicationStackOverflowHook,
 * starvation detector, etc.) only carry a TaskHandle_t — the kernel
 * has not necessarily walked the table-index round-trip.  The scan is
 * bounded by TRAX_CFG_MAX_RTOS_TASKS (default 16) so the worst case is
 * still trivial, and it runs from cold paths only (overflow, fault,
 * session-snapshot read). */
void trax_rtos_task_set_flag(uint32_t handle, uint32_t flag)
{
	if (handle == 0U || flag == 0U) {
		return;
	}
	for (uint8_t i = 0; i < TRAX_CFG_MAX_RTOS_TASKS; i++) {
		if (p_task_table[i].handle == handle) {
			p_task_table[i].flags |= flag;
			return;
		}
	}
}

uint32_t trax_rtos_task_get_flags(uint32_t handle)
{
	if (handle == 0U) {
		return 0U;
	}
	for (uint8_t i = 0; i < TRAX_CFG_MAX_RTOS_TASKS; i++) {
		if (p_task_table[i].handle == handle) {
			return p_task_table[i].flags;
		}
	}
	return 0U;
}

void trax_rtos_task_latch_overflow(uint32_t handle,
                                   uint32_t tick_overflow_cntr,
                                   uint32_t timepacked)
{
	if (handle == 0U) {
		return;
	}
	for (uint8_t i = 0; i < TRAX_CFG_MAX_RTOS_TASKS; i++) {
		struct trax_rtos_task_entry_t *e = &p_task_table[i];
		if (e->handle != handle) {
			continue;
		}
		/* First-write-wins: if the slot already carries a non-zero pair
		 * we keep it.  The first overflow is the diagnostic event of
		 * interest; later "aftershocks" — only possible if the user's
		 * hook returns instead of resetting — would just smear the
		 * post-mortem on the host. */
		if (e->overflow_tick_overflow_cntr == 0U &&
		    e->overflow_timepacked == 0U) {
			e->overflow_tick_overflow_cntr = tick_overflow_cntr;
			e->overflow_timepacked         = timepacked;
		}
		return;
	}
}


int trax_rtos_object_create(uint32_t handle, uint8_t obj_type,
                            uint32_t length, uint32_t item_size)
{
	int idx = trax_obj_freelist_pop();
	if (idx < 0) {
		return -1;
	}

	struct trax_rtos_object_entry_t *entry = &p_object_table[idx];
	entry->handle    = handle;
	entry->obj_type  = obj_type;
	entry->length    = length;
	entry->item_size = item_size;
	entry->p_name[0] = '\0';

	if (TRAX_IS_SESSION_ACTIVE()) {
		TRAX_FRAME_ARGS_PROTOCOL(TRAX_TID_OBJ_CREATE,
			handle, (uint32_t)obj_type, length, item_size);
	}

	return idx;
}

void trax_rtos_object_delete(uint8_t table_index, uint32_t handle)
{
	if (table_index >= TRAX_CFG_MAX_RTOS_OBJECTS) {
		return;
	}

	struct trax_rtos_object_entry_t *entry = &p_object_table[table_index];
	if (entry->handle != handle) {
		return;
	}

	memset(entry, 0, sizeof(*entry));
	trax_obj_freelist_push(table_index);

	if (TRAX_IS_SESSION_ACTIVE()) {
		TRAX_FRAME_ARGS_ATOMIC(TRAX_TID_OBJ_DELETE, handle);
	}
}

void trax_rtos_object_set_name(uint8_t table_index, uint32_t handle,
                               const char *name)
{
	if (table_index >= TRAX_CFG_MAX_RTOS_OBJECTS) {
		return;
	}

	struct trax_rtos_object_entry_t *entry = &p_object_table[table_index];
	if (entry->handle != handle) {
		return;
	}

	entry->p_name[0] = '\0';
	if (name != NULL) {
		uint8_t len = trax_strnlen(name, TRAX_RTOS_OBJ_NAME_LEN - 1);
		memcpy(entry->p_name, name, len);
		entry->p_name[len] = '\0';
	}

	if (TRAX_IS_SESSION_ACTIVE()) {
		uint8_t name_len = trax_strnlen(entry->p_name, TRAX_RTOS_OBJ_NAME_LEN);
		TRAX_FRAME_RAW_ARGS_PROTOCOL(TRAX_TID_OBJ_NAME,
			entry->p_name, name_len, handle);
	}
}

uint8_t trax_os_get_task_handles(uint32_t *p_handles, uint8_t max_count)
{
	uint8_t count = 0;
	for (uint8_t i = 0; i < TRAX_CFG_MAX_RTOS_TASKS && count < max_count; i++) {
		if (p_task_table[i].handle != 0) {
			p_handles[count++] = p_task_table[i].handle;
		}
	}
	return count;
}

int trax_rtos_object_find_by_handle(uint32_t handle)
{
	if (handle == 0U) {
		return -1;
	}
	for (uint8_t i = 0; i < TRAX_CFG_MAX_RTOS_OBJECTS; i++) {
		if (p_object_table[i].handle == handle) {
			return (int)i;
		}
	}
	return -1;
}

void trax_rtos_object_delete_by_handle(uint32_t handle)
{
	int idx = trax_rtos_object_find_by_handle(handle);
	if (idx >= 0) {
		trax_rtos_object_delete((uint8_t)idx, handle);
	}
}

void trax_rtos_object_set_name_by_handle(uint32_t handle, const char *name)
{
	int idx = trax_rtos_object_find_by_handle(handle);
	if (idx >= 0) {
		trax_rtos_object_set_name((uint8_t)idx, handle, name);
	}
}

/*=============================================================================
 ====================LOCAL FUNCTION IMPLEMENTATION=============================
 ============================================================================*/
static void trax_task_freelist_init(void)
{
	if (freelist_ready) {
		return;
	}

	for (uint8_t i = 0; i < TRAX_CFG_MAX_RTOS_TASKS; i++) {
		p_free_stack[i] = i;
	}

	free_count = TRAX_CFG_MAX_RTOS_TASKS;
	freelist_ready = 1;
}

static int trax_task_freelist_pop(void)
{
	if (free_count == 0) {
		return -1;
	}

	free_count--;

	return (int)p_free_stack[free_count];
}

static void trax_task_freelist_push(uint8_t index)
{
	p_free_stack[free_count] = index;
	free_count++;
}

static void trax_obj_freelist_init(void)
{
	if (obj_freelist_ready) {
		return;
	}
	for (uint8_t i = 0; i < TRAX_CFG_MAX_RTOS_OBJECTS; i++) {
		p_obj_free_stack[i] = i;
	}
	obj_free_count = TRAX_CFG_MAX_RTOS_OBJECTS;
	obj_freelist_ready = 1;
}

static int trax_obj_freelist_pop(void)
{
	if (obj_free_count == 0) {
		return -1;
	}
	obj_free_count--;
	return (int)p_obj_free_stack[obj_free_count];
}

static void trax_obj_freelist_push(uint8_t index)
{
	p_obj_free_stack[obj_free_count] = index;
	obj_free_count++;
}

#endif /* TRAX_CFG_RTOS_TYPE != TRAX_RTOS_NONE */
