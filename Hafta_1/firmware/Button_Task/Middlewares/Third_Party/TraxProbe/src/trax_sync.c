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
 * @file           : trax_sync.c
 * @brief          : Multi-probe timestamp synchronization implementation
 * @version        : 5.0.0
 ******************************************************************************
 */

#include "trax_sync.h"
#include "trax_frame.h"
#include "trax_timestamp.h"
#include "trax_hw.h"
#include "trax_tid.h"

/* TraxProbe disabled (TRAX_ENABLE==0): whole unit compiles out. The sync
 * entry points are provided as inline no-op stubs in trax_sync.h. */
#if TRAX_ENABLE

void trax_sync_triggered(void)
{
    uint32_t ts       = TRAX_HW_PORT_TIMEPACKED_GET32();
    uint32_t overflow = trax_timebase.tick_overflow_cntr;
    TRAX_FRAME_ARGS_PROTOCOL(TRAX_TID_SYNC_TIMESTAMP, ts, overflow);
}
void trax_sync_trigger_start(void)
{
    /* Compiled away entirely on SLAVE / NONE builds — nothing to drive. */
#if (TRAX_CFG_SYNC_ROLE == TRAX_SYNC_ROLE_MASTER)

    /* Pull the shared line LOW and capture the master's own timestamp
     * atomically.  The falling edge propagates to all slaves over the
     * open-drain bus; they capture it independently in their EXTI ISRs.
     *
     * The critical section guarantees that no other ISR can slip in
     * between the GPIO edge and the timestamp read — otherwise the
     * master's logged time would drift relative to the edge the slaves
     * actually saw. */
    TRAX_PORT_ENTER_CRITICAL_SECTION
        TRAX_CFG_SYNC_GPIO_SET_LOW();
        trax_sync_triggered();
    TRAX_PORT_EXIT_CRITICAL_SECTION

#endif /* TRAX_CFG_SYNC_ROLE == MASTER */
}

void trax_sync_trigger_end(void)
{
    /* Compiled away entirely on SLAVE / NONE builds — nothing to drive. */
#if (TRAX_CFG_SYNC_ROLE == TRAX_SYNC_ROLE_MASTER)

    /* The release edge is not a sync event (slaves only EXTI on falling
     * edges), so no critical section / timestamp capture is required. */
    TRAX_CFG_SYNC_GPIO_SET_HIGH();

#endif /* TRAX_CFG_SYNC_ROLE == MASTER */
}

#endif /* TRAX_ENABLE */
