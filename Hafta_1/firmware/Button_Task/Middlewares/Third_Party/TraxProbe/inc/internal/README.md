# TraxProbe — Internal Headers

Headers in this folder are **private to the TraxProbe library implementation**.
They are an implementation detail and are **not** part of the public API.

## Rules

- **Do not include these from application code.** Application code includes only
  the public headers in `inc/` (typically just `trax.h`, plus `trax_transport.h`,
  `trax_sync.h`, and `trax_tid.h`).
- **No public header may include anything from `internal/`.** The public headers
  in `inc/` must stay self-contained so integrators only need the single `inc/`
  include path. If a header here ever needs to be referenced by a public header,
  it is no longer internal and should move up to `inc/`.
- Library `.c` files reference these as `#include "internal/trax_xxx.h"`. This
  resolves through the existing `inc/` include path — no extra include path is
  added to the build.

## Current contents

| Header                 | Purpose                                          |
| ---------------------- | ------------------------------------------------ |
| `trax_cmd_handler.h`   | Command dispatch internals (host command frames) |
| `trax_timestamp_wrap.h` | Timestamp wrap resync frame generation          |
| `trax_memory.h`        | Heap/stack monitoring port interface             |
| `trax_task.h`          | RTOS task enumeration internals                  |

## How "public vs internal" was decided

A header is **public** if it is reachable (transitively) from a public root
(`trax.h`, `trax_transport.h`, `trax_sync.h`). Because the log/var/marker/ISR
macros are header-only and expand frame-building inline, most headers
(`trax_frame.h`, `trax_session.h`, `trax_timestamp.h`, etc.) are pulled into the
public surface and must remain in `inc/`. Only headers referenced **exclusively**
by `.c` files belong here.
