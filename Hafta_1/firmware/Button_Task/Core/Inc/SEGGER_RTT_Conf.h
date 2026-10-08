#ifndef SEGGER_RTT_CONF_H
#define SEGGER_RTT_CONF_H

#define SEGGER_RTT_MAX_NUM_UP_BUFFERS   2
#define SEGGER_RTT_MAX_NUM_DOWN_BUFFERS 2
#define BUFFER_SIZE_UP               16
#define BUFFER_SIZE_DOWN             16
#define RTT_USE_ASM                  0

/* Protect task, scheduler and ISR producers; preserve nested lock state. */
#define SEGGER_RTT_LOCK()   { unsigned rtt_primask; \
    __asm volatile ("mrs %0, primask\n\tcpsid i" : "=r" (rtt_primask) :: "memory");
#define SEGGER_RTT_UNLOCK() \
    __asm volatile ("msr primask, %0" :: "r" (rtt_primask) : "memory"); }

#endif
