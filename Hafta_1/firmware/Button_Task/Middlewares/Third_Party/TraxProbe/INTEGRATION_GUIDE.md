# TraxProbe Integration Guide

This guide walks you through adding TraxProbe to your firmware. Three
target environments are covered:

- [FreeRTOS](#freertos) (Cortex-M with FreeRTOS kernel)
- [Bare-metal](#bare-metal) (Cortex-M without an RTOS)
- [Linux](#linux) (POSIX user-space process)

---

## Common steps (all targets)

### 1. Add sources to your build

Compile every `.c` file under `src/` and the `.c` file matching your
selected transport. The exact set depends on your build system — for
GCC + Makefile this typically means:

```makefile
TRAXPROBE_DIR    := Third_Party/TraxProbe
TRAXPROBE_SRC    := $(wildcard $(TRAXPROBE_DIR)/src/*.c) \
                    $(TRAXPROBE_DIR)/transports/trax_transport.c \
                    $(TRAXPROBE_DIR)/transports/RTT/trax_transport_rtt.c

TRAXPROBE_INC    := -I$(TRAXPROBE_DIR)/inc \
                    -I$(TRAXPROBE_DIR)/config \
                    -I$(TRAXPROBE_DIR)/transports/RTT/include \
                    -I$(TRAXPROBE_DIR)/transports/RTT/config
```

For STM32CubeIDE / Eclipse CDT projects: right-click the project →
**Properties → C/C++ Build → Settings → Includes**, add the four
directories above. Then **Source Locations → Add Folder** for `src/`,
the OS port folder, the HW port folder, and the transport folder.

### 2. Configure

TraxProbe has **one** user-editable file: `trax_config.h`. You create it in
your project's include path; the library finds it by name.

The fastest start is to copy a ready-made template from `config/templates/`
and rename it to `trax_config.h`:

| Template | Setup |
|----------|-------|
| `trax_config.template.h` | Canonical, fully documented reference — every option explained. |
| `trax_config.cortexm_baremetal_rtt.h` | Bare-metal Cortex-M + SEGGER RTT. |
| `trax_config.cortexm_freertos_rtt.h` | FreeRTOS Cortex-M + SEGGER RTT. |
| `trax_config.cortexm_uart_custom.h` | Cortex-M + your own UART/USB/TCP transport. |

See `config/templates/README.md` for the full table and how to compute the
clock values. Only a few macros are **required** (`TRAX_CFG_HW_PORT`,
`TRAX_CFG_TIMER_FREQ_HZ`, and in the default tick-timer mode
`TRAX_CFG_TIMESTAMP_TIMER_VAL` + `TRAX_CFG_TICK_COUNTER_PERIOD`); the library
emits a clear `#error` pointing back to the templates until they are set.

> The other `config/trax_config_*.h` files (log, buffer, hw_port, rtos, meta,
> diag) are **library internals** — do not edit them. They provide
> production-safe defaults that you override from your own `trax_config.h`.

### 3. Set up the transport

Pick **one** transport. The SDK ships RTT and a UART helper:

| Transport | When to use | Setup |
|-----------|-------------|-------|
| RTT       | You have a SEGGER J-Link | No pin assignment needed |
| UART      | You have a free UART     | Configure baud + DMA |

The transport's `trax_transport_<name>_init()` is called from your
`main()` after clocks are up.

### 4. Wire up the main loop

After init, call `trax_main_loop_run()` (bare-metal) or pin the
TraxProbe task to a low-priority FreeRTOS thread. The SDK does the
rest — your application code only needs to call the macros.

---

## FreeRTOS

### Files to add

- All `src/*.c`
- `os/FreeRTOS/*.c`
- `transports/<your-transport>/*.c`
- `hw_port/ARM_Cortex_M/*.c` (or your target architecture)

### Linker fragment (required)

TraxProbe places its metadata (logs, vars, ISRs, markers, …) in dedicated
`.trax_*` sections and uses linker-defined boundary symbols
(`__trax_log_start/_end`, `__trax_isr_start/_end`, …) to walk them at runtime.
The SDK ships the section definitions in `linker/trax_meta.ld`. **If you skip
this step the link fails with `undefined reference to '__trax_log_start'`** (and
similar).

The fragment defines its own `MEMORY` region, so add it as an **additional
linker script** rather than `INCLUDE`-ing it:

- **STM32CubeIDE / Eclipse CDT:** Project → Properties → C/C++ Build →
  Settings → **MCU/MPU GCC Linker → General → Linker Script (-T)** — keep your
  device `.ld` and add a second entry:
  `"${workspace_loc:/${ProjName}/Third_Party/TraxProbe/linker/trax_meta.ld}"`
- **Makefile / command line:** add another `-T` flag:
  `-T STM32xxxx_FLASH.ld -T Third_Party/TraxProbe/linker/trax_meta.ld`

The same one line in `trax_config.h` then controls placement with no other
build changes:

```c
#define TRAX_CFG_META_STORAGE   TRAX_META_IN_FLASH   // tables in flash + on wire
#define TRAX_CFG_META_STORAGE   TRAX_META_ELF_ONLY   // tables only in the .elf
```

### Minimal main()

```c
#include "trax.h"

int main(void) {
    SystemClock_Config();
    HAL_Init();

    trax_init();           // includes RTOS port, transport, buffer
    xTaskCreate(trax_task, "trax", 512, NULL, tskIDLE_PRIORITY + 1, NULL);

    /* your tasks ... */
    vTaskStartScheduler();
}
```

### Tag a variable for streaming

```c
#include "trax_stream.h"

float motor_rpm;
TRAX_STREAM_REGISTER(motor_rpm, TRAX_TYPE_F32);

/* 1 kHz control loop */
void control_loop_isr(void) {
    motor_rpm = read_encoder();
    TRAX_STREAM_SAMPLE(motor_rpm);  // visible in Traxcope's scope
}
```

---

## Bare-metal

### Files to add

- All `src/*.c`
- `os/BareMetal/*.c`
- `transports/<your-transport>/*.c`
- `hw_port/ARM_Cortex_M/*.c`

### Linker fragment (required)

Same as FreeRTOS — add `linker/trax_meta.ld` as an additional `-T` linker
script. Skipping it produces `undefined reference to '__trax_log_start'` (and
similar) at link time.

### Minimal main()

```c
#include "trax.h"

int main(void) {
    SystemClock_Config();
    trax_init();

    while (1) {
        trax_main_loop_run();   // call from idle path
        application_tick();
    }
}
```

The bare-metal port assumes a non-blocking idle loop. If you use WFI,
have a SysTick interrupt nudge the loop to drain the buffer.

---

## Linux

### Files to add

- All `src/*.c`
- `os/Linux/*.c`
- `os/common/*.c`
- `hw_port/Linux/*.c`
- `transports/<your-transport>/*.c`

### Build with CMake

```cmake
add_subdirectory(Third_Party/TraxProbe/cmake) # provided helper
target_link_libraries(my_app PRIVATE traxprobe)
```

Or compile manually with:

```bash
gcc -ITraxProbe/inc -ITraxProbe/config -ITraxProbe/hw_port/Linux/include \
    TraxProbe/src/*.c TraxProbe/os/Linux/*.c TraxProbe/os/common/*.c \
    TraxProbe/hw_port/Linux/*.c TraxProbe/transports/RTT/*.c \
    my_app.c -o my_app -lpthread
```

### Minimal main()

```c
#include "trax.h"
#include <unistd.h>

int main(void) {
    trax_init();
    pthread_t t;
    pthread_create(&t, NULL, (void *(*)(void *))trax_task, NULL);

    while (1) {
        application_tick();
        usleep(1000);
    }
}
```

---

## Verifying

1. Build, flash (or run on Linux), open Traxcope.
2. **File → New Probe → Add → <your transport>**.
3. Click **Connect**. Within ~1s the **Probe Tree** populates with
   variables, logs, and markers your firmware registered.

If you see a "Wire format mismatch" banner in Traxcope, update either
the SDK or the desktop app so both share the same MAJOR version (see
`inc/trax_version.h`).

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| Probe never appears | Transport mis-wired or muted | Check baud / RTT control block |
| Variables empty     | `TRAX_STREAM_REGISTER` was not linked | Confirm linker section is `KEEP`'d |
| Frequent overruns   | Buffer too small | Increase `TRAX_BUFFER_SIZE` in `trax_config_buffer.h` |
| "Build version differs" warning | Recompile mismatch host vs. firmware | Both sides must use the same SDK release |

For deeper issues, capture `Help → Diagnostics → Save Bundle...` in
Traxcope and email it to info@embedya.com.
