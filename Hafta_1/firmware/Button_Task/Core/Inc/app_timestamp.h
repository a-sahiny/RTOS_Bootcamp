/**
 * app_timestamp.h
 *
 * Cortex-M7 DWT cycle counter tabanli yuksek cozunurluklu zaman damgasi.
 * FreeRTOS tick'i (1 ms) yerine kullanilir; bkz. SPEC.md S5.
 */
#ifndef APP_TIMESTAMP_H
#define APP_TIMESTAMP_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32f7xx_hal.h"   /* DWT/CoreDebug tanimlari icin (core_cm7.h) */

/** DWT->CYCCNT'i acar ve sifirlar. RTOS baslamadan once cagrilmalidir.
 *  Sayac gercekten ilerliyorsa true doner (Cortex-M7 LAR kilidi dahil). */
bool Timestamp_Init(void);

/** Anlik DWT cycle sayacini dondurur (ISR-safe, tek register okumasi). */
static inline uint32_t Timestamp_Now(void)
{
    return DWT->CYCCNT;
}

/**
 * `from` -> `to` arasindaki cycle farkini mikrosaniyeye cevirir.
 * 32-bit CYCCNT 216 MHz'de ~19.9 s'de tasar; islaretsiz cikarma tek
 * tasmayi dogru ele alir (tek olayin t0..t4 araligi bundan cok kisadir).
 * Donusum calisma anindaki SystemCoreClock ile yapilir.
 */
uint32_t Timestamp_DeltaToUs(uint32_t from, uint32_t to);

#endif /* APP_TIMESTAMP_H */
