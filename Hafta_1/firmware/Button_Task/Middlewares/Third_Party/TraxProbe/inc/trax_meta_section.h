/*
 * SPDX-License-Identifier: LicenseRef-TraxProbe-Commercial
 * Copyright (c) 2026 Embedya. All rights reserved.
 *
 * TraxProbe core engine. CONFIDENTIAL and proprietary to Embedya.
 * Licensed under the TraxProbe Commercial License (see LICENSE-COMMERCIAL.txt).
 * No use, copying, modification, redistribution, decompilation, or reverse
 * engineering is permitted except as expressly authorized by that agreement.
 */

/**
 ******************************************************************************
 * @file        : trax_meta_section.h
 * @brief       : ELF input-section name selector for TraxProbe metadata
 ******************************************************************************
 * @attention
 *
 * This header maps each TraxProbe metadata kind (log, var, isr, ...) to the
 * ELF input-section name it should be emitted into, based on the single
 * configuration switch in `App/Cfg/trax_config.h`:
 *
 *   #define TRAX_CFG_META_STORAGE   TRAX_META_IN_FLASH    // section in flash + ELF
 *   #define TRAX_CFG_META_STORAGE   TRAX_META_ELF_ONLY    // section only in ELF
 *
 * The companion linker fragment (`trax_probe.ld`) defines TWO output sections
 * per metadata kind — `.trax_<kind>` placed in FLASH and `.trax_<kind>_elf`
 * placed in a non-flash pseudo region.  Whichever input name we emit into
 * here is the one that ends up populated; the other output section is empty
 * and harmless.  This keeps everything driven from a single C macro and
 * needs no preprocessor pass on the linker script.
 *
 * The host (Traxcope) probes both names per kind, so neither side has to
 * know which mode the firmware was built in — it just reads whichever
 * section actually has bytes.
 *
 ******************************************************************************
 */

#ifndef TRAX_META_SECTION_H_
#define TRAX_META_SECTION_H_

#include "trax_config_default.h"  /* TRAX_CFG_META_STORAGE + option constants */

#if (TRAX_CFG_META_STORAGE == TRAX_META_IN_FLASH)

  #define TRAX_META_SECTION_LOG          ".trax_log"
  #define TRAX_META_SECTION_LOG_VARS     ".trax_log_vars"
  #define TRAX_META_SECTION_VAR          ".trax_var"
  #define TRAX_META_SECTION_STREAM_ADC   ".trax_stream_adc"
  #define TRAX_META_SECTION_STREAM_ROTOR ".trax_stream_rotor"
  #define TRAX_META_SECTION_ISR          ".trax_isr"
  #define TRAX_META_SECTION_MARKER       ".trax_marker"
  #define TRAX_META_SECTION_SM           ".trax_sm"
  #define TRAX_META_SECTION_TRIGGER      ".trax_trigger"
  #define TRAX_META_SECTION_VAR_FORMULA  ".trax_var_formula"
  #define TRAX_META_SECTION_VAR_FILTER   ".trax_var_filter"

#else  /* TRAX_META_ELF_ONLY */

  #define TRAX_META_SECTION_LOG          ".trax_log_elf"
  #define TRAX_META_SECTION_LOG_VARS     ".trax_log_vars_elf"
  #define TRAX_META_SECTION_VAR          ".trax_var_elf"
  #define TRAX_META_SECTION_STREAM_ADC   ".trax_stream_adc_elf"
  #define TRAX_META_SECTION_STREAM_ROTOR ".trax_stream_rotor_elf"
  #define TRAX_META_SECTION_ISR          ".trax_isr_elf"
  #define TRAX_META_SECTION_MARKER       ".trax_marker_elf"
  #define TRAX_META_SECTION_SM           ".trax_sm_elf"
  #define TRAX_META_SECTION_TRIGGER      ".trax_trigger_elf"
  #define TRAX_META_SECTION_VAR_FORMULA  ".trax_var_formula_elf"
  #define TRAX_META_SECTION_VAR_FILTER   ".trax_var_filter_elf"

#endif

#endif /* TRAX_META_SECTION_H_ */
