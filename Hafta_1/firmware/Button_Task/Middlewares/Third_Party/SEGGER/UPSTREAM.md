# SEGGER sources

- SystemView: copied from this repository's `SystemViewer_SourceCode`, whose
  pack metadata identifies release **4.12.0**. Only the core, common headers,
  FreeRTOS V10 adapter and UART recorder state machine are compiled.
- RTT: [SEGGERMicro/RTT](https://github.com/SEGGERMicro/RTT), commit
  `4d8feab3150f86f37a9d323ddc88d6cdf5673072`. Only the C implementation is used
  (`RTT_USE_ASM=0`).
- Original license files are retained in `SystemView/LICENSE.md` and
  `RTT/LICENSE.md`; original per-file notices are retained.

Local recorder changes: `HelloResponse` advertises the SystemView compatibility
version instead of the sample's state-enum values. The version encoding matches
SEGGER's [STM32F407 UART example](https://kb.segger.com/Use_SystemView_UART_Recorder).
The outgoing function's return value is initialized to satisfy optimized GCC
builds. Transport for STM32 HAL and configuration live in `Core`, outside the
vendor implementation. The source folder at the repository root is unchanged.

FreeRTOS 10.2.0 integration is based on the supplied
`Sample/FreeRTOSV10/Patch/FreeRTOSV10_Core.patch`, applied to the existing ST kernel:
ready/delayed/suspended events, ready-list reordering,
and SysTick tracing adapted to `ARM_CM7/r0p1`. V10.2's mutex timeout priority
disinherit path also uses the ready-list reorder hook. Peripheral ISR exits
are recorded once at the handler boundary; `portmacro.h` is unchanged to avoid
duplicate exits from CMSIS-RTOS2 calls.

Task-state additions: indefinite event waits use stop cause 4 (the stock
FreeRTOS description calls it `Delayed`), while explicit `vTaskSuspend` uses
27 (`Suspended`). The V10 adapter retains each task's stop cause even when
recording is off. `SEGGER_SYSVIEW_Init_Ex` registers `SYSVIEW_SendTaskStates`
as the Start callback, so already-blocked/suspended tasks are visible from the
start of a recording. The snapshot does not traverse kernel lists or call
task-only state APIs from the USART6 ISR. Metadata-only task-list requests do
not emit state transitions. Extra state storage is 32 bytes for eight tasks.

After CubeMX replaces FreeRTOS sources, run `tests/systemview_states/run.ps1`
from the repository's `Hafta_1` directory to detect missing task-state hooks.

Current recording selection: task lifecycle/states, Idle, data queues and the
button EXTI interrupt only. Ordinary RTOS API, notification and stream-buffer
trace macros are disabled. Queue hooks check `uxItemSize` so semaphores/mutexes
do not appear as data queues. Task deletion uses the native Task Terminate event;
priority metadata still updates without logging the priority API call. SysTick
trace macros are empty; its scheduling and task-ready hooks remain functional.
Only the button handler has explicit ISR entry/exit records in Core.
