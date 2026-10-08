# TraxProbe Firmware SDK

The **TraxProbe SDK** is the firmware-side library that pairs with the
[Traxcope](https://embedya.com/traxcope) desktop application. Linking it into
your MCU project (or Linux process) gives Traxcope live access to:

- Variable streams and oscilloscope traces
- Structured logs with severity and tagging
- Markers, timestamps, and ISR profiling
- Heap, stack and memory metrics
- State-machine transitions

## What's in this folder

```
TraxProbe/
├── inc/             Public headers — include from your firmware
├── src/             Core implementation (compile these)
├── config/          User-tunable defaults (copy + edit per project)
├── os/              FreeRTOS, BareMetal, Linux ports
├── hw_port/         ARM Cortex-M, Generic, Linux hardware glue
├── transports/      RTT, UART transport implementations
├── linker/          Linker fragments for trax_meta sections
├── debug/           Optional frame validator for development
├── docs/            Upstream technical references
├── INTEGRATION_GUIDE.md    Step-by-step setup
├── LICENSE.txt             License overview (see LICENSE.md, LICENSE-*.txt)
└── README.md               This file
```

## Quick start

1. Copy this entire `TraxProbe/` folder into your firmware project
   (e.g. as a `Third_Party/` subdirectory).
2. Open `INTEGRATION_GUIDE.md` and follow the section that matches your
   target (FreeRTOS, BareMetal, or Linux).
3. Build, flash, run. Open Traxcope and connect to your probe — your
   variables, logs, and markers will appear automatically.

## Compatibility

This SDK ships with the bundled Traxcope installer and is guaranteed
compatible with the same release. Mixing different MAJOR versions of
TraxProbe and Traxcope will be reported as a wire-format mismatch in
the desktop UI — update both sides together.

The current SDK protocol version is recorded in `inc/trax_version.h`.

## Support

- Documentation: https://embedya.com/docs/traxcope
- Issues:        info@embedya.com
- Contact:       info@embedya.com
