/**
 * @file    startup.c
 * @brief   Vector table and reset handler, written in C (no assembly file).
 *
 * WHAT HAPPENS AT RESET ON A CORTEX-M4
 * ------------------------------------
 * The core does not "jump to main". After reset it:
 *
 *   1. Reads the 32-bit word at address 0x00000000 and loads it into the
 *      main stack pointer (MSP).
 *   2. Reads the word at 0x00000004 (the "reset vector") and jumps there.
 *
 * On the STM32F429, with the BOOT0 pin low (the Nucleo default), address
 * 0x00000000 is an alias of internal flash at 0x08000000. So the first two
 * words of our flash image must be: initial stack pointer, then the address
 * of Reset_Handler. The linker script places the `.isr_vector` section
 * defined below at the very start of flash to make that true.
 *
 * The rest of the table holds addresses of exception and interrupt
 * handlers. When an exception fires, the core indexes this table by
 * exception number and jumps to the stored address.
 *
 * C RUNTIME SETUP
 * ---------------
 * A C program assumes that, before main() runs:
 *   - initialised globals (`int x = 5;`) hold their initial values, and
 *   - zero-initialised globals (`int y;`, `static int z;`) are zero.
 *
 * Nothing on the chip does this for us. The linker stores initial values of
 * `.data` in flash (load address) but assigns their addresses in RAM (run
 * address); Reset_Handler copies them over. `.bss` simply gets zeroed.
 * The linker script exports the boundary symbols used here.
 */
#include <stdint.h>
#include "stm32f429.h"
#include "board.h"

/* ---- Symbols provided by the linker script (see stm32f429zi.ld) -------- */
/* Only their *addresses* are meaningful; they have no storage of their own. */
extern uint32_t _sidata;  /* start of .data initial values in flash */
extern uint32_t _sdata;   /* start of .data in RAM                  */
extern uint32_t _edata;   /* end of .data in RAM                    */
extern uint32_t _sbss;    /* start of .bss in RAM                   */
extern uint32_t _ebss;    /* end of .bss in RAM                     */
extern uint32_t _estack;  /* top of RAM = initial stack pointer     */

extern int main(void);

void Reset_Handler(void);
void Default_Handler(void);

/* ---- Exception handlers -------------------------------------------------
 * Declared weak and aliased to Default_Handler: if another file defines a
 * function with the same name (e.g. SysTick_Handler in systick.c) the
 * linker uses that one; otherwise the alias catches the exception.
 * ------------------------------------------------------------------------ */
void NMI_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void)    __attribute__((weak, alias("Default_Handler")));

/* The STM32F429 has 91 peripheral interrupt lines (IRQ0 WWDG .. IRQ90
 * DMA2D, RM0090 table 62). This demo polls every peripheral, so all of
 * them point at Default_Handler -- they are never enabled in the NVIC, so
 * none should fire. If one does, Default_Handler makes it obvious. */
#define NUM_IRQS 91

typedef void (*vector_t)(void);

/**
 * The vector table. `used` stops the compiler discarding it (nothing in C
 * references it); the section attribute plus KEEP() in the linker script
 * keep it at the start of flash and safe from --gc-sections.
 */
__attribute__((section(".isr_vector"), used))
const vector_t vector_table[16 + NUM_IRQS] = {
    [0]  = (vector_t)&_estack,     /* initial main stack pointer        */
    [1]  = Reset_Handler,          /* exception 1: reset                */
    [2]  = NMI_Handler,            /* 2: non-maskable interrupt         */
    [3]  = HardFault_Handler,      /* 3: all escalated faults           */
    [4]  = MemManage_Handler,      /* 4: MPU violation                  */
    [5]  = BusFault_Handler,       /* 5: bus error                      */
    [6]  = UsageFault_Handler,     /* 6: undefined instr, div by 0, ... */
    /* 7..10 reserved (left as 0 by the designated initialiser)         */
    [11] = SVC_Handler,            /* 11: supervisor call (svc instr)   */
    [12] = DebugMon_Handler,       /* 12: debug monitor                 */
    /* 13 reserved                                                      */
    [14] = PendSV_Handler,         /* 14: pendable service request      */
    [15] = SysTick_Handler,        /* 15: SysTick timer                 */
    [16 ... 16 + NUM_IRQS - 1] = Default_Handler, /* IRQ0..IRQ90 (GNU range init) */
};

/**
 * Reset_Handler - first code executed after reset.
 *
 * The stack pointer is already valid here (the core loaded it from
 * vector_table[0]), so this can be an ordinary C function. It must not
 * rely on any global variable until .data/.bss are initialised.
 */
void Reset_Handler(void)
{
    /* 1. Enable the FPU.
     *    The Cortex-M4F's single-precision FPU is coprocessors CP10/CP11
     *    and is *disabled* at reset: the first floating-point instruction
     *    would trigger a UsageFault. We compile with -mfloat-abi=hard, so
     *    the compiler may use FPU instructions anywhere -- this has to be
     *    the very first thing we do. The barriers make sure the change has
     *    taken effect before the next instruction executes. */
    SCB_CPACR |= SCB_CPACR_CP10_CP11_FULL;
    dsb();
    isb();

    /* 2. Copy initialised data from flash to RAM. */
    uint32_t *src = &_sidata;
    for (uint32_t *dst = &_sdata; dst < &_edata; ) {
        *dst++ = *src++;
    }

    /* 3. Zero the .bss section. */
    for (uint32_t *dst = &_sbss; dst < &_ebss; ) {
        *dst++ = 0;
    }

    /* 4. Run the application. Embedded main() should never return; if it
     *    does, park the CPU rather than executing whatever follows. */
    (void)main();
    for (;;) {
    }
}

/**
 * Default_Handler - catches any exception/interrupt with no real handler.
 *
 * Most commonly this is a HardFault (bad pointer, unaligned access to
 * device memory, FPU used before enabling, stack overflow into a fault...).
 * We light the red LED and spin so it's visible on the board; attach a
 * debugger and inspect the stacked registers to find the culprit.
 *
 * It writes the GPIO registers directly rather than calling board code,
 * because the fault may have happened anywhere -- the less this depends
 * on, the better. If GPIOB's clock is not enabled yet, the write is simply
 * ignored and the CPU still halts here.
 */
void Default_Handler(void)
{
    GPIOB->BSRR = (1UL << LED_RED_PIN);
    for (;;) {
    }
}
