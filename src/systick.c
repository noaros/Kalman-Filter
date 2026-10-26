/**
 * @file    systick.c
 * @brief   SysTick: the 24-bit down-counter built into every Cortex-M core.
 *
 * SysTick counts down from LOAD to 0 on each processor clock, then reloads
 * and (with TICKINT set) raises the SysTick exception. With LOAD = N - 1 the
 * period is exactly N clock cycles. 16 MHz / 1 kHz = 16000 cycles, well
 * within the 24-bit (16.7 M) limit.
 *
 * SysTick is an *exception*, not a peripheral IRQ, so it needs no NVIC
 * enable -- setting TICKINT is enough. Its priority defaults to 0.
 */
#include "systick.h"
#include "stm32f429.h"

/* Written by the exception handler, read by main code: must be volatile,
 * or the compiler could hoist the read out of a polling loop. A 32-bit
 * aligned load is atomic on Cortex-M, so no locking is needed to read it. */
static volatile uint32_t s_ms_ticks;

void SysTick_Handler(void)  /* overrides the weak alias in startup.c */
{
    s_ms_ticks++;
}

void systick_init(uint32_t core_clock_hz)
{
    SYSTICK->CTRL = 0;                          /* stop while configuring */
    SYSTICK->LOAD = core_clock_hz / 1000u - 1u; /* 1 ms period            */
    SYSTICK->VAL  = 0;                          /* any write clears count */
    SYSTICK->CTRL = SYSTICK_CTRL_CLKSOURCE      /* run from core clock    */
                  | SYSTICK_CTRL_TICKINT        /* exception on wrap      */
                  | SYSTICK_CTRL_ENABLE;
}

uint32_t millis(void)
{
    return s_ms_ticks;
}

void delay_ms(uint32_t ms)
{
    /* +1 guarantees *at least* `ms`: we may be called just before a tick. */
    const uint32_t deadline = millis() + ms + 1u;
    while (!time_reached(millis(), deadline)) {
        /* Could execute `wfi` here to sleep until the next interrupt. */
    }
}
