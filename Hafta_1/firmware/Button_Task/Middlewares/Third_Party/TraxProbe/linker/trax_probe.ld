/*
 ******************************************************************************
 * @file        : trax_probe.ld
 * @brief       : TraxProbe linker fragment — single file for ALL features
 ******************************************************************************
 *
 * THE one TraxProbe linker integration point for GNU ld builds.  Add it
 * once in Eclipse (or any GNU ld build):
 *   Project Properties → C/C++ Build → Settings →
 *     GNU Arm Cross C Linker → General → Script files (-T)
 *   add an entry:
 *     "${workspace_loc:/${ProjName}/Third_Party/TraxProbe/linker/trax_probe.ld}"
 *
 * After that, every TraxProbe feature is controlled from
 * `App/Cfg/trax_config.h` alone — no further linker edits:
 *
 *   METADATA (always active):
 *     #define TRAX_CFG_META_STORAGE   TRAX_META_IN_FLASH   // metadata in flash
 *     #define TRAX_CFG_META_STORAGE   TRAX_META_ELF_ONLY   // metadata only in ELF
 *
 *   TRAXFAULT (crash capture — optional):
 *     #define TRAX_CFG_FAULT_ENABLE      1
 *     #define TRAX_CFG_FAULT_NOINIT_MAX  <budget bytes>    // REQUIRED when enabled
 *     The `.trax_noinit` output section below is auto-sized: with TraxFault
 *     disabled nothing is emitted into it, so it is empty and costs ZERO
 *     RAM.  With TraxFault enabled it grows to TRAX_FAULT_NOINIT_TOTAL
 *     (trax_config_fault.h), and the config layer refuses to build unless
 *     TRAX_CFG_FAULT_NOINIT_MAX covers that total — the RAM reservation is
 *     always explicit and checked at compile time.
 *
 * REQUIRED memory regions in the project's main linker script:
 *   FLASH — the metadata sections below are mapped with `> FLASH`.  If your
 *           board script names its flash region differently (e.g. `ROM`,
 *           `m_text`, `PROGRAM_FLASH`), add ONE line after its MEMORY{}:
 *               REGION_ALIAS("FLASH", ROM);
 *           (We avoid `INSERT AFTER .text;` because STM32CubeIDE /
 *           GNU-MCU-Eclipse pass the board script via `-T`, which replaces
 *           the default script that INSERT attaches to — ld then reports
 *           ".text not found for insert".)
 *   RAM   — `.trax_noinit` is mapped with `> RAM`.  Alias likewise if your
 *           run-mode RAM region has another name:
 *               REGION_ALIAS("RAM", SRAM1);
 *
 * ============================ METADATA ======================================
 *
 * HOW IT WORKS:
 *   The TraxProbe headers (`trax_meta_section.h`) emit each metadata entry
 *   into either `.trax_<kind>` (FLASH mode) or `.trax_<kind>_elf` (ELF-only
 *   mode) depending on TRAX_CFG_META_STORAGE.  This script defines BOTH
 *   placements unconditionally.  The unused output section ends up empty
 *   (or omitted entirely by the linker) and costs nothing.
 *
 *     FLASH mode     → bytes land in `.trax_<kind>` → loaded into FLASH
 *                      and visible in the ELF.  Boundary symbols
 *                      `__trax_<kind>_start/_end` let `trax_meta_tx.c`
 *                      walk the tables at runtime.
 *
 *     ELF-only mode  → bytes land in `.trax_<kind>_elf` → placed at a
 *                      synthetic VMA (0x90000000) outside any flash
 *                      region and marked `(INFO)`.  Section type stays
 *                      SHT_PROGBITS so the bytes live in the ELF (host
 *                      parser reads them from there), but the section
 *                      is NOT allocatable → zero flash cost.
 *
 *   The `(INFO)` attribute on the ELF-only sections is load-bearing.
 *   Mapping a section into a MEMORY region otherwise gives it SHF_ALLOC,
 *   with two user-visible consequences:
 *
 *     1. `arm-none-eabi-size` sums every ALLOC section, so it folds the
 *        whole metadata table into the `text` column.  A project with a
 *        few hundred log sites then appears ~50 KB larger than the image
 *        actually flashed.
 *     2. ld places ALLOC sections in a PT_LOAD program header, and
 *        `objcopy -O ihex` faithfully emits every PT_LOAD — so the .hex
 *        grows a spurious record block at 0x90000000, which programmers
 *        either reject or silently drop.
 *
 *   `(INFO)` clears SHF_ALLOC while keeping the contents in the file, so
 *   `size` ignores the table, the .hex covers only real flash, and the
 *   host still reads the records straight out of the .elf.
 *
 *   We intentionally do NOT use `(NOLOAD)` here.  NOLOAD makes the
 *   sections SHT_NOBITS, which carries no bytes in the ELF file — the
 *   host would then see empty sections.
 *
 * ============================ TRAXFAULT NOINIT ==============================
 *
 * The fault handler writes its crash record into `.trax_noinit`; the record
 * must SURVIVE the system reset that follows, so the section is:
 *
 *   1. NOLOAD           — nothing is programmed into flash for it, and
 *   2. outside .bss     — the startup zero-fill loop must not wipe it
 *                         (it is a separate output section, never part of
 *                         the _sbss.._ebss range).
 *
 * NOTES
 *   - SIZE: the section holds the fault record, the flight-recorder buffer
 *     and the dynamic-metadata snapshot; the exact total for your config is
 *     TRAX_FAULT_NOINIT_TOTAL (trax_config_fault.h).  The block below is
 *     auto-sized by the linker, so it grows with the config and is EMPTY
 *     (zero bytes) when TRAX_CFG_FAULT_ENABLE is 0.
 *   - BUDGET: when TraxFault is enabled the config layer REQUIRES
 *     TRAX_CFG_FAULT_NOINIT_MAX >= TRAX_FAULT_NOINIT_TOTAL, so the RAM
 *     spent here is always declared in trax_config.h and config growth is
 *     caught as a compile error, not silent RAM creep.
 *   - Placement: target RAM must be powered in your normal run mode and
 *     not cleared by a bootloader.  Multi-bank parts (STM32H7 etc.): any
 *     always-on SRAM/DTCM bank works — the record is only accessed at
 *     fault time and boot time, so access latency is irrelevant.
 *   - Alignment: the record is word-based; ALIGN(4) is sufficient.
 *   - If your project already provides a `.noinit` section, you may instead
 *     set `#define TRAX_CFG_FAULT_SECTION ".noinit"` in trax_config.h; the
 *     block below then simply stays empty.
 *
 ******************************************************************************
 */

/*
 * Synthetic memory region for ELF-only metadata.
 *   - 0x90000000 is outside flash on every supported MCU (STM32F/G/H/L,
 *     NXP M0/M4, Nordic nRF5, etc.).
 *   - LENGTH is generous enough for thousands of metadata records.  Bump
 *     it here if your project has more than ~16 k of metadata.
 *   - Defining the region unconditionally is fine even in flash mode —
 *     it is only consumed by the `_elf` output sections below, which are
 *     empty when the firmware is built in flash mode.
 */
MEMORY
{
  TRAX_META_ELF (r) : ORIGIN = 0x90000000, LENGTH = 1024K
}

SECTIONS
{
  /* ============================================================
   * FLASH-resident metadata output sections.
   * Populated only when TRAX_CFG_META_STORAGE == TRAX_META_IN_FLASH
   * (the C-side macro emits into the matching input sections).
   * Always defined; empty otherwise.
   *
   * Boundary symbols (`__trax_<kind>_start/_end`) are wrapped in
   * PROVIDE_HIDDEN so the linker only emits them when something
   * actually references them.  trax_meta_tx.c declares the externs
   * inside `#if (TRAX_CFG_META_STORAGE == TRAX_META_IN_FLASH)`, so:
   *
   *   - FLASH mode  → C side references the symbols → linker provides
   *                   them with their real flash addresses.
   *   - ELF-only    → no C reference exists → linker silently omits
   *                   the symbols, keeping the symbol table clean.
   * ============================================================ */
  .trax_log :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__trax_log_start = .);
    KEEP(*(.trax_log))
    PROVIDE_HIDDEN(__trax_log_end = .);
    . = ALIGN(4);
  } > FLASH

  .trax_log_vars :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__trax_log_vars_start = .);
    KEEP(*(.trax_log_vars))
    PROVIDE_HIDDEN(__trax_log_vars_end = .);
    . = ALIGN(4);
  } > FLASH

  .trax_var :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__trax_var_start = .);
    KEEP(*(.trax_var))
    PROVIDE_HIDDEN(__trax_var_end = .);
    . = ALIGN(4);
  } > FLASH

  .trax_stream_adc :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__trax_stream_adc_start = .);
    KEEP(*(.trax_stream_adc))
    PROVIDE_HIDDEN(__trax_stream_adc_end = .);
    . = ALIGN(4);
  } > FLASH

  .trax_stream_rotor :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__trax_stream_rotor_start = .);
    KEEP(*(.trax_stream_rotor))
    PROVIDE_HIDDEN(__trax_stream_rotor_end = .);
    . = ALIGN(4);
  } > FLASH

  .trax_isr :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__trax_isr_start = .);
    KEEP(*(.trax_isr))
    PROVIDE_HIDDEN(__trax_isr_end = .);
    . = ALIGN(4);
  } > FLASH

  .trax_marker :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__trax_marker_start = .);
    KEEP(*(.trax_marker))
    PROVIDE_HIDDEN(__trax_marker_end = .);
    . = ALIGN(4);
  } > FLASH

  .trax_sm :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__trax_sm_start = .);
    KEEP(*(.trax_sm))
    PROVIDE_HIDDEN(__trax_sm_end = .);
    . = ALIGN(4);
  } > FLASH

  .trax_trigger :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__trax_trigger_start = .);
    KEEP(*(.trax_trigger))
    PROVIDE_HIDDEN(__trax_trigger_end = .);
    . = ALIGN(4);
  } > FLASH

  .trax_var_formula :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__trax_var_formula_start = .);
    KEEP(*(.trax_var_formula))
    PROVIDE_HIDDEN(__trax_var_formula_end = .);
    . = ALIGN(4);
  } > FLASH

  .trax_var_filter :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__trax_var_filter_start = .);
    KEEP(*(.trax_var_filter))
    PROVIDE_HIDDEN(__trax_var_filter_end = .);
    . = ALIGN(4);
  } > FLASH

  /* ============================================================
   * ELF-only metadata output sections.
   * Populated only when TRAX_CFG_META_STORAGE == TRAX_META_ELF_ONLY
   * (the C-side macro emits into the `_elf` input sections).
   * Bytes live at 0x90000000+ — present in the ELF, but `(INFO)` keeps
   * them out of SHF_ALLOC, so they reach neither the .bin/.hex nor the
   * `size` text total (see the METADATA notes in the file header).
   * No boundary symbols: firmware never reads these tables in this
   * mode (trax_meta_tx.c gates the relevant code with #if).
   * ============================================================ */
  .trax_log_elf (INFO) :
  {
    . = ALIGN(4);
    KEEP(*(.trax_log_elf))
    . = ALIGN(4);
  } > TRAX_META_ELF

  .trax_log_vars_elf (INFO) :
  {
    . = ALIGN(4);
    KEEP(*(.trax_log_vars_elf))
    . = ALIGN(4);
  } > TRAX_META_ELF

  .trax_var_elf (INFO) :
  {
    . = ALIGN(4);
    KEEP(*(.trax_var_elf))
    . = ALIGN(4);
  } > TRAX_META_ELF

  .trax_stream_adc_elf (INFO) :
  {
    . = ALIGN(4);
    KEEP(*(.trax_stream_adc_elf))
    . = ALIGN(4);
  } > TRAX_META_ELF

  .trax_stream_rotor_elf (INFO) :
  {
    . = ALIGN(4);
    KEEP(*(.trax_stream_rotor_elf))
    . = ALIGN(4);
  } > TRAX_META_ELF

  .trax_isr_elf (INFO) :
  {
    . = ALIGN(4);
    KEEP(*(.trax_isr_elf))
    . = ALIGN(4);
  } > TRAX_META_ELF

  .trax_marker_elf (INFO) :
  {
    . = ALIGN(4);
    KEEP(*(.trax_marker_elf))
    . = ALIGN(4);
  } > TRAX_META_ELF

  .trax_sm_elf (INFO) :
  {
    . = ALIGN(4);
    KEEP(*(.trax_sm_elf))
    . = ALIGN(4);
  } > TRAX_META_ELF

  .trax_trigger_elf (INFO) :
  {
    . = ALIGN(4);
    KEEP(*(.trax_trigger_elf))
    . = ALIGN(4);
  } > TRAX_META_ELF

  .trax_var_formula_elf (INFO) :
  {
    . = ALIGN(4);
    KEEP(*(.trax_var_formula_elf))
    . = ALIGN(4);
  } > TRAX_META_ELF

  .trax_var_filter_elf (INFO) :
  {
    . = ALIGN(4);
    KEEP(*(.trax_var_filter_elf))
    . = ALIGN(4);
  } > TRAX_META_ELF

  /* ============================================================
   * TraxFault persistent crash record.
   * NOLOAD and a separate output section — never inside the
   * startup zero-fill range (_sbss.._ebss) — so the record written
   * by the fault handler survives NVIC_SystemReset.
   * Auto-sized: EMPTY (zero RAM) when TRAX_CFG_FAULT_ENABLE == 0;
   * grows to TRAX_FAULT_NOINIT_TOTAL when enabled (budget enforced
   * in trax_config_fault.h via TRAX_CFG_FAULT_NOINIT_MAX).
   *
   * PLACEMENT: as an appended fragment this section lands after the
   * board script's RAM sections (above the ._user_heap_stack minimum
   * reservation, below the initial stack at the top of RAM).  That is
   * fine for the crash record's lifetime: it is WRITTEN inside the
   * fault handler and READ once at the next boot — between those two
   * points only reset + startup run, so runtime heap/stack excursions
   * cannot corrupt a record anyone still needs (the CRC seal rejects
   * a clobbered one; the next fault rewrites the section in full).
   * (We cannot pin it right after .bss with `INSERT AFTER .bss` —
   * CubeIDE passes the board script via -T, and ld cannot resolve
   * INSERT anchors across separate -T scripts: ".bss not found for
   * insert".)
   * ============================================================ */
  .trax_noinit (NOLOAD) :
  {
    . = ALIGN(4);
    KEEP(*(.trax_noinit))
    . = ALIGN(4);
  } > RAM
}

/* ============================================================
 * Build-time guard — catch a mis-placed FLASH region.
 *
 * In TRAX_META_IN_FLASH mode the firmware streams the metadata at
 * session start by reading it straight from the `.trax_*` sections
 * (trax_meta_tx.c: memcpy(.., __trax_<kind>_start, ..)).  Those
 * sections are mapped `> FLASH` above, so the project's main linker
 * script MUST expose a flash region named FLASH.  If it names its
 * flash something else (e.g. ROM, m_text, PROGRAM_FLASH) and does
 * not alias it, GNU ld silently collapses these sections to address
 * 0x0 — the firmware then transmits garbage and Traxcope shows no
 * logs/vars/events.  This ASSERT converts that silent failure into a
 * clear build error.
 *
 * How it works, with no hard-coded vendor address:
 *   - It keys off the `__trax_<kind>_start` boundary symbols, NOT off
 *     SIZEOF/ADDR of the output sections.  Those symbols are emitted
 *     (PROVIDE_HIDDEN) for every kind whenever trax_meta_tx.c references
 *     them — i.e. in TRAX_META_IN_FLASH mode — even for kinds with zero
 *     entries.  Referencing them is therefore safe for empty sections
 *     (which ld may discard, making SIZEOF/ADDR on them unreliable).
 *   - A single `DEFINED(__trax_log_start)` ternary gates the whole
 *     check: in TRAX_META_ELF_ONLY mode none of these symbols exist, so
 *     the guard short-circuits to `1` (inert) — the `.trax_*_elf`
 *     sections carry the data at the fragment's own region instead.
 *   - For each kind we require its start to sit at/above `.text`, i.e.
 *     inside the program image.  A collapse to 0x0 fails this
 *     (0 < ADDR(.text)).  `.text` is the anchor because every GNU-ld
 *     output image defines it, regardless of vendor flash-region name.
 *
 * Limitation: on parts whose flash legitimately begins at 0x0 AND
 * whose `.text` is also at 0x0, a collapse cannot be distinguished by
 * address; such targets must still expose a FLASH region for `.text`
 * itself, so the misconfiguration surfaces elsewhere in the link.
 * ============================================================ */
ASSERT(
  DEFINED(__trax_log_start) ?
    (  __trax_log_start         >= ADDR(.text)
    && __trax_log_vars_start    >= ADDR(.text)
    && __trax_var_start         >= ADDR(.text)
    && __trax_stream_adc_start  >= ADDR(.text)
    && __trax_stream_rotor_start >= ADDR(.text)
    && __trax_isr_start         >= ADDR(.text)
    && __trax_marker_start      >= ADDR(.text)
    && __trax_sm_start          >= ADDR(.text)
    && __trax_trigger_start     >= ADDR(.text)
    && __trax_var_formula_start >= ADDR(.text)
    && __trax_var_filter_start  >= ADDR(.text) )
    : 1,
  "TraxProbe: .trax_* metadata is not placed in flash (it collapsed to ~0x0). Your linker script's flash region is most likely not named FLASH. Fix: add  REGION_ALIAS('FLASH', <your_flash_region>);  right after the MEMORY block of your board linker script (e.g.  REGION_ALIAS('FLASH', ROM);  -- use real double-quotes around the region names). Alternatively build with TRAX_CFG_META_STORAGE = TRAX_META_ELF_ONLY (host then reads metadata from the .elf).")
