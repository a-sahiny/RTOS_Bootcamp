# Pitfall: RTOS API Calls in Trace Macro Arguments

**Never call an RTOS API (or any function that manipulates the global
interrupt mask) inside the argument list of a TraxProbe frame macro.**

```c
/* WRONG — uxQueueMessagesWaiting() is evaluated INSIDE TraxProbe's
 * critical section and re-enables interrupts on its way out. */
TRAX_VAR_SET(VAR_TID_QUEUE_DEPTH,
             (uint32_t)uxQueueMessagesWaiting(s_work_q));

/* CORRECT (preferred) — use the FromISR query variant. TraxProbe already
 * manages the interrupt mask itself, so the macro body executes in an
 * ISR-like context; the FromISR API family is designed for exactly that. */
TRAX_VAR_SET(VAR_TID_QUEUE_DEPTH,
             (uint32_t)uxQueueMessagesWaitingFromISR(s_work_q));

/* CORRECT (always safe) — evaluate the RTOS call first, pass a plain value. */
uint32_t depth = (uint32_t)uxQueueMessagesWaiting(s_work_q);
TRAX_VAR_SET(VAR_TID_QUEUE_DEPTH, depth);
```

## Affected macros

Every frame-emitting macro evaluates its value arguments inside
`TRAX_PORT_ENTER_CRITICAL_SECTION`:

- `TRAX_VAR_SET` / `TRAX_VAR_SET_FLOAT` / `TRAX_VAR_SET64` / `TRAX_VAR_SET_DOUBLE`
- `TRAX_LOG_*` (all argument-carrying log macros)
- `TRAX_FRAME_ARGS`, `TRAX_FRAME_ARGS_ATOMIC`, `TRAX_FRAME_RAW_ARGS`

## Why it corrupts the stream

TraxProbe's critical section is a raw interrupt-mask save/disable/restore
(PRIMASK on Cortex-M). FreeRTOS's `taskENTER_CRITICAL()` /
`taskEXIT_CRITICAL()` pair — used internally by *most* non-`FromISR` kernel
APIs, including innocent-looking queries like `uxQueueMessagesWaiting()` —
tracks only its **own** nesting depth (`uxCriticalNesting`). When that count
returns to zero, `taskEXIT_CRITICAL()` **unconditionally re-enables
interrupts**. It does not know TraxProbe had them disabled.

The result, in the middle of `TRAX_FRAME_ARGS_ATOMIC`:

1. The frame slot is allocated and timestamp + TID are written.
2. The user argument expression calls the RTOS API → `taskEXIT_CRITICAL()`
   → interrupts re-enabled **while the frame is half-written**.
3. Pending ISRs and context switches run. Other tasks/ISRs allocate and
   commit frames in the slots *after* the half-written one.
4. Milliseconds later the preempted task resumes and commits Word 0 with a
   now-stale-read transaction counter.

On the wire the frame appears **out of order**: its `trans_counter` is
higher than the frames that physically follow it in the ring. The frame
validator (and the host's gap detector) flags this as
`TRAX_STOP_REASON_FRAME_CORRUPT` even though no memory was actually
smashed. Its timestamp may also carry a stale tick byte, because the tick
ISR was pended while the mask was held.

This is not a FreeRTOS bug: the FreeRTOS API contract forbids calling
non-`FromISR` APIs while interrupts are disabled by other means. The
unconditional re-enable is a documented performance trade-off.

## Port-dependent visibility — why testing may not catch it

| Core | FreeRTOS critical section | Effect on TraxProbe's PRIMASK section |
|------|---------------------------|----------------------------------------|
| Cortex-M0/M0+/M23 | PRIMASK | **Broken open** — corruption as described |
| Cortex-M3/M4/M7/M33 | BASEPRI | PRIMASK untouched — *accidentally* safe |

Identical application code can run clean on an M4/M7 board and corrupt the
stream on an M0+ board. Do not rely on the BASEPRI ports' accidental
immunity — future kernel versions, other RTOSes, or other ports may differ.

## Rule of thumb

Frame macro arguments must behave like **ISR-safe, side-effect-free
expressions**: variables, arithmetic, casts, struct/array reads — and, when
you need a kernel value, the **query-style `FromISR` API variants**.

### Why `FromISR` variants are safe here

TraxProbe already manages the interrupt mask itself inside the frame
macros, so the argument expression runs in an ISR-like context — precisely
the environment the `FromISR` family is specified for:

- Simple queries (`uxQueueMessagesWaitingFromISR`, `xTaskGetTickCountFromISR`,
  `uxQueueSpacesAvailable`-style reads) take **no critical section at all** —
  they never touch the interrupt mask.
- `FromISR` functions that do need protection internally use
  `portSET_INTERRUPT_MASK_FROM_ISR()` / `portCLEAR_INTERRUPT_MASK_FROM_ISR()`,
  the **save/restore** pattern that composes correctly with TraxProbe's
  already-set mask (unlike `taskEXIT_CRITICAL()`'s unconditional enable).
- The reads are consistent: interrupts are masked for the whole macro body,
  so nothing can mutate the kernel object mid-read.

Restrict this to **queries**. Action APIs (`xQueueSendFromISR`,
`xSemaphoreGiveFromISR`, …) do real kernel work and carry a
`pxHigherPriorityTaskWoken` yield protocol — they do not belong inside a
trace macro argument. When in doubt, fall back to the local-variable
pattern, which is safe with zero API knowledge.

## Case study

Observed on an STM32G0 (Cortex-M0+) running the Workshop 11 pipeline demo:
a `TRAX_VAR_SET(VAR_TID_QUEUE_DEPTH, uxQueueMessagesWaiting(q))` frame was
allocated after trans 25511, then preempted for ~3 ms (one SysTick frame,
context switches, a user ISR — trans 25512–25522 landed behind it in the
ring) and finally committed as trans 25523. The validator stopped the
stream with `FRAME_CORRUPT`, expected trans 25512, actual 25523.
