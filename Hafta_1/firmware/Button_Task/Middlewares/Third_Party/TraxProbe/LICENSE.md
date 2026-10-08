# TraxProbe Licensing

TraxProbe uses a **split license** so that the porting layer can be freely
extended while the core engine stays proprietary.

| Component | Location | License | SPDX tag |
|-----------|----------|---------|----------|
| Core engine | `inc/`, `src/`, `config/`, `debug/`, `os/common/` | TraxProbe Commercial | `LicenseRef-TraxProbe-Commercial` |
| Porting layer | `hw_port/`, `os/FreeRTOS/`, `os/BareMetal/`, reference transport backends (`transports/RTT/trax_transport_rtt.c`, `transports/RTT/config/trax_transport_config.h`) | Apache License 2.0 | `Apache-2.0` |
| SEGGER RTT | `transports/RTT/SEGGER_RTT.*`, `transports/RTT/config/SEGGER_RTT_Conf.h` | SEGGER (third-party) | retained as shipped |

License texts:

- Core: [`LICENSE-COMMERCIAL.txt`](LICENSE-COMMERCIAL.txt)
- Porting layer: [`LICENSE-Apache-2.0.txt`](LICENSE-Apache-2.0.txt)
- Attribution: [`NOTICE`](NOTICE)

## What you may do

- **Extend hardware support:** copy a directory under `hw_port/<Platform>/`,
  modify it for your target, and ship it. Covered by Apache-2.0.
- **Extend RTOS/OS support:** copy a directory under `os/<RTOS>/`, modify it,
  and ship it. Covered by Apache-2.0.
- **Add transport backends:** use the reference backend(s) as a template.

## What you may not do

- Redistribute, modify, or reverse engineer any file tagged
  `LicenseRef-TraxProbe-Commercial` (the core engine) except as permitted by
  your commercial agreement.

## How licensing is marked

Every source file carries an `SPDX-License-Identifier` line in its header. That
tag is authoritative for the individual file. The Apache-2.0 grant on a porting
file applies only to that file's source; the core headers it includes remain
under the Commercial License.

> This documentation is not legal advice. The commercial license text is a
> template to be finalized by Embedya's counsel.
