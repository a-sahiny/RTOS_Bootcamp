/**
 ******************************************************************************
 * @file           : trax_config.reference.h   (REFERENCE — all options)
 * @brief          : Catalogue of every user-settable TraxProbe macro
 ******************************************************************************
 * This is a MENU, not a ready-to-use config. Every line is commented out, so
 * as written this file overrides nothing. Use it two ways:
 *
 *   1. As a lookup — find a knob, its default, and its allowed values.
 *   2. As a starting point — copy to your include path as `trax_config.h`,
 *      then uncomment and edit the entries you want.
 *
 * Notes:
 *   - The value shown after each macro is the LIBRARY DEFAULT (what you get
 *     when the macro is left undefined), UNLESS marked "REQUIRED" — those have
 *     no default and must be set (see trax_config.minimal.h).
 *   - Each knob lives in a config/trax_config_*.h module (named per section);
 *     those files are library internals — DO NOT edit them. Override here.
 *   - The named constants below come from config/trax_config_options.h, which
 *     the library includes before your trax_config.h.
 ******************************************************************************
 */

#ifndef APP_TRAX_CONFIG_H_
#define APP_TRAX_CONFIG_H_

#include "trax_tid.h"   /* TID range anchors, only needed for the TID enums below */

/*=============================================================================
 * MASTER SWITCH                                  (inc/trax_config_default.h)
 *===========================================================================*/
// #define TRAX_ENABLE                    1   // 1=on (default). 0 = compile the entire library out (zero footprint)

/*=============================================================================
 * HARDWARE PORT                                  (inc/trax_config_default.h)
 *===========================================================================*/
// #define TRAX_CFG_HW_PORT               TRAX_HW_PORT_ARM_CORTEX_M  // REQUIRED. _ARM_CORTEX_M | _ZYNQ | _XTENSA | _MICROBLAZE | _GENERIC

// --- Cortex-M only -------------------------------------------------------
// #define TRAX_CFG_BASEPRI               configMAX_SYSCALL_INTERRUPT_PRIORITY  // M3+: mask by priority instead of PRIMASK

// --- Zynq (Cortex-A/R) only ----------------------------------------------
// Cortex-A/R has no IPSR, and its FreeRTOS ports have no traceISR_ENTER()
// hook, so ISR-context detection must be wired or the trace timebase never
// advances. Pick ONE (a FreeRTOS build that picks neither fails to compile):
// extern volatile uint32_t ulPortInterruptNesting;   // ARM_CA9 — declare EXACTLY as your port.c defines it (CR5: uint32_t, no volatile; CA53: uint64_t ullPortInterruptNesting)
// #define TRAX_CFG_IRQ_NESTING_COUNTER   ulPortInterruptNesting  // reuse the RTOS port's counter (preferred)
// #define TRAX_CFG_ZYNQ_OWN_IRQ_NESTING  1     // or let TraxProbe keep its own; wrap GIC dispatch with trax_zynq_irq_enter/exit()
// #define TRAX_CFG_ZYNQ_MASK_FIQ         1     // default. 1 = critical sections mask IRQ+FIQ; 0 = IRQ only (FIQ must not call TRAX_*)
// #define TRAX_PORT_ZYNQ_GLOBAL_TIMER_BASE 0xF8F00200U  // A9 MPCore global timer base

// --- MicroBlaze only -----------------------------------------------------
// Build with -mlittle-endian; mb-gcc defaults to big endian, which the wire
// format does not support (build error). No architectural counter exists, so
// FREERUN needs a peripheral base (build error if missing):
// #define TRAX_CFG_MB_TIMER_BASE         XPAR_AXI_TIMER_0_BASEADDR  // TCR0 read at +0x08
// #define TRAX_CFG_MB_USE_MSR_INSTR      0     // default. 1 = msrclr/msrset (needs C_USE_MSR_INSTR=1 hardware)
// #define TRAX_CFG_MB_OWN_IRQ_NESTING    1     // TraxProbe-owned ISR nesting counter (FreeRTOS builds need one)

/*=============================================================================
 * TRANSPORT                          (transports/RTT or your custom transport .c)
 *===========================================================================*/
// #define TRAX_CFG_TRANSPORT             TRAX_TRANSPORT_RTT  // default. _RTT | _CUSTOM
// Custom: set _CUSTOM and implement four trax_transport_*() functions — init,
// write, read, clear_tx (see trax_transport.h) — bound at link time, no struct,
// no register call. The knobs below are all OPTIONAL: each names one of your
// own functions and defaults to the behaviour a transport had without it.
// #define TRAX_CFG_TRANSPORT_TX_FREE     my_tx_free       // recommended. size_t(void) TX free space; default: unknown (try-and-retry)
// #define TRAX_CFG_TRANSPORT_BUF_SIZE    sizeof(my_ring)  // diagnostics. TX capacity for the host gauge; default: 0 (unknown)
// #define TRAX_CFG_TRANSPORT_BYTES_USED  my_tx_pending    // diagnostics. size_t(void) exact TX occupancy; default: BUF_SIZE - TX_FREE

/*=============================================================================
 * TIMESTAMP / TIMER                               (config/trax_config_hw_port.h)
 *===========================================================================*/
// #define TRAX_CFG_TIMESTAMP_MODE        TRAX_TIMESTAMP_TICK_TIMER // default. _TICK_TIMER | _FREERUN (e.g. DWT->CYCCNT)
// #define TRAX_CFG_TICK_BITS             8                    // tick bits in the 32-bit stamp: [tick:N][fine:32-N]
// #define TRAX_CFG_TIMESTAMP_TIMER_DIR   TRAX_TIMER_DIR_DOWN  // default per mode: _DOWN in TICK_TIMER (SysTick), _UP in FREERUN (every free-running counter counts up). Override only for a genuine 32-bit down-counter.
// #define TRAX_CFG_TIMESTAMP_TIMER_VAL   (SysTick->VAL)       // REQUIRED in TICK_TIMER mode — fine-time register read
// #define TRAX_CFG_TIMER_FREQ_HZ         64000000U            // REQUIRED — timer/core clock in Hz
// #define TRAX_CFG_TIMER_FREQ_DIV        1U                   // divisor for fractional clocks: freq = HZ/DIV
// #define TRAX_CFG_TICK_COUNTER_PERIOD   64000U               // REQUIRED in TICK_TIMER mode — fine cycles per tick (LOAD+1)

/*=============================================================================
 * MULTI-CORE / MULTI-PROBE SYNC                   (config/trax_config_hw_port.h)
 *===========================================================================*/
// #define TRAX_CFG_CORE_COUNT            1U                   // cores running THIS image (SMP). 1..63
// #define TRAX_CFG_SYNC_ROLE             TRAX_SYNC_ROLE_NONE  // default. _NONE | _MASTER | _SLAVE
// #define TRAX_CFG_SYNC_GPIO_SET_LOW()   LL_GPIO_ResetOutputPin(GPIOB, LL_GPIO_PIN_4)  // MASTER only
// #define TRAX_CFG_SYNC_GPIO_SET_HIGH()  LL_GPIO_SetOutputPin(GPIOB, LL_GPIO_PIN_4)    // MASTER only
// #define TRAX_CFG_SYNC_FILTER_DELAY_NUM 0U                   // input-filter delay numerator   (seconds = NUM/DEN)
// #define TRAX_CFG_SYNC_FILTER_DELAY_DEN 1U                   // input-filter delay denominator

/*=============================================================================
 * RTOS                                            (config/trax_config_rtos.h)
 *===========================================================================*/
// #define TRAX_CFG_RTOS_TYPE             TRAX_RTOS_NONE       // default. _NONE | TRAX_RTOS_FREERTOS
// #define TRAX_CFG_FREERTOS_VERSION      TRAX_FREERTOS_VERSION(11, 2, 0)
//                                                             // MANDATORY with TRAX_RTOS_FREERTOS: kernel version
//                                                             // in your build (task.h: tskKERNEL_VERSION_NUMBER).
//                                                             // Supported: V10.2.0 .. V11.2.x. Also feeds the OS
//                                                             // version in START_TRACE metadata.
// #define TRAX_CFG_OS_VER_MAJOR          0                    // OS version reported in START_TRACE
// #define TRAX_CFG_OS_VER_MINOR          0                    //   (non-FreeRTOS only — FreeRTOS derives these
// #define TRAX_CFG_OS_VER_PATCH          0                    //    from TRAX_CFG_FREERTOS_VERSION)
// #define TRAX_CFG_MAX_RTOS_TASKS        16                   // FreeRTOS: task table size
// #define TRAX_CFG_MAX_RTOS_OBJECTS      16                   // FreeRTOS: sync-object table size
// #define TRAX_CFG_RTOS_TASK_NAME_MAX    16                   // stored task-name length (try configMAX_TASK_NAME_LEN)
// #define TRAX_CFG_CTRL_TASK_PRIORITY    1                    // drain task prio (just above IDLE) — see contract in rtos.h
// #define TRAX_CFG_CTRL_TASK_STACK_SIZE  256                  // words. Bump to 512 if trace-start overflows
// #define TRAX_CFG_CTRL_TASK_PERIOD_MS   10                   // drain wake period
// #define TRAX_CFG_ISR_YIELD_TO_SCHEDULER 1                   // 1=omit ISR_EXIT when a switch is pending (default)
//                                                             // 0=emit ISR_EXIT and show the 2–5 µs Cortex-M resume of the preempted task
// #define TRAX_CFG_OWN_FREERTOS_TICK_HOOK 1                   // 1=TraxProbe owns vApplicationTickHook (default).
//                                                             // CubeMX: uncheck USE_TICK_HOOK, delete generated stub.
//                                                             // App tick work: override trax_app_tick_hook(). 0=you own the hook.

/*=============================================================================
 * BUFFERS                                         (config/trax_config_buffer.h)
 *===========================================================================*/
// #define TRAX_CFG_OUT_BUFFER_SIZE32     2048U                // output ring buffer, 32-bit words (2048 = 8 KB)
// #define TRAX_CFG_IN_BUFFER_SIZE32      16U                  // host->device command buffer, words
// #define TRAX_CFG_FRAME_MAX_SIZE32      65535U               // max frame words (16-bit field ceiling)
// #define TRAX_CFG_START_RUNDOWN_POLL_LIMIT 100u              // restart: max trax_process() passes to wait for ring quiescence before forcing the reset

/*=============================================================================
 * METADATA / IDENTIFICATION                       (config/trax_config_meta.h)
 *===========================================================================*/
// #define TRAX_CFG_META_STORAGE          TRAX_META_IN_FLASH   // default. _ELF_ONLY (host reads .elf) | _IN_FLASH (on wire)
// #define TRAX_CFG_PROJECT_NAME          "Unknown"            // shown in the host report
// #define TRAX_CFG_BUILD_VERSION         ""                   // e.g. "1.4.2-rc3"
// #define TRAX_CFG_BUILD_ID              __DATE__ "T" __TIME__ // build fingerprint (git short hash, CI id, ...)
// #define TRAX_CFG_META_NAME_LEN         32                   // signal/marker/ISR name length   (multiple of 4)
// #define TRAX_CFG_META_TAG_LEN          32                   // log tag (Context column) length (multiple of 4)
// #define TRAX_CFG_META_UNIT_LEN         8                    // signal unit length              (multiple of 4)
// #define TRAX_CFG_META_FORMAT_LEN       64                   // log format-string length        (multiple of 4)
// #define TRAX_CFG_META_DESC_LEN         32                   // stream description length       (multiple of 4)
// #define TRAX_CFG_META_PROJECT_LEN      32                   // project-name field length       (multiple of 4)
// #define TRAX_CFG_META_VERSION_LEN      16                   // build-version field length      (multiple of 4)
// #define TRAX_CFG_META_BUILD_ID_LEN     24                   // build-id field length           (multiple of 4)
// #define TRAX_CFG_FILTER_MAX_ORDER      12                   // max var transfer-function order (wire contract - needs a matching Traxcope build)
// #define TRAX_CFG_LOG_MAX_ARGS          24                   // max format args per log (even, 1..24)
// #define TRAX_CFG_FORMULA_EXPR_LEN      48                   // var-formula expr length (8..256, multiple of 4; stored as expr_len)

/*=============================================================================
 * LOGGING                                         (config/trax_config_log.h)
 *===========================================================================*/
// NOTE: these two have NO "TRAX_CFG_" prefix — the library reads them verbatim
//       (config/trax_config_log.h). Spelling them TRAX_CFG_* has no effect.
// #define TRAX_LOG_COMPILE_LEVEL             TRAX_LOG_LEVEL_TRACE // compile-time ceiling. _EMERGENCY(0).._TRACE(8)
//   Per-file override: #define TRAX_FILE_LOG_LEVEL <level> BEFORE #include "trax_log.h" inside a .c file.

/*=============================================================================
 * SELF-DIAGNOSTICS                                (config/trax_config_diag.h)
 *===========================================================================*/
// #define TRAX_CFG_DIAG_MAX_SUBSCRIBERS  2                    // concurrent diag subscribers (>= 1)
// #define TRAX_CFG_DIAG_DEFAULT_LOGS     1                    // 1=library emits critical-event logs; 0=silent
// #define TRAX_CFG_DIAG_REPORT_PERIOD_MS 1000u                // periodic snapshot cadence (>= 100)

/*=============================================================================
 * MEMORY MONITORING  (heap/stack health)         (inc/internal/trax_memory.h)
 *===========================================================================*/
// #define TRAX_CFG_STACK_MONITOR_INTERVAL_MS 500              // stack high-water poll period (self-throttled)
// #define TRAX_CFG_HEAP_WARN_BYTES       2048                 // heap free below this -> warning zone
// #define TRAX_CFG_HEAP_CRITICAL_BYTES   512                  // heap free below this -> danger zone
// #define TRAX_CFG_STACK_WARN_PERCENT    20                   // warn at N% stack remaining (0 = disabled)
// #define TRAX_CFG_STACK_CRITICAL_PERCENT 10                  // critical at N% stack remaining (0 = disabled)

/*=============================================================================
 * DEBUG AIDS
 *===========================================================================*/
// On-target frame validation (debug/trax_frame_validate.c) is now a MANDATORY
// always-on part of the self-diagnostic family — there is no knob. The former
// TRAX_CFG_DEBUG_FRAME_VALIDATE toggle was removed in v3.0.0; the validator's
// error counters (trax_validate_err_*) are inspectable in a debugger at any
// breakpoint. See debug/trax_frame_validate.h.

/*=============================================================================
 * STREAMS                                         (inc/trax_stream.h)
 *===========================================================================*/
// #define TRAX_CFG_STREAM_CNT               0                    // count of high-rate var streams. MUST be a plain literal.

/*=============================================================================
 * STATE MACHINES                                  (inc/trax_sm.h)
 *===========================================================================*/
// #define TRAX_CFG_SM_CNT                   0                    // count of TRAX_SM_DEFINE state machines. Sizes the
                                                                  // current-state shadow table snapshotted into
                                                                  // SESSION_START / SESSION_GAP (late join + gap resync).

/*=============================================================================
 * CRITICAL SECTION MASK — hard-real-time ISRs   (hw_port/ARM_Cortex_M)
 *===========================================================================*/
// #define TRAX_CFG_BASEPRI  configMAX_SYSCALL_INTERRUPT_PRIORITY // Cortex-M3/M4/M7/M33+ only. Switches TraxProbe's
                                                                  // critical section from PRIMASK (masks everything) to a
                                                                  // BASEPRI raise: ISRs MORE urgent than this value are
                                                                  // NEVER delayed by TraxProbe — and MUST NOT call any
                                                                  // TRAX_* macro (same contract as FreeRTOS FromISR APIs).
                                                                  // Raw shifted 8-bit register value, non-zero.

/*=============================================================================
 * TRACE IDs  — define your own, anchored at the matching *_USER_START
 *===========================================================================*/
// enum { TID_LOG_BOOT    = TRAX_TID_RANGE_LOG_USER_START };    // LOG    0x0100-0x1FFF
// enum { TID_VAR_RPM     = TRAX_TID_RANGE_VAR_USER_START };    // VAR    0x2000-0x2FFF
// enum { TID_STREAM_ADC  = TRAX_TID_RANGE_STREAM_USER_START }; // VAR STREAM 0x3000-0x3FFF (keep TRAX_CFG_STREAM_CNT in sync)
// enum { TID_ISR_TIM2    = TRAX_TID_RANGE_ISR_USER_START };    // ISR    0x4000-0x4FFF
// enum { TID_MARKER_LOOP = TRAX_TID_RANGE_MARKER_USER_START }; // MARKER 0x5000-0x5FFF
// enum { TID_SM_SYSTEM   = TRAX_TID_RANGE_SM_USER_START };     // SM     0x7000-0x70FF (keep TRAX_CFG_SM_CNT in sync)
// enum { TID_TRG_OVERVOLT = TRAX_TID_RANGE_TRIGGER_USER_START }; // TRIGGER 0x7200-0x72FF (pause/resume on rare conditions — inc/trax_trigger.h)

#endif /* APP_TRAX_CONFIG_H_ */
