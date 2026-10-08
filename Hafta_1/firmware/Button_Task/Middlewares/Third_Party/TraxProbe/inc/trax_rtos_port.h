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
 * @file           : trax_rtos_port.h
 * @brief          : RTOS port dispatcher
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 *
 * Selects and includes the correct RTOS port header based on TRAX_CFG_RTOS_TYPE
 * defined in App/Config/trax_config.h.
 *
 * Application code should include only trax.h. When TRAX_CFG_RTOS_TYPE is not
 * TRAX_RTOS_NONE, trax.h pulls this dispatcher in automatically. In
 * FreeRTOSConfig.h that #include "trax.h" must come AFTER the config* macros
 * (especially configUSE_TRACE_FACILITY). Do not also include this header
 * from FreeRTOSConfig.h — it is redundant.
 *
 * Add only TraxProbe/include/ to your compiler include path. No RTOS-specific
 * subdirectory is needed; the correct port is resolved automatically.
 *
 * Each RTOS port implementation lives at:
 *   os/FreeRTOS/trax_rtos_port.h
 *   os/Zephyr/trax_rtos_port.h   (when available)
 *   ...
 *
 ******************************************************************************
 */

#ifndef TRAX_RTOS_PORT_H_
#define TRAX_RTOS_PORT_H_

#include "trax_tid.h"
#include "trax_config_default.h"

#if   (TRAX_CFG_RTOS_TYPE == TRAX_RTOS_FREERTOS)
    #include "../os/FreeRTOS/trax_rtos_port.h"
#elif (TRAX_CFG_RTOS_TYPE == TRAX_RTOS_NONE)
    /* Bare metal — no RTOS port needed */
#else
    #warning "trax_rtos_port.h: no port available for the configured TRAX_CFG_RTOS_TYPE"
#endif

/* Fallback for the task-priority ordering byte sent in the SESSION_START
 * header. Each RTOS / OS port is expected to define
 * `TRAX_CFG_TASK_PRIO_ASCENDING` itself as a hard `#define` (no
 * `#ifndef`), since the convention is a kernel API fact and varies
 * between kernels. Audited ports today:
 *   - FreeRTOS (`os/FreeRTOS/trax_rtos_port.h`)         → 1
 * The fallback below only applies to:
 *   - Bare metal (`TRAX_RTOS_NONE`) — no tasks exist, the byte still
 *     needs a value but is informational only.
 *   - Unported RTOS types — the `#warning` above already told the user
 *     to add a port; defaulting to 0 keeps the build going. */
#ifndef TRAX_CFG_TASK_PRIO_ASCENDING
#define TRAX_CFG_TASK_PRIO_ASCENDING  0
#endif

#endif /* TRAX_RTOS_PORT_H_ */
