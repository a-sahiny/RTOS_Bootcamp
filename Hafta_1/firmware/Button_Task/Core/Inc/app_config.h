/**
 * app_config.h
 *
 * Uygulama genelinde kullanilan sabitler, gorev oncelikleri, kuyruk
 * derinlikleri ve donanim eslemesi. Bkz. SPEC.md.
 *
 * Donanim degerleri STM32F746G-DISCO icin dogrulanmistir (Zephyr kart
 * DTS dosyasi, NuttX kart dokumantasyonu, UM1907):
 *  - ST-LINK VCP -> USART1, TX=PA9, RX=PB7
 *  - Kullanici butonu B1 -> PI11, GPIO_ACTIVE_HIGH (basilinca YUKSEK)
 *  - HSE 25 MHz kristal (bypass degil), SYSCLK 216 MHz
 * Ayni degerler ButtonLatency.ioc icinde de tanimlidir; birini degistirirseniz
 * digerini de guncelleyin.
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "cmsis_os2.h"

/* ------------------------------------------------------------------ */
/* Derleme aninda deney modu secimi                                    */
/*                                                                    */
/* Asagidaki iki tanimdan SADECE BIRINI aktif et. Hicbiri aktif       */
/* degilse APP_TX_MODE_ORIGINAL otomatik secilir. Alternatif olarak   */
/* CubeIDE > C/C++ Build > Settings > MCU GCC Compiler > Preprocessor */
/* altindan tek bir modu -D ile tanimlayabilirsin.                     */
/* ------------------------------------------------------------------ */
/* #define APP_TX_MODE_LARGE_FIFO */
/* #define APP_TX_MODE_DUAL_QUEUE */
/* #define APP_TX_MODE_ORIGINAL   */

#if !defined(APP_TX_MODE_ORIGINAL) && \
    !defined(APP_TX_MODE_LARGE_FIFO) && \
    !defined(APP_TX_MODE_DUAL_QUEUE)
#define APP_TX_MODE_LARGE_FIFO
#endif

#if (defined(APP_TX_MODE_ORIGINAL) + \
     defined(APP_TX_MODE_LARGE_FIFO) + \
     defined(APP_TX_MODE_DUAL_QUEUE)) != 1
#error "TX modu icin tam olarak BIR #define secilmelidir."
#endif

/* ------------------------------------------------------------------ */
/* Donanim eslemesi (ButtonLatency.ioc ile ayni olmali)                */
/* ------------------------------------------------------------------ */
#define APP_UART_HANDLE            huart1
#define APP_UART_INSTANCE          USART1

#define USER_BUTTON_PORT           GPIOI
#define USER_BUTTON_PIN            GPIO_PIN_11
#define USER_BUTTON_PRESSED_STATE  GPIO_PIN_SET   /* aktif-YUKSEK */

/* ------------------------------------------------------------------ */
/* Protokol / cerceve sabitleri (SPEC.md S8)                           */
/* ------------------------------------------------------------------ */
#define APP_FRAME_SIZE              64u     /* toplam cerceve boyutu       */
#define APP_FRAME_PAYLOAD_SIZE      62u     /* ASCII alanlar + bosluk pad  */
#define APP_FRAME_CRC_INDEX         62u     /* 1 bayt CRC-8                */
#define APP_FRAME_LF_INDEX          63u     /* '\n'                        */
#define APP_FRAME_LF_BYTE           ((uint8_t)'\n')

#define APP_CRC8_POLY               0x07u
#define APP_CRC8_INIT               0x00u

#define APP_CMD_LINE_MAX_LEN        64u     /* PC->MCU komut satiri ust siniri (LF haric) */

/* ------------------------------------------------------------------ */
/* Zamanlama / deadline (SPEC.md S6, S8.6)                             */
/* ------------------------------------------------------------------ */
#define APP_DEADLINE_US              20000u  /* R = t4 - t0 <= 20 ms       */
#define APP_DEBOUNCE_MS              30u     /* ileride dusurulebilir     */
#define APP_TX_TC_TIMEOUT_MS         100u    /* TC bekleme ust siniri (~5.56 ms beklenen) */
#define APP_TX_QUEUE_SEND_TIMEOUT_MS 200u    /* TxQueue'ya birakma ust siniri */
#define APP_MIN_TELEMETRY_PERIOD_MS  10u     /* fiziksel bant genisligi tavani, SPEC S8.6 */

/* osDelayUntil / periyotlar kernel tick cinsindendir; asagidaki hesaplar
 * 1 tick = 1 ms varsayar (CubeMX FreeRTOS varsayilani TICK_RATE_HZ=1000). */
#define APP_KERNEL_TICK_HZ           1000u

/* ------------------------------------------------------------------ */
/* Kuyruk / tampon derinlikleri (SPEC.md S4, S7)                       */
/* ------------------------------------------------------------------ */
#define APP_BUTTON_QUEUE_LEN         8u
/* Tek FIFO orijinal: 16; sadece FIFO arttirma deneyi: 64.
 * Iki kuyruk modunda TEL FIFO 16, BTN FIFO 8 elemandir. */
#ifdef APP_TX_MODE_ORIGINAL
#define APP_TX_QUEUE_LEN             16u
#endif
#ifdef APP_TX_MODE_LARGE_FIFO
#define APP_TX_QUEUE_LEN             256u
#endif
#ifdef APP_TX_MODE_DUAL_QUEUE
#define APP_TX_QUEUE_LEN             16u
#define APP_BTN_TX_QUEUE_LEN          8u
#endif
#define APP_CMD_QUEUE_LEN            4u
#define APP_RING_BUFFER_CAPACITY     256u    /* asgari 64 gereksinimini asar */

/* ------------------------------------------------------------------ */
/* Gorev oncelikleri (CMSIS-RTOS v2) - SPEC.md S4 iliskisiyle birebir  */
/*   UartTxTask (Dusuk-1) < ButtonTask (Orta-2) < TelemetryTask (Yuksek-3) */
/* ------------------------------------------------------------------ */
#define APP_PRIO_UART_TX_TASK        osPriorityLow
#define APP_PRIO_BUTTON_TASK         osPriorityNormal
#define APP_PRIO_TELEMETRY_TASK      osPriorityAboveNormal

/* osThreadAttr_t.stack_size birimi BAYT'tir (CMSIS-RTOS2). newlib-nano
 * snprintf tek basina ~300-400 B yigin kullanabilir. */
#define APP_STACK_UART_TX_TASK       (1536u)
#define APP_STACK_BUTTON_TASK        (1024u)
#define APP_STACK_TELEMETRY_TASK     (1536u)

/* ------------------------------------------------------------------ */
/* Senaryo yapilandirmasi varsayilanlari                               */
/* ------------------------------------------------------------------ */
#define APP_DEFAULT_TELEMETRY_PERIOD_MS  200u
#define APP_DEFAULT_LOAD_ITERATIONS      0u
#define APP_DEFAULT_SCENARIO_ID          0u

#endif /* APP_CONFIG_H */
