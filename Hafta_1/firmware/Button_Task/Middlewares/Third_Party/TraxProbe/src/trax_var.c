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
 * @file           : trax_var.c
 * @brief          : TraxProbe VAR (Variable) Implementation
 * @version        : 3.0.0
 ******************************************************************************
 * @attention
 *
 * Most VAR operations are implemented as macros in trax_var.h for
 * performance reasons.
 *
 * COMPILE-TIME CONFIGURATION (TRAX_CFG_META_STORAGE):
 *   0 = ELF-only: Metadata sending functions compiled out
 *   1 = Embedded: Metadata sending functions included
 *
 ******************************************************************************
 */


#include "trax_var.h"
#include "trax_config_default.h"
#include "trax_meta_type.h"  /* For trax_var_meta_t */

/* TraxProbe disabled (TRAX_ENABLE==0): whole unit compiles out. The public VAR
 * API is provided as no-op macros in trax_var.h. */
#if TRAX_ENABLE

/*=============================================================================
 ====================GLOBAL FUNCTION IMPLEMENTATION============================
 ============================================================================*/

void trax_var_init(void)
{
    /* No runtime state for VARs */
}

#endif /* TRAX_ENABLE */


