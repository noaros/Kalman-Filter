/**
 * @file    systick.h
 * @brief   1 ms system tick based on the Cortex-M SysTick timer.
 */
#ifndef SYSTICK_H
#define SYSTICK_H

#include <stdint.h>

/** Start a 1 kHz tick interrupt from a core clock of `core_clock_hz`. */
void systick_init(uint32_t core_clock_hz);

/** Milliseconds since systick_init(). Wraps after ~49.7 days. */
uint32_t millis(void);

/** Busy-wait for at least `ms` milliseconds. */
void delay_ms(uint32_t ms);

/**
 * True once `now` has reached or passed `deadline`, even across the
 * 32-bit wrap-around: the subtraction is done modulo 2^32 and interpreted
 * as signed, which is correct as long as the two are < 24.8 days apart.
 */
static inline int time_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

#endif /* SYSTICK_H */
