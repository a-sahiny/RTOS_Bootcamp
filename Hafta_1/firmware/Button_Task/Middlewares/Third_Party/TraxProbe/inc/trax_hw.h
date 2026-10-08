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
 * @file           : trax_hw.h
 * @brief          : TraxProbe Hardware Port Selection
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 *
 * This file eliminates the need for users to manually add hardware-port-specific
 * include paths in their IDE. It reads `TRAX_CFG_HW_PORT` from the user's
 * `trax_config.h` and routes `#include` directives statically.
 *
 ******************************************************************************
 */

#ifndef TRAX_HW_H_
#define TRAX_HW_H_

#include "../config/trax_config_options.h"
#include "trax_config.h"

#if (TRAX_CFG_HW_PORT == TRAX_HW_PORT_ARM_CORTEX_M)
    #include "../hw_port/ARM_Cortex_M/include/trax_hw_port.h"
#elif (TRAX_CFG_HW_PORT == TRAX_HW_PORT_GENERIC)
    #include "../hw_port/Generic/include/trax_hw_port.h"
#elif (TRAX_CFG_HW_PORT == TRAX_HW_PORT_ZYNQ)
    #include "../hw_port/Zynq/include/trax_hw_port.h"
#elif (TRAX_CFG_HW_PORT == TRAX_HW_PORT_XTENSA)
    #include "../hw_port/Xtensa/include/trax_hw_port.h"
#elif (TRAX_CFG_HW_PORT == TRAX_HW_PORT_MICROBLAZE)
    #include "../hw_port/MicroBlaze/include/trax_hw_port.h"
#elif (TRAX_CFG_HW_PORT == TRAX_HW_PORT_NONE)
    /* "No port selected" sentinel. Nothing is included here on purpose:
     * trax_config_default.h has already raised a hard #error telling the user
     * to set TRAX_CFG_HW_PORT, so the build never reaches code needing a port. */
#else
    #error "TraxProbe: Unknown or undefined TRAX_CFG_HW_PORT in trax_config.h!"
#endif

#endif /* TRAX_HW_H_ */
