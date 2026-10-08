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
 * @file           : trax_session.h
 * @brief          : TraxProbe Session and State Management
 * @version        : 7.0.0
 ******************************************************************************
 * @attention
 *
 * SIMPLIFIED FLAG MODEL:
 *   The probe is always tracing after trax_init(). Streaming and recording
 *   are independent capabilities controlled by boolean flags.
 *
 *   initialized  — trax_init() has been called (probe is always tracing)
 *   streaming    — ring buffer writes + real-time transmission to host
 *
 * API:
 *   trax_session_init()              — Sets initialized = 1
 *   trax_session_enable_streaming()  — Sets streaming = 1
 *   trax_session_disable_streaming() — Sets streaming = 0
 *
 *   These are internal flag flips. The public session API (trax.h) is
 *   trax_session_start/stop/pause/resume() and trax_wait_session_started().
 *
 * PENDING-STOP LATCH (FATAL ERROR LIFECYCLE):
 *   On a fatal stream error (frame validator reject, transport
 *   hard-error) the firmware needs to (a) immediately stop new
 *   application writes from landing in a known-bad ring, and (b) tell the
 *   host *why* the stream is stopping. To keep the hot path zero-overhead
 *   and to guarantee the STOP frame is the unambiguous last frame on the
 *   wire, the lifecycle is split into two phases:
 *
 *     Phase 1 — Hot path (alloc-fail callback / send-frames validator hit):
 *       trax_session.streaming is flipped to 0 and the pending-stop latch
 *       is armed via trax_session_arm_stop(reason, info). First arm wins:
 *       a later, less-critical reason cannot displace the original.
 *
 *     Phase 2 — Slow path (trax_process, after trax_send_frames returns):
 *       trax_session_check_stop() reads back the latched reason+info and
 *       clears the latch. The caller (trax_process) emits a single
 *       TRAX_TID_SESSION_STOP via trax_meta_tx_send_stop() — which uses
 *       TRAX_FRAME_ARGS_PROTOCOL and is therefore NOT gated by
 *       TRAX_IS_SESSION_ACTIVE(). The frame lands at p_wr32 behind any
 *       pre-error frames already queued, so the host sees the autopsy
 *       in chronological order.
 *
 *   The STOP frame body carries the at-stop diag autopsy snapshot
 *   (80 bytes, built by trax_diag_build_report inside the same critical
 *   section as the reason/info fields), so the host gets reason +
 *   counters atomically in a single frame.
 *
 * SESSION COUNTER:
 *   - Incremented on trax_init()
 *   - Helps Traxcope detect firmware restarts
 *
 * HOT PATH OPTIMIZATION:
 *   - Use TRAX_IS_SESSION_ACTIVE()    for trace frame writes
 *   - Use TRAX_IS_INITIALIZED()  to check if trax_init() has been called
 *
 ******************************************************************************
 */

#ifndef TRAX_SESSION_H_
#define TRAX_SESSION_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 ====================GLOBAL MACRO FUNCTIONS====================================
 ============================================================================*/

/**
 * @brief Check if TraxProbe has been initialized
 *
 * True after trax_init(). The probe is always tracing once initialized.
 *
 * @return Non-zero if initialized, 0 if not
 */
#define TRAX_IS_INITIALIZED() (trax_session.initialized)

/**
 * @brief Check if the data stream to host is active
 *
 * Use this for all trace frame writes (logs, variables, streams, RTOS events).
 * True when ring buffer writes and transmission are enabled.
 *
 * @return Non-zero if streaming, 0 if not
 */
#define TRAX_IS_SESSION_ACTIVE() (trax_session.streaming)

/**
 * @brief Count a producer frame suppressed by the streaming gate.
 *
 * Placed on the FAILURE branch of the TRAX_IS_SESSION_ACTIVE() check in the
 * TRAX_FRAME_* producer macros, so the streaming hot path pays nothing.
 * Only counts while a gap episode is armed: a deliberate stop (user or
 * fatal) suppresses frames too, but those are not "gap losses" and the
 * idle probe keeps its near-zero per-call cost (one extra load+branch).
 *
 * The increment is intentionally unsynchronised — see gap_skip_cntr.
 */
#define TRAX_GAP_NOTE_SKIP() \
    do { \
        if (trax_session.pending_gap_armed) { \
            trax_session.gap_skip_cntr++; \
        } \
    } while (0)

/*=============================================================================
 ====================GLOBAL TYPEDEF============================================
 ============================================================================*/

/**
 * @brief Why a SESSION_GAP resync frame re-enabled streaming.
 *
 * Carried in the gap header so the host can render "ring overflow" vs
 * "paused by user, resumed by user" vs "paused, resumed by trigger X".
 */
enum trax_resume_reason_t {
    TRAX_RESUME_REASON_OVERFLOW = 0,  /**< Ring-overflow gap episode (classic accordion) */
    TRAX_RESUME_REASON_USER     = 1,  /**< Paused and resumed explicitly (API / host command) */
    TRAX_RESUME_REASON_TRIGGER  = 2,  /**< Paused; resumed by TRAX_TRIGGER_FIRE() — trigger
                                           TID/ctx/anchor ride in the gap header */
};

/**
 * @brief TraxProbe session state structure
 *
 * Contains only runtime state. All fields are volatile for interrupt safety.
 * The probe is always tracing after trax_init(). Streaming and recording
 * are independent boolean flags (not a state enum).
 *
 * Layout note: pending_stop_armed/reason are packed into the same word as
 * initialized/streaming so the hot-path "is streaming?" test and the
 * "was a fatal stop armed?" test both touch a single cache line.
 */
struct trax_session_t {
    volatile uint8_t  initialized;              /**< trax_init() called */
    volatile uint8_t  streaming;                /**< Sending data to host */
    volatile uint8_t  pending_stop_armed;       /**< 0 = idle, 1 = STOP queued for next trax_process() */
    volatile uint8_t  pending_stop_reason;      /**< enum trax_stop_reason_t value, valid only when armed */
    volatile uint32_t session_id;               /**< Session counter (increments on init) */
    volatile uint32_t frame_count;              /**< Frames written this session */
    volatile uint32_t pending_stop_info;        /**< Reason-specific payload, valid only when armed */

    /* Ring-overflow gap latch. Armed by trax_buffer_on_alloc_fail();
     * consumed by trax_process() once the ring is quiescent
     * (drain → TRAX_TID_SESSION_GAP resync → streaming re-enabled). */
    volatile uint8_t  pending_gap_armed;        /**< 0 = idle, 1 = gap resume queued */
    volatile uint32_t gap_start_tick_overflow;  /**< trax_timebase.tick_overflow_cntr at first failed alloc */
    volatile uint32_t gap_start_timepacked;     /**< Packed timestamp at first failed alloc */
    volatile uint32_t gap_start_fail_cntr;      /**< trax_buffer.alloc_fail_cntr snapshot BEFORE the first
                                                     failure of this gap — resume computes the drop count as
                                                     (current alloc_fail_cntr - this) */
    volatile uint32_t gap_cntr;                 /**< Gap episodes completed (cumulative since boot, like
                                                     alloc_fail_cntr — the host tracks per-session counts) */
    volatile uint32_t gap_skip_cntr;            /**< Producer frames suppressed at the TRAX_IS_SESSION_ACTIVE()
                                                     gate while a gap episode was armed (cumulative since
                                                     boot). Plain non-atomic increments from nested ISRs may
                                                     occasionally lose a count — the value is a documented
                                                     lower bound, good enough for loss diagnostics. */
    volatile uint32_t gap_start_skip_cntr;      /**< gap_skip_cntr snapshot at arm — resume reports the
                                                     per-episode skip count as the delta */

    /* Pause / trigger extension of the gap latch (see trax_trigger.h).
     * pause_hold blocks trax_process()'s automatic gap resume; releasing
     * it (user resume or trigger fire) lets the existing gap machinery
     * emit the resync frame. All O(1) byte/word stores — no new cost on
     * any hot path. */
    volatile uint8_t  pause_hold;               /**< 1 = gap resume held (stream paused) */
    volatile uint8_t  resume_reason;            /**< enum trax_resume_reason_t for the next resync */
    volatile uint8_t  trigger_latched;          /**< First-wins: trigger fields below are valid */
    volatile uint16_t trigger_tid;              /**< Trigger TID that released the pause */
    volatile uint32_t trigger_ctx;              /**< User context word from TRAX_TRIGGER_FIRE */
    volatile uint32_t trigger_tick_overflow;    /**< Timebase overflow counter at fire */
    volatile uint32_t trigger_timepacked;       /**< Packed timestamp at fire — the exact trigger
                                                     time (the resync frame itself is emitted up to
                                                     one trax_process() pass later) */
};

/*=============================================================================
 ====================GLOBAL VARIABLES==========================================
 ============================================================================*/
/**
 * @brief Global session state
 *
 * Single source of truth for TraxProbe state.
 */
extern struct trax_session_t trax_session;

/*=============================================================================
 ====================GLOBAL FUNCTION DECLARATION===============================
 ============================================================================*/

/**
 * @brief Initialize session state
 *
 * Called from trax_init(). Sets initialized = 1, streaming = 0.
 * Increments session_id and clears buffer.
 */
void trax_session_init(void);

/**
 * @brief Enable stream to host
 *
 * Sets streaming = 1. Ring buffer writes and transmission begin.
 *
 * @note If already streaming, this has no effect.
 */
void trax_session_enable_streaming(void);

/**
 * @brief Disable stream to host
 *
 * Sets streaming = 0. Ring buffer writes and transmission stop.
 * The probe continues tracing.
 *
 * @note If not streaming, this has no effect.
 */
void trax_session_disable_streaming(void);

/**
 * @brief Arm the pending-stop latch (first writer wins).
 *
 * Records the reason+info that trax_process() will report to the host on
 * its next pass. If the latch is already armed (a previous fatal error
 * already reported its reason this session) the call is a no-op — the
 * original reason is preserved so the host always sees the *first* cause.
 *
 * Thread / ISR safe in the "good enough" sense: the read-then-write race
 * between two ISRs both arming for the first time is benign because the
 * second writer's reason+info is also valid (the fatal condition is
 * symmetric). Per-field volatility is sufficient on the supported MCUs;
 * no critical section is taken here so the call is safe from inside the
 * TRAX_BUFF_ALLOC critical section without nesting.
 *
 * Order of writes is deliberate: info and reason land first, then the
 * armed byte. trax_session_check_stop() reads armed first, so it
 * cannot observe a half-built record.
 *
 * @param reason  enum trax_stop_reason_t value (cast to uint8_t)
 * @param info    Reason-specific 32-bit context (e.g. validator-fail
 *                counter for FRAME_CORRUPT). Pass 0 if not applicable.
 */
void trax_session_arm_stop(uint8_t reason, uint32_t info);

/**
 * @brief Consume the pending-stop latch if it was armed.
 *
 * Single-shot read: if armed, copies reason+info to the out params, clears
 * the armed byte, and returns true. If not armed, returns false and leaves
 * the out params untouched.
 *
 * Called exclusively from trax_process() context (single-threaded), so no
 * critical section is needed to make the read+clear sequence atomic.
 *
 * @param[out] p_reason  Receives the latched reason on hit. Must be non-NULL.
 * @param[out] p_info    Receives the latched info on hit.   Must be non-NULL.
 * @return true if the latch was armed (caller should emit STOP),
 *         false if no pending stop is pending.
 */
bool trax_session_check_stop(uint8_t *p_reason, uint32_t *p_info);

/**
 * @brief Arm the ring-overflow gap latch (first writer wins).
 *
 * Called from trax_buffer_on_alloc_fail() (cold path, inside the failing
 * producer's critical section) when a frame allocation finds the ring full.
 * On the FIRST arm of an episode it captures the gap start time anchor
 * (trax_timebase.tick_overflow_cntr + packed timestamp) and the
 * alloc-fail counter baseline; subsequent calls during the same gate are
 * no-ops so repeated rejected allocs cost one load + branch.
 *
 * The caller has already flipped trax_session.streaming = 0, which is
 * what actually gates producers — this latch only records the episode
 * for the resume path in trax_process().
 */
void trax_session_arm_gap(void);

/**
 * @brief Clear the gap latch without resuming.
 *
 * Called when something overrides a pending gap resume:
 *   - a fatal pending-stop fired (STOP frame wins, session is dead),
 *   - CMD_SESSION_STOP / trax_session_stop() during a gate (the user
 *     asked for a real stop — resuming afterwards would be wrong),
 *   - a fresh streaming start (trax_session_enable_streaming() calls
 *     this internally).
 *
 * Also clears the pause hold and trigger latch — a stop wins over a
 * pending pause/trigger resume.
 */
void trax_session_clear_gap(void);

/**
 * @brief Pause the stream: gate producers and hold the gap resume.
 *
 * Same producer gate as an overflow episode (streaming = 0 → one
 * load+branch per suppressed frame, counted in gap_skip_cntr), but the
 * latch is HELD: trax_process() drains the ring (everything captured
 * before the pause still reaches the host) and then waits instead of
 * auto-resuming. Release with trax_session_release_pause() or a
 * TRAX_TRIGGER_FIRE().
 *
 * No-op unless currently streaming (a stream already gated by an
 * overflow episode keeps its automatic resume).
 */
void trax_session_arm_pause(void);

/**
 * @brief Release a held pause; the next trax_process() pass emits the
 *        SESSION_GAP resync (full dynamic snapshot) and resumes streaming.
 *
 * @param reason  TRAX_RESUME_REASON_USER for API/host resume. (Trigger
 *                fires go through trax_session_latch_trigger instead.)
 *
 * No-op when no pause is held.
 */
void trax_session_release_pause(uint8_t reason);

/**
 * @brief Trigger-fire latch: release a held pause and record attribution.
 *
 * ISR-safe, O(1): first-wins latch of (tid, ctx, time anchor) inside a
 * short critical section — same masking class as any frame emission.
 * No-op when no pause is held (a trigger fired while streaming live is
 * just its event frame) or when a trigger already fired this episode.
 *
 * Called by TRAX_TRIGGER_FIRE(); not intended for direct use.
 */
void trax_session_latch_trigger(uint16_t tid, uint32_t ctx);


#ifdef __cplusplus
}
#endif

#endif /* TRAX_SESSION_H_ */
