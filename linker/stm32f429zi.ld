/*
 * stm32f429zi.ld - GNU ld linker script for the STM32F429ZI
 *
 * A linker script answers two questions:
 *   1. What memory exists, and where?            (MEMORY)
 *   2. Which pieces of the program go where?     (SECTIONS)
 *
 * The compiler puts code and data into named sections (.text, .rodata,
 * .data, .bss, plus our own .isr_vector). This script places them into
 * flash and RAM and exports symbols that startup.c uses to initialise RAM.
 *
 * Memory map from DS9405 (STM32F429xx datasheet), figure "Memory map":
 *   Flash   2 MB   @ 0x0800_0000   (aliased at 0x0000_0000 when BOOT0=0)
 *   SRAM  192 KB   @ 0x2000_0000   (SRAM1 112K + SRAM2 16K + SRAM3 64K,
 *                                   contiguous)
 *   CCM    64 KB   @ 0x1000_0000   (core-coupled; CPU-only, no DMA; unused)
 */

/* The first instruction to run. Mainly informs debuggers / ELF loaders;
 * the hardware itself uses the reset vector in the vector table. */
ENTRY(Reset_Handler)

MEMORY
{
    FLASH  (rx)  : ORIGIN = 0x08000000, LENGTH = 2048K
    RAM    (rwx) : ORIGIN = 0x20000000, LENGTH = 192K
    CCMRAM (rw)  : ORIGIN = 0x10000000, LENGTH = 64K
}

/* The stack starts at the top of RAM and grows downward (full-descending
 * stack, as required by the ARM ABI). 0x20030000 is 8-byte aligned, which
 * the AAPCS requires at public function boundaries. */
_estack = ORIGIN(RAM) + LENGTH(RAM);

/* Minimum free space we demand between the end of .bss and the top of RAM.
 * This is not a hard limit at run time -- nothing stops the stack growing
 * further -- but the link fails if static data leaves less room than this. */
_min_stack_size = 0x1000;  /* 4 KB, ample for this demo */

SECTIONS
{
    /* Vector table: MUST be first in flash (see startup.c). KEEP() stops
     * --gc-sections from discarding it, as no code references it. */
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    /* Program code and read-only data (const variables, string literals,
     * float constants). -ffunction-sections puts each function in its own
     * .text.<name> section, hence the wildcard. */
    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    /* Unwind tables for exceptions/backtraces. C code compiled without
     * -funwind-tables normally produces none, but libgcc may, so give them
     * a home rather than an "orphan section" warning. */
    .ARM.exidx :
    {
        *(.ARM.exidx*)
    } > FLASH

    /* Initialised data. "> RAM AT > FLASH" means: addresses are in RAM
     * (where the code will access the variables), but the initial bytes are
     * stored in flash right after .text. Reset_Handler copies them. */
    _sidata = LOADADDR(.data);

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    /* Zero-initialised data. NOLOAD: no bytes stored in the image at all,
     * Reset_Handler just zeroes the range. */
    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /* Reserve the minimum stack. Takes no space in the image; it only makes
     * the linker check that RAM is big enough. */
    ._stack_reserve (NOLOAD) :
    {
        . = ALIGN(8);
        . = . + _min_stack_size;
    } > RAM

    /* Not needed on target. */
    /DISCARD/ :
    {
        *(.comment)
    }
}
