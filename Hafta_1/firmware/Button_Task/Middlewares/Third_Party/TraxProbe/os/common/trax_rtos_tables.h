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
 * @file           : trax_rtos_tables.h
 * @brief          : RTOS-independent task and object table management
 * @version        : 1.1.0
 ******************************************************************************
 * @attention
 *
 * Unified metadata query API for all platform types (bare-metal, RTOS).
 * Source files include this header unconditionally; the correct implementation
 * is selected at compile time:
 *
 *   Bare-metal (TRAX_RTOS_NONE)  — static inline stubs (zero overhead)
 *   FreeRTOS / other RTOS        — os/common/trax_rtos_tables.c
 *
 * The task/object CRUD functions (create, delete, set_name, etc.) are only
 * declared for RTOS builds where port hook macros call them directly.
 *
 ******************************************************************************
 */

#ifndef TRAX_RTOS_TABLES_H_
#define TRAX_RTOS_TABLES_H_

#include <stdint.h>
#include "trax_tid.h"
#include "trax_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 ====================METADATA QUERY API========================================
 ============================================================================*/
/*
 * These five functions form the platform-independent metadata interface used
 * by trax_meta_tx.c and trax_core.c.  Implementations:
 *   Bare-metal  — static inline stubs below (zero overhead)
 *   FreeRTOS    — os/common/trax_rtos_tables.c
 */

#if (TRAX_CFG_RTOS_TYPE == TRAX_RTOS_NONE)

static inline void    trax_os_init(void) {}
static inline uint8_t trax_os_get_task_count(void)              { return 0; }
static inline void    trax_os_write_task_meta(uint8_t *dst, uint8_t max_entries)
{
    (void)dst; (void)max_entries;
}
static inline uint8_t trax_os_get_object_count(void)            { return 0; }
static inline void    trax_os_write_object_meta(uint8_t *dst, uint8_t max_entries)
{
    (void)dst; (void)max_entries;
}
static inline uint8_t trax_os_get_core_count(void)              { return (uint8_t)TRAX_CFG_CORE_COUNT; }
static inline uint8_t trax_os_get_task_handles(uint32_t *p, uint8_t m) { (void)p; (void)m; return 0; }
static inline uint8_t trax_os_snapshot_task_meta(uint8_t *dst, uint8_t max_entries, uint8_t *p_total)
{
    (void)dst; (void)max_entries;
    if (p_total) { *p_total = 0; }
    return 0;
}
static inline uint8_t trax_os_snapshot_object_meta(uint8_t *dst, uint8_t max_entries, uint8_t *p_total)
{
    (void)dst; (void)max_entries;
    if (p_total) { *p_total = 0; }
    return 0;
}
static inline uint32_t trax_os_get_free_heap_size(void)         { return 0; }
static inline uint32_t trax_os_get_total_heap_size(void)        { return 0; }
static inline uint32_t trax_os_get_min_ever_free_heap_size(void)   { return 0; }
static inline uint32_t trax_os_get_largest_free_heap_block(void)   { return 0; }
static inline uint32_t trax_os_get_heap_alloc_count(void)          { return 0; }
static inline uint32_t trax_os_get_heap_free_count(void)           { return 0; }
static inline uint32_t trax_os_get_heap_alloc_fail_count(void)     { return 0; }
static inline uint16_t trax_os_get_task_stack_hwm_words(uint32_t handle) {
    (void)handle; return 0;
}

#else /* RTOS or OS build */

/**
 * @brief Initialize RTOS task and object tracking
 *
 * Must be called once from trax_init() before any trace hooks fire.
 */
void trax_os_init(void);

/**
 * @brief Get number of active tasks in the table
 */
uint8_t trax_os_get_task_count(void);

/**
 * @brief Serialize task metadata into destination buffer (capped)
 *
 * Writes AT MOST @p max_entries trax_rtos_task_wire_t records and
 * zero-fills any shortfall up to exactly @p max_entries records, so the
 * output length always matches what the caller allocated.
 *
 * WHY THE CAP: the caller sizes its buffer from trax_os_get_task_count()
 * and this serializer runs later with interrupts ENABLED and preemption
 * possible (deliberately — the HWM query walks task stacks and must not
 * run under a masked window).  The live table can therefore change in
 * between:
 *   - task created in the window  → capped out here; its TASK_CREATE
 *     frame was emitted live (streaming is re-enabled before the fill in
 *     both send_start and send_gap), so the host still learns about it
 *     in order — nothing is lost.
 *   - task deleted in the window  → shortfall is zero-filled; the host
 *     parser drops handle==0 records, and the TASK_DELETE frame follows
 *     live.
 * Without the cap a mid-window creation made this writer emit one more
 * 40-byte record than the caller allocated — silently overrunning the
 * next frame section.
 *
 * Residual (accepted): a single entry mutating mid-copy can yield a torn
 * name string for that one record — cosmetic, corrected by the next
 * resync; fixing it would require masking around the copy.
 *
 * @param dst         Output buffer (max_entries × 40 bytes)
 * @param max_entries Capacity of @p dst in entries (the counted value)
 */
void trax_os_write_task_meta(uint8_t *dst, uint8_t max_entries);

/**
 * @brief Get number of active objects in the table
 */
uint8_t trax_os_get_object_count(void);

/**
 * @brief Serialize object metadata into destination buffer (capped)
 *
 * Same cap + zero-fill contract as trax_os_write_task_meta() — see the
 * race rationale there.
 *
 * @param dst         Output buffer (max_entries × 32 bytes)
 * @param max_entries Capacity of @p dst in entries (the counted value)
 */
void trax_os_write_object_meta(uint8_t *dst, uint8_t max_entries);

/**
 * @brief Get number of CPU cores available to the application
 *
 * Used in the START_TRACE metadata header so the host can pre-allocate
 * per-core swimlanes.
 *   RTOS / NONE  — returns TRAX_CFG_CORE_COUNT (default 1; override in
 *                  App/Config/trax_config.h for multi-core SoCs).
 *
 * @return Number of cores (1–63)
 */
uint8_t trax_os_get_core_count(void);

/**
 * @brief Get the current free heap size
 * @return Free heap size in bytes, or 0 if unknown
 */
uint32_t trax_os_get_free_heap_size(void);

/**
 * @brief Get the total heap size configured for this platform
 *
 * Returns the platform's total heap capacity in bytes.
 * Used to populate the session header for host-side threshold rendering.
 *   FreeRTOS  — configTOTAL_HEAP_SIZE
 *   Zephyr    — CONFIG_HEAP_MEM_POOL_SIZE (when ported)
 *
 * @return Total heap size in bytes, or 0 if unknown / not applicable
 */
uint32_t trax_os_get_total_heap_size(void);

/*=============================================================================
 ====================MEMORY SNAPSHOT API (RTOS ports only)=====================
 =============================================================================
 *
 * Boot-time memory statistics queried once by trax_meta_tx_send_start()
 * and shipped in the SESSION_START frame so the host can show truthful
 * "since boot" memory health when connecting to an already-running
 * target (mid-flight connect is the common embedded debugging pattern).
 *
 * Without these, every Memory View metric would silently restart at
 * "since connect" and the host would mis-report the firmware's actual
 * heap state for hours after a reconnect.
 *
 * All functions return 0 when the underlying RTOS / heap variant cannot
 * supply the value — the host treats 0 as "unknown / not supported" and
 * falls back to host-derived stats.
 */

/** @brief Lowest-ever free heap size since boot (heap_2/4/5; 0 otherwise) */
uint32_t trax_os_get_min_ever_free_heap_size(void);

/** @brief Largest contiguous free block, snapshotted at call time
 *         (heap_4/5 only; 0 otherwise).  NOT ISR-safe — caller must run
 *         from task context (e.g., trax control task at session start). */
uint32_t trax_os_get_largest_free_heap_block(void);

/** @brief Total successful pvPortMalloc() calls since boot */
uint32_t trax_os_get_heap_alloc_count(void);

/** @brief Total vPortFree() calls since boot */
uint32_t trax_os_get_heap_free_count(void);

/** @brief Total pvPortMalloc() failures (traceMALLOC_FAILED) since boot */
uint32_t trax_os_get_heap_alloc_fail_count(void);

/* Counter externs used by the trace macros for zero-call-overhead
 * increment from inside traceMALLOC / traceFREE / traceMALLOC_FAILED.
 * Defined in os/<RTOS>/trax_<rtos>_heap.c. */
extern volatile uint32_t g_trax_heap_alloc_count;
extern volatile uint32_t g_trax_heap_free_count;
extern volatile uint32_t g_trax_heap_alloc_fail_count;

/**
 * @brief Snapshot the current stack high-water-mark for a task, in words.
 *
 * Returns 0 when the underlying RTOS has no per-task HWM API or when the
 * port-specific include guard (e.g. INCLUDE_uxTaskGetStackHighWaterMark
 * on FreeRTOS) is disabled.  Used by trax_os_write_task_meta() to stamp
 * each task's wire record at session start so the host's Memory View
 * shows real headroom on connect (instead of waiting for the next
 * 500-ms stack-monitor poll cycle).
 *
 * NOT ISR-safe: caller must run from task context (typically the trax
 * control task in trax_meta_tx_send_start()).
 */
uint16_t trax_os_get_task_stack_hwm_words(uint32_t handle);

/*=============================================================================
 ====================TASK TABLE API (RTOS ports only)===========================
 ============================================================================*/

/**
 * @brief Record task creation: allocates table slot, sends frame if tracing
 *
 * @param handle     Task control block pointer cast to uint32_t
 * @param priority   Task priority
 * @param stack_size Stack depth in words (0 if unknown)
 * @param name       Null-terminated task name
 * @return           Table index (>=0) on success, -1 if table full
 */
int trax_rtos_task_create(uint32_t handle, uint32_t priority,
                          uint32_t stack_size, const char *name);

/**
 * @brief Record task deletion: frees table slot, sends frame if tracing
 *
 * @param table_index Index previously returned by trax_rtos_task_create()
 * @param handle      Task handle for frame emission
 */
void trax_rtos_task_delete(uint8_t table_index, uint32_t handle);

/**
 * @brief Update task priority in table, sends frame if tracing
 *
 * @param table_index Index previously returned by trax_rtos_task_create()
 * @param handle      Task handle for frame emission
 * @param new_priority New priority value
 */
void trax_rtos_task_update_priority(uint8_t table_index, uint32_t handle,
                                    uint8_t new_priority);

/*=============================================================================
 ====================PER-TASK FLAG WORD========================================
 ============================================================================*/
/* 32-bit flag word stored alongside each task slot.  Bit 0 is wired to
 * the stack-overflow latch so that vApplicationStackOverflowHook ->
 * trax_report_stack_overflow() leaves a persistent breadcrumb the host
 * can ship in the SESSION_START frame even if the user reconnects after
 * the overflow already happened.  Remaining 31 bits reserved for future
 * use (e.g., starvation-suspect, deadline-missed, watchdog-petted). */
#define TRAX_TASK_FLAG_OVERFLOWED   (1U << 0)

/**
 * @brief Set one or more flag bits for the task identified by @p handle.
 *
 * Flags are sticky: bits already set stay set.  Lookup is an O(N) scan
 * over TRAX_CFG_MAX_RTOS_TASKS but is rare (overflow / starvation paths
 * only).  Safe to call from any context the search loop can tolerate:
 * the underlying table layout is single-writer in normal use, but the
 * scan itself is read-mostly and tolerates concurrent set_flag from a
 * fault handler.
 *
 * @param handle  Task handle (TaskHandle_t cast to uint32_t)
 * @param flag    Bitmask of TRAX_TASK_FLAG_* values to OR in
 */
void trax_rtos_task_set_flag(uint32_t handle, uint32_t flag);

/**
 * @brief Read the current flag word for the task identified by @p handle.
 *
 * @param handle  Task handle (TaskHandle_t cast to uint32_t)
 * @return        Flag word, or 0 if @p handle is not in the table.
 */
uint32_t trax_rtos_task_get_flags(uint32_t handle);

/**
 * @brief Latch the *first* stack-overflow timestamp for @p handle.
 *
 * Companion to trax_rtos_task_set_flag(TRAX_TASK_FLAG_OVERFLOWED): the flag
 * tells the host *that* an overflow happened since boot, this call records
 * *when* it happened.  Captures (tick_overflow_cntr, timepacked) at the
 * moment vApplicationStackOverflowHook fires so the Memory View can show
 * the exact source-time after a reconnect, with no further wire traffic.
 *
 * Semantics:
 *   - Idempotent / first-write-wins.  Subsequent calls for the same task
 *     are silently ignored — the very first overflow is the diagnostic
 *     event of interest; later "aftershocks" (uncommon: would require the
 *     user's hook to return without resetting) shouldn't shadow it.
 *   - O(N) scan over TRAX_CFG_MAX_RTOS_TASKS, identical pattern to
 *     trax_rtos_task_set_flag.  Cold path only.
 *   - Both timestamp components zero is reserved as "not latched".  Callers
 *     should pass at least one non-zero value; the unlikely edge case of a
 *     genuine (0, 0) reading is rendered as "before connect" by the host.
 *
 * @param handle              Task handle (TaskHandle_t cast to uint32_t)
 * @param tick_overflow_cntr  trax_timebase.tick_overflow_cntr at fire time
 * @param timepacked          TRAX_FRAME_TIMEPACKED_GET() at fire time
 */
void trax_rtos_task_latch_overflow(uint32_t handle,
                                   uint32_t tick_overflow_cntr,
                                   uint32_t timepacked);

/**
 * @brief Record kernel-driven priority inheritance: same table update as
 *        trax_rtos_task_update_priority() but emits TRAX_TID_TASK_PRIORITY_INHERIT
 *        instead of TRAX_TID_TASK_PRIORITY_SET so the host can distinguish a
 *        kernel-driven boost (mutex inheritance) from an explicit
 *        application call to vTaskPrioritySet().
 */
void trax_rtos_task_priority_inherit(uint8_t table_index, uint32_t handle,
                                     uint8_t inherited_priority);

/**
 * @brief Record kernel-driven priority disinheritance: same table update as
 *        trax_rtos_task_update_priority() but emits
 *        TRAX_TID_TASK_PRIORITY_DISINHERIT.  Fires when the holder of the boosted
 *        mutex gives it back and the kernel restores the original priority.
 */
void trax_rtos_task_priority_disinherit(uint8_t table_index, uint32_t handle,
                                        uint8_t restored_priority);

/*=============================================================================
 ====================OBJECT TABLE API (RTOS ports only)========================
 ============================================================================*/

/**
 * @brief Record object creation: allocates table slot, sends frame if tracing
 *
 * @param handle     Object pointer cast to uint32_t
 * @param obj_type   Object type identifier (e.g., queue, semaphore, mutex)
 * @param length     Queue depth / semaphore max count / 1 for mutex
 * @param item_size  Queue item size / 0 for semaphores and mutexes
 * @return           Table index (>=0) on success, -1 if table full
 */
int trax_rtos_object_create(uint32_t handle, uint8_t obj_type,
                            uint32_t length, uint32_t item_size);

/**
 * @brief Record object deletion: frees table slot, sends frame if tracing
 *
 * @param table_index Index previously returned by trax_rtos_object_create()
 * @param handle      Object handle for frame emission
 */
void trax_rtos_object_delete(uint8_t table_index, uint32_t handle);

/**
 * @brief Update object name in table, sends frame if tracing
 *
 * @param table_index Index previously returned by trax_rtos_object_create()
 * @param handle      Object handle for frame emission
 * @param name        Null-terminated name string
 */
void trax_rtos_object_set_name(uint8_t table_index, uint32_t handle,
                               const char *name);

/**
 * @brief Find an object table index by handle, or -1 if not present.
 *
 * Linear scan over TRAX_CFG_MAX_RTOS_OBJECTS.  Used by the
 * trax_rtos_object_*_by_handle() helpers below for stream-buffer and
 * timer hooks: StreamBuffer_t / Timer_t carry no uxQueueNumber-equivalent
 * field for an O(1) round-trip, so we pay a bounded scan instead.  The
 * handle == 0 sentinel matches nothing (the slot's vacant state is also
 * encoded as handle == 0, so vacant slots are filtered implicitly).
 *
 * @param handle Object pointer cast to uint32_t
 * @return       Table index (>=0) on success, -1 if not found
 */
int trax_rtos_object_find_by_handle(uint32_t handle);

/**
 * @brief Delete an object by handle (no table_index round-trip required).
 *
 * Convenience wrapper around trax_rtos_object_find_by_handle() +
 * trax_rtos_object_delete().  Used by traceSTREAM_BUFFER_DELETE and
 * traceTIMER_DELETE where the macro caller has no stash slot for the
 * table index produced at create time.  No-op (silently) if the handle
 * is not registered — e.g. tracing was started after the object was
 * created and no OBJ_CREATE was ever recorded for it.
 *
 * @param handle Object pointer cast to uint32_t
 */
void trax_rtos_object_delete_by_handle(uint32_t handle);

/**
 * @brief Update an object's name by handle (no table_index round-trip).
 *
 * Convenience wrapper around trax_rtos_object_find_by_handle() +
 * trax_rtos_object_set_name().  Used by application code wanting to
 * give a name to a stream / message buffer (which otherwise has no
 * pcName-equivalent field) after xStreamBufferCreate() returns.
 * Silently no-op if the handle is not registered.
 *
 * @param handle Object pointer cast to uint32_t
 * @param name   Null-terminated name string
 */
void trax_rtos_object_set_name_by_handle(uint32_t handle, const char *name);

/**
 * @brief Get handles of all active tasks for memory monitoring
 *
 * Iterates the task table and copies active task handles into the
 * caller-supplied array. Used by trax_memory_monitor_process() to
 * poll stack watermarks without exposing the internal table.
 *
 * @param p_handles  Output array for task handles
 * @param max_count  Maximum entries to fill
 * @return           Number of handles written
 */
uint8_t trax_os_get_task_handles(uint32_t *p_handles, uint8_t max_count);

/*=============================================================================
 ====================FAULT-SAFE SNAPSHOT API (TraxFault dynmeta)===============
 ============================================================================*/
/*
 * Bounded, fault-handler-safe variants of trax_os_write_*_meta().  Unlike
 * the session-start serializers they:
 *   - stop after max_entries (the caller's buffer is a fixed noinit block),
 *   - make NO RTOS calls (the HWM query walks task stacks — unacceptable
 *     inside a fault handler on a possibly-corrupted system; the wire
 *     field is written as 0 = unknown),
 *   - report the LIVE table count through p_total so the host can detect
 *     truncation (stored < total) and tell the user to raise
 *     TRAX_CFG_FAULT_DYNMETA_TASKS / _OBJECTS.
 */

/**
 * @brief Copy up to @p max_entries task-table entries into @p dst as
 *        trax_rtos_task_wire_t records.  Fault-handler safe.
 *
 * @param dst          Output buffer (max_entries × 40 bytes)
 * @param max_entries  Capacity of @p dst in entries
 * @param p_total      Optional: receives the live table count
 * @return             Entries actually written (<= max_entries)
 */
uint8_t trax_os_snapshot_task_meta(uint8_t *dst, uint8_t max_entries, uint8_t *p_total);

/**
 * @brief Copy up to @p max_entries object-table entries into @p dst as
 *        trax_rtos_object_wire_t records.  Fault-handler safe.
 *
 * @param dst          Output buffer (max_entries × 32 bytes)
 * @param max_entries  Capacity of @p dst in entries
 * @param p_total      Optional: receives the live table count
 * @return             Entries actually written (<= max_entries)
 */
uint8_t trax_os_snapshot_object_meta(uint8_t *dst, uint8_t max_entries, uint8_t *p_total);

#endif /* TRAX_RTOS_NONE */

#ifdef __cplusplus
}
#endif

#endif /* TRAX_RTOS_TABLES_H_ */
