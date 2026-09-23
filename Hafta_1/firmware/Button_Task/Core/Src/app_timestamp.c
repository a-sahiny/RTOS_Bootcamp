#include "app_timestamp.h"

#define DWT_LAR_UNLOCK_KEY 0xC5ACCE55u

bool Timestamp_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    /* Cortex-M7'de DWT registerlari Lock Access Register ile kilitlidir;
     * debugger bagli degilken acilmazsa CYCCNT hic saymaz. */
    DWT->LAR = DWT_LAR_UNLOCK_KEY;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    uint32_t before = DWT->CYCCNT;
    for (volatile int i = 0; i < 16; i++) {
    }
    return DWT->CYCCNT != before;
}

uint32_t Timestamp_DeltaToUs(uint32_t from, uint32_t to)
{
    uint32_t delta_cycles = to - from;
    uint32_t cycles_per_us = SystemCoreClock / 1000000u;
    if (cycles_per_us == 0u) {
        cycles_per_us = 1u;
    }
    return delta_cycles / cycles_per_us;
}
