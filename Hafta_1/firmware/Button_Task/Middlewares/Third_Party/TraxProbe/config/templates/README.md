# TraxProbe `trax_config.h` templates

TraxProbe has **one** user-editable file: `trax_config.h`. You create it in your
application include path; the library finds it by name. These templates give you
a correct starting point so you don't have to discover the required macros from
compiler errors.

## Quick start

1. Pick the template that matches your setup (table below).
2. Copy it into your application include path and rename it to exactly
   `trax_config.h`. In an STM32CubeIDE / CubeMX project that is
   `Core/Inc/trax_config.h` (already on the include path).
3. Edit the two clock values (`TRAX_CFG_TIMER_FREQ_HZ` and
   `TRAX_CFG_TICK_COUNTER_PERIOD`) to match your MCU clock. Everything else has
   a working default.
4. Build. Add the matching transport source files to your project (see each
   template's header comment).

## Templates

| File | RTOS | Transport | Timestamp | Metadata | Use case |
|------|------|-----------|-----------|----------|----------|
| `trax_config.minimal.h` | none | RTT (default) | SysTick | in-flash (default) | **Smallest valid config.** Sets ONLY the required macros and overrides nothing — you inherit every library default. The best starting point. |
| `trax_config.cortexm_baremetal_rtt.h` | none | SEGGER RTT | SysTick | ELF-only | Bare-metal MCU debugged over a J-Link / ST-Link. |
| `trax_config.cortexm_freertos_rtt.h` | FreeRTOS | SEGGER RTT | SysTick | ELF-only | FreeRTOS app with task/ISR tracing; library runs its own drain task. |
| `trax_config.cortexm_uart_custom.h` | none | custom (UART/USB/TCP) | SysTick | in-flash (on wire) | Production / no-debugger deployment; metadata is sent on the wire so the host needs no `.elf`. |
| `trax_config.zynq_a9_freertos.h` | FreeRTOS | custom | global timer (FREERUN) | ELF-only | **Zynq-7000 (Cortex-A9)** in Vitis. Includes the interrupt-nesting wiring that Cortex-A/R requires — without it the trace timebase never advances. |
| `trax_config.zynq_r5_baremetal.h` | none | custom | PMCCNTR (FREERUN) | ELF-only | **ZynqMP RPU (Cortex-R5F)**, standalone BSP. Includes the one-time PMCCNTR enable snippet and the `trax_timestamp_poll()` requirement. |
| `trax_config.microblaze_baremetal.h` | none | custom | AXI Timer (FREERUN) | ELF-only | **MicroBlaze** soft core in the PL. Includes the AXI Timer free-run setup and the `-mlittle-endian` requirement. |
| `trax_config.template.h` | — | — | — | — | Canonical, fully documented walk-through. Every knob explained with `TODO` markers. |
| `trax_config.reference.h` | — | — | — | — | **Menu of every option.** All macros listed (commented out) with their defaults and allowed values. Look here to find a knob, or copy lines from it. |

> `minimal` vs `reference`: `minimal` is what you ship (only what's required);
> `reference` is the full catalogue you consult to override a default. The
> `cortexm_*` and `zynq_*` files are convenience presets between the two.

> **MicroBlaze**: build with `-mlittle-endian` (mb-gcc still defaults to big
> endian, which TraxProbe's wire format does not support) and point
> `TRAX_CFG_MB_TIMER_BASE` at an AXI Timer — the core has no architectural
> counter, so there is nothing to fall back on. Both are build errors if
> missed. See `hw_port/MicroBlaze/README.md`.

> **Cortex-A/R targets**: the `zynq_*` presets are not just a different clock
> value. Cortex-A/R has no IPSR and its FreeRTOS ports have no `traceISR_ENTER()`
> hook, so ISR-context detection and wrap tracking have to be wired explicitly.
> Start from those presets rather than adapting a `cortexm_*` one, and read
> `hw_port/Zynq/README.md` first.

## Required vs optional

Only a few macros are **required**; the library emits a clear `#error` (pointing
back here) until they are set:

- `TRAX_CFG_HW_PORT` — e.g. `TRAX_HW_PORT_ARM_CORTEX_M`, `TRAX_HW_PORT_ZYNQ`
- `TRAX_CFG_TIMER_FREQ_HZ` — frequency of the timestamp timer (your core clock)
- In the default `TRAX_TIMESTAMP_TICK_TIMER` mode also:
  `TRAX_CFG_TIMESTAMP_TIMER_VAL` and `TRAX_CFG_TICK_COUNTER_PERIOD`

Everything else (transport, RTOS, buffer sizes, metadata mode, log level, string
lengths) already has a sensible default inside the library. Override only what
you need.

## Working out the clock values (TICK_TIMER mode)

For a SysTick-based timestamp:

- `TRAX_CFG_TIMER_FREQ_HZ` = your core clock in Hz (e.g. `64000000U` for 64 MHz).
- `TRAX_CFG_TICK_COUNTER_PERIOD` = `SysTick->LOAD + 1` = core cycles per tick
  (e.g. `64000U` for a 1 ms tick at 64 MHz).
- `TRAX_CFG_TIMESTAMP_TIMER_DIR` = `TRAX_TIMER_DIR_DOWN` (SysTick counts down).
- Call `trax_timestamp_tick()` from your `SysTick_Handler()` (or RTOS tick hook).

> Tip: for Cortex-M3/M4/M7/M33 you can instead use the free-running cycle
> counter (`TRAX_CFG_TIMESTAMP_MODE = TRAX_TIMESTAMP_FREERUN`, DWT->CYCCNT) for
> higher resolution — see the comments in `trax_config.template.h` and
> `config/trax_config_hw_port.h`.

## Reference

The named constants used in these files
(`TRAX_HW_PORT_*`, `TRAX_TRANSPORT_*`, `TRAX_RTOS_*`, `TRAX_TIMESTAMP_*`,
`TRAX_TIMER_DIR_*`, `TRAX_META_*`, `TRAX_LOG_LEVEL_*`) and every overridable
default live in the modular config headers one level up:

- `config/trax_config_options.h` — all the named option constants
- `config/trax_config_hw_port.h` — timestamp / timer / sync
- `config/trax_config_buffer.h` — buffer & frame sizes
- `config/trax_config_rtos.h` — RTOS type & drain-task budget
- `config/trax_config_meta.h` — metadata storage & string lengths
- `config/trax_config_log.h` — log levels & filtering
