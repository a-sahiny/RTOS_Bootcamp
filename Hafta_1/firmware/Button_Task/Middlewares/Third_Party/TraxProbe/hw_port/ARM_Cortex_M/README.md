# ARM Cortex-M Platform Port

Hardware abstraction layer for ARM Cortex-M series microcontrollers.

## Supported MCUs

- **STMicroelectronics**: STM32 (all series)
- **NXP**: LPC, Kinetis, i.MX RT
- **Nordic Semiconductor**: nRF series
- **Silicon Labs**: EFM32, EFR32
- **Texas Instruments**: Tiva C, MSP432
- **Other ARM Cortex-M**: Any MCU with ARM Cortex-M core

## Features

- **Critical Section Macros**: Uses ARM PRIMASK register for interrupt control
- **Free-Running Counter**: DWT->CYCCNT via `TRAX_HW_PORT_FREERUN_COUNTER`
- **Timestamp Support**: FREERUN (DWT) or TICK_TIMER (SysTick / HW timer)
- **Memory Barriers**: Uses ARM DMB (Data Memory Barrier) for synchronization
- **Block-Style Macros**: `TRAX_ENTER_CRITICAL() { ... } TRAX_EXIT_CRITICAL()`

## Core compatibility

This one port covers **every Cortex-M profile**. The critical section
(PRIMASK), the `__DMB()` barrier, and SysTick exist on all of them, so
**TICK_TIMER mode works everywhere**. Only **FREERUN mode** is core-gated,
because it reads the DWT cycle counter, which the Baseline profiles do not
implement:

| Core(s) | Architecture | DWT->CYCCNT | FREERUN | TICK_TIMER (SysTick) |
|---------|--------------|:-----------:|:-------:|:--------------------:|
| M0, M0+, M1 | ARMv6-M | no | — | yes |
| M23 | ARMv8-M Baseline | no | — | yes |
| M3 | ARMv7-M | yes | yes | yes |
| M4, M7 | ARMv7E-M | yes | yes | yes |
| M33, M35P | ARMv8-M Mainline | yes¹ | yes | yes |
| M55, M85 | ARMv8.1-M Mainline | yes¹ | yes | yes |

¹ DWT is optional on Mainline ARMv8-M; if it is absent (or secure-only on a
TrustZone part), CMSIS reports `__DWT_PRESENT == 0` and the port disables
FREERUN automatically.

The port detects DWT availability at compile time (CMSIS `__DWT_PRESENT`, then
the compiler `__ARM_ARCH_*` macros, then `__CORTEX_M`). Selecting FREERUN on a
core without DWT produces a clear `#error`. You can override detection by
defining `TRAX_PORT_HAS_DWT_CYCCNT` (0 or 1) before the port is included.

## Required Configuration

### Option A: FREERUN mode (Mainline cores with DWT: M3/M4/M7/M33/M55/M85)

DWT setup (in your main.c, before first TRAX log):
```c
CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
DWT->CYCCNT = 0;
DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
```

Configuration (in `App/Config/trax_config.h`):
```c
#define TRAX_CFG_TIMESTAMP_MODE   TRAX_TIMESTAMP_FREERUN
#define TRAX_CFG_TICK_BITS        8
#define TRAX_CFG_TIMER_FREQ_HZ    168000000U    // CPU clock
```

### Option B: TICK_TIMER mode (any Cortex-M — required on M0/M0+/M23 without DWT)

Configuration (in `App/Config/trax_config.h`):
```c
#define TRAX_CFG_TIMESTAMP_MODE       TRAX_TIMESTAMP_TICK_TIMER
#define TRAX_CFG_TICK_BITS            8
#define TRAX_CFG_TIMESTAMP_TIMER_VAL  (SysTick->VAL)
#define TRAX_CFG_TIMESTAMP_TIMER_DIR  TRAX_TIMER_DIR_DOWN
#define TRAX_CFG_TICK_COUNTER_PERIOD  48000U
#define TRAX_CFG_TIMER_FREQ_HZ        48000000U
```

> Note: the host (`MessageDecoder::calculateDeviceTime`) detects the
> timer-fired-before-ISR-ran race per-core and snaps to
> timestamp-wrap frames, so the MCU no longer needs a `_CHECK_IRQ_PENDING()` macro.

## Usage

1. Add `hw_port/ARM_Cortex_M/include/` to your build system's include paths
2. Configure timestamp mode in `App/Config/trax_config.h`
3. The platform is automatically used when this include path is added

## Implementation Details

### Critical Sections

Uses ARM Cortex-M PRIMASK register:
- `__get_PRIMASK()`: Read current interrupt state
- `__disable_irq()`: Disable interrupts
- `__set_PRIMASK()`: Restore interrupt state

### Port-Provided Macros

| Macro | Value | Description |
|-------|-------|-------------|
| `TRAX_HW_PORT_FREERUN_COUNTER` | `(DWT->CYCCNT)` | 32-bit CPU cycle counter (defined only when `TRAX_PORT_HAS_DWT_CYCCNT`) |
| `TRAX_HW_PORT_TIMEPACKED_GET32()` | Mode-dependent | Read timestamp |
| `TRAX_HW_PORT_TIMEPACKED_PUT32(p_wr)` | Mode-dependent | Write timestamp to buffer |

### Memory Barriers

Uses ARM `__DMB()` (Data Memory Barrier) to ensure:
- Timer value reads are not reordered
- Interrupt pending checks happen after timer reads
- Prevents race conditions during timer wrap
