# TraxProbe FreeRTOS Port

FreeRTOS-specific integration for TraxProbe. Maps FreeRTOS kernel trace hooks to TraxProbe frames via the common RTOS table API.

## Supported FreeRTOS Versions

**FreeRTOS V10.2.0 through V11.2.x** (plus the ESP-IDF fork via `sdk/esp/`).

The kernel version **must be declared** in `App/Config/trax_config.h` — it cannot
be auto-detected because the port header is parsed while `FreeRTOSConfig.h` is
still being processed, before `task.h` defines `tskKERNEL_VERSION_MAJOR`
(Percepio's TraceRecorder has the same constraint and uses the same solution,
`TRC_CFG_FREERTOS_VERSION`):

```c
#define TRAX_CFG_FREERTOS_VERSION   TRAX_FREERTOS_VERSION(11, 2, 0)
```

Use the version from `tskKERNEL_VERSION_NUMBER` in your kernel's
`include/task.h`. The build fails with a clear `#error` when the value is
missing or below V10.2.0.

Version differences handled by the port:

| Kernel | Difference | Handling |
|--------|-----------|----------|
| < 10.4.0 | Notify hooks have no index argument (no notification arrays) | Version-gated hook variants; index reported as 0 |
| < 10.4.0 | No tick-ISR trace hooks in port.c (`traceISR_ENTER` family) | Runtime-guarded `traceTASK_INCREMENT_TICK` advances the **timestamp** and opens a **synthetic SysTick ISR** frame; TraxProbe's `vApplicationTickHook` closes it via `trax_freertos_on_tick()` (near-zero ENTER+EXIT if `TRAX_CFG_OWN_FREERTOS_TICK_HOOK=0` and the kernel tick hook is off). Native ≥10.4 path is skipped automatically when `traceISR_ENTER` already ran |
| < 10.4.0 | No `traceQUEUE_SET_SEND`; queue-set notify fires `traceQUEUE_SEND` on the container, possibly from ISR | Hook gated to >= 10.4.0; queue hooks read `uxMessagesWaiting` directly (never `uxQueueMessagesWaiting()`) so the ISR-context path is safe |
| < 10.3.0 | No `vPortGetHeapStats()` | `TRAX_CFG_HEAP_HAS_STATS` defaults from the declared version |
| >= 11.0.0 | SMP kernel (`configNUMBER_OF_CORES > 1`) keeps `pxCurrentTCB` private to tasks.c | Port maps `pxCurrentTCB` to `xTaskGetCurrentTaskHandle()` for the other kernel TUs |
| >= 11.1.0 | Stream **batching** buffers (third `sbTYPE_*`), `xStreamBufferResetFromISR`, `configUSE_EVENT_GROUPS` / `configUSE_STREAM_BUFFERS` opt-outs | Type byte maps arithmetically; feature guards use the permissive "undefined counts as enabled" form so options defaulted by `FreeRTOS.h` *after* the trax include still trace correctly |
| < 10.2.0 | Different `Queue_t` / `Timer_t` private layouts | **Not supported** — build fails with `#error` |

### SysTick ISR swimlane matrix

| Kernel / tree | `port.c` has `traceISR_*`? | How Traxcope gets SysTick slices |
|---------------|----------------------------|----------------------------------|
| FreeRTOS **10.2–10.3.x** (e.g. STM32CubeMX) | No | Synthetic path. TraxProbe owns `vApplicationTickHook` (`TRAX_CFG_OWN_FREERTOS_TICK_HOOK=1`) and calls `trax_freertos_on_tick()`. That close path honours `TRAX_CFG_ISR_YIELD_TO_SCHEDULER` (omit EXIT when this tick readied a preempting task) |
| FreeRTOS **≥ 10.4** / **V11** | Yes (most ARM ports) | Native `traceISR_ENTER` / `EXIT` from `port.c`. `traceISR_EXIT_TO_SCHEDULER` omits `ISR_EXIT` by default (`TRAX_CFG_ISR_YIELD_TO_SCHEDULER=1`) so Traxcope draws SysTick → Scheduler → new task without the 2–5 µs Cortex-M return to the preempted task; set the config to 0 for that literal resume |
| ESP-IDF FreeRTOS | IDF tick hooks | Existing `sdk/esp` adapter — synthetic path stays inactive when native ENTER sets `trax_in_tick_isr` |

CubeMX: leave **USE_TICK_HOOK unchecked** so codegen does not emit `vApplicationTickHook` in `app_freertos.c`. TraxProbe forces `configUSE_TICK_HOOK=1` and provides the function. Delete any already-generated stub (otherwise: multiple definition). Application tick work goes in `trax_app_tick_hook()`. Do **not** patch Middleware `port.c`.

SysTick metadata priority (`trax_freertos_meta.c`) is the lowest implemented NVIC priority (`__NVIC_PRIO_BITS`), not a hard-coded 15.

## Files

| File | Purpose |
|------|---------|
| `trax_rtos_port.h` | FreeRTOS trace hook macros (pulled in by `trax.h`) |
| `trax_freertos_task.c` | Ctrl Task implementation using `xTaskCreateStatic` + `vTaskDelayUntil` |
| `trax_freertos_heap.c` | Heap query API (`xPortGetFreeHeapSize`, `configTOTAL_HEAP_SIZE`) |
| `trax_freertos_memory.c` | Stack high-water polling via `uxTaskGetStackHighWaterMark` (no-op when `INCLUDE_uxTaskGetStackHighWaterMark = 0`) |
| `trax_freertos_meta.c` | `TRAX_ISR_DEFINE` for SysTick — kernel owns the ISR, so the port owns its metadata |
| `trax_freertos_tick.c` | `trax_freertos_on_tick()` + TraxProbe-owned `vApplicationTickHook` — closes pre-10.4 synthetic SysTick ISR |

## Integration Steps

### 1. Configuration (`trax_config.h`)

```c
#define TRAX_CFG_RTOS_TYPE             TRAX_RTOS_FREERTOS
#define TRAX_CFG_FREERTOS_VERSION      TRAX_FREERTOS_VERSION(11, 2, 0)  // REQUIRED — your kernel version
#define TRAX_CFG_RTOS_TASK_NAME_MAX    configMAX_TASK_NAME_LEN  // optional
```

### 2. FreeRTOS Configuration (`FreeRTOSConfig.h`)

Include `trax.h` at the **end** of `FreeRTOSConfig.h`, after all `config*`
macros are defined — several trace hooks select their shape from
`configUSE_TRACE_FACILITY`, `configRECORD_STACK_HIGH_ADDRESS` and
`configNUMBER_OF_CORES` at parse time:

```c
#define configUSE_TRACE_FACILITY    1   // REQUIRED (build fails loudly if missing)

#include "trax.h"   /* pulls in the RTOS port dispatcher automatically */
```

### 3. Include Paths

Add only this directory to your compiler's include path:

- `TraxProbe/include/`

`trax_rtos_port.h` in `include/` is a dispatcher that automatically includes the FreeRTOS port implementation based on `TRAX_CFG_RTOS_TYPE`. No RTOS-specific subdirectory needs to be on the include path.

### 4. Source Files

Add to your build (or just recurse over `Third_Party/TraxProbe/`; every
file is wrapped in a `TRAX_CFG_RTOS_TYPE` guard so inactive ports
collapse to nothing):

- `TraxProbe/os/common/trax_rtos_tables.c`
- `TraxProbe/os/FreeRTOS/trax_freertos_task.c`
- `TraxProbe/os/FreeRTOS/trax_freertos_heap.c`
- `TraxProbe/os/FreeRTOS/trax_freertos_memory.c`
- `TraxProbe/os/FreeRTOS/trax_freertos_meta.c`

## Traced Events

### Context Switches (critical path, ~1.5 us @ 64 MHz)
- `traceTASK_SWITCHED_IN` / `traceTASK_SWITCHED_OUT`

### Task Lifecycle
- `traceTASK_CREATE` — allocates table slot, stores index in `uxTCBNumber`
- `traceTASK_DELETE` — frees table slot
- `traceTASK_PRIORITY_SET` — updates priority in table

### Task State
- `traceMOVED_TASK_TO_READY_STATE`, `traceTASK_SUSPEND`, `traceTASK_RESUME`
- `traceTASK_DELAY`, `traceTASK_DELAY_UNTIL`

### ISR (SysTick)
- `traceISR_ENTER` — increments tick counter, records ISR entry (≥10.4 `port.c`)
- `traceISR_EXIT` — records ISR exit (no switch pending)
- `traceISR_EXIT_TO_SCHEDULER` — tick pended a switch; default omits the EXIT frame (`TRAX_CFG_ISR_YIELD_TO_SCHEDULER`)
- Pre-10.4 / no-port-hook: synthetic ENTER from `traceTASK_INCREMENT_TICK`,
  EXIT via `trax_freertos_on_tick()` (TraxProbe `vApplicationTickHook`; override `trax_app_tick_hook()` for app tick work)
- Lane name + priority sent to Traxcope by `trax_freertos_meta.c`
  (priority = lowest implemented NVIC priority from `__NVIC_PRIO_BITS`).
  `TRAX_TID_ISR_TICK` itself is reserved in `trax_tid.h` (library-defined),
  so there is **nothing for the user to declare** in
  `App/Config/trax_config.h` — the SysTick lane is entirely port-managed.
  User ISR enums start at `TRAX_TID_RANGE_ISR_USER_START` (0x4010).

### Queue / Semaphore / Mutex
- `traceQUEUE_SEND` / `traceQUEUE_RECEIVE` (with fill level)
- `traceQUEUE_SEND_FAILED` / `traceQUEUE_RECEIVE_FAILED`
- `traceQUEUE_CREATE` / `traceQUEUE_DELETE`
- `traceQUEUE_REGISTRY_ADD` (object naming)

## Ctrl Task Configuration

| Macro | Default | Description |
|-------|---------|-------------|
| `TRAX_CFG_CTRL_TASK_PRIORITY` | 1 | Task priority (just above idle) |
| `TRAX_CFG_CTRL_TASK_STACK_SIZE` | 256 | Stack size in `StackType_t` words |
| `TRAX_CFG_CTRL_TASK_PERIOD_MS` | 10 | Wake period in milliseconds |
| `TRAX_CFG_ISR_YIELD_TO_SCHEDULER` | 1 | Omit `ISR_EXIT` when the tick (or `TRAX_ISR_END`) pends a switch, so Traxcope draws ISR → Scheduler without the 2–5 µs Cortex-M resume of the preempted task. Set to 0 for that literal resume |
| `TRAX_CFG_OWN_FREERTOS_TICK_HOOK` | 1 | TraxProbe provides `vApplicationTickHook` and forces `configUSE_TICK_HOOK=1`. CubeMX: uncheck USE_TICK_HOOK. App tick work: `trax_app_tick_hook()`. Set to 0 to keep your own `vApplicationTickHook` (then call `trax_freertos_on_tick()` yourself) |

## Naming Convention

Each RTOS port uses the filename `trax_rtos_port.h`, with the RTOS specificity coming from its folder (`os/FreeRTOS/`, `os/Zephyr/`, etc.). Add only the relevant port's folder to the include path; the `#include "trax_rtos_port.h"` directive then resolves to the correct implementation automatically.
