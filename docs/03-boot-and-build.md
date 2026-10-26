# 3. Boot and build

What happens between `make` and the first CSV line, without the usual
toolchain abstractions.

---

## 3.1 From power-on to `main()`

```
 power on / reset
       │
       ▼
 core reads 0x0000_0000 → MSP      (aliased to flash 0x0800_0000, BOOT0 = 0)
 core reads 0x0000_0004 → PC       (address of Reset_Handler, Thumb bit set)
       │
       ▼
 Reset_Handler()                    src/startup.c
   1. enable FPU   (SCB->CPACR CP10/CP11 = full access; DSB; ISB)
   2. copy .data   flash [_sidata..] → RAM [_sdata.._edata)
   3. zero .bss    RAM [_sbss.._ebss)
   4. main()
       │
       ▼
 main()                             src/main.c
   flash caches → SysTick → GPIO → UART → ADC/calibration → loop forever
```

### The vector table

The Cortex-M core needs no assembly start-up. After reset it **loads the
stack pointer and the program counter from the first two words of
memory**. Everything else in the table is a list of handler addresses,
indexed by exception number:

| Index | Exception | Handler here |
|---|---|---|
| 0 | (initial MSP) | `_estack` = 0x2003_0000 |
| 1 | Reset | `Reset_Handler` |
| 2–6 | NMI, HardFault, MemManage, BusFault, UsageFault | `Default_Handler` (red LED, spin) |
| 11, 12, 14 | SVCall, DebugMon, PendSV | `Default_Handler` |
| 15 | SysTick | `SysTick_Handler` (systick.c) |
| 16–106 | IRQ0–IRQ90 (peripherals) | `Default_Handler`; none are enabled |

`startup.c` builds this table as a `const` array of function pointers in a
section called `.isr_vector`. The linker script places that section first in
flash. Handlers are **weak aliases**, so defining a function with the same
name anywhere, like `SysTick_Handler` in `systick.c`, replaces the default
without touching `startup.c`.

### Why the FPU is enabled first

The project compiles with `-mfloat-abi=hard`, so the compiler may use FPU
registers and instructions **anywhere**, even for copying data. The FPU is
off at reset, and the first FPU instruction would fault. So
`Reset_Handler` enables it before anything else, then runs `DSB`/`ISB` so
the change takes effect before the next instruction.

### Why `.data` and `.bss` need code

C promises that `int a = 5;` starts at 5 and `static int b;` starts at 0.
On a microcontroller, RAM holds garbage at power-on. The linker therefore:

- stores the initial *values* of `.data` in flash after the code (its
  **load address**, LMA), while assigning the *variables* addresses in RAM
  (the **virtual address**, VMA), and
- gives `.bss` RAM addresses only, with nothing stored in flash.

Reset_Handler copies one region and zeroes the other, using the boundary
symbols the linker script exports. Until that has run, no global can be
trusted. That is why the copy loops use only locals.

## 3.2 The linker script

[`linker/stm32f429zi.ld`](../linker/stm32f429zi.ld), section by section:

```
FLASH 0x0800_0000 ┌───────────────┐
                  │ .isr_vector   │ 428 bytes (107 × 4)
                  │ .text         │ code + .rodata (const, strings, float literals)
                  │ .ARM.exidx    │ (empty here)
                  │ .data image   │ initial values, copied to RAM at boot
                  └───────────────┘
RAM   0x2000_0000 ┌───────────────┐
                  │ .data         │
                  │ .bss          │
                  │ (stack check) │ ≥ 4 KB reserved, or the link fails
                  │      ...      │
                  │   ↓ stack     │
      0x2003_0000 └───────────────┘ _estack
```

- `KEEP(*(.isr_vector))`: nothing references the vector table from code,
  so without `KEEP` the `--gc-sections` option would throw it away.
- `> RAM AT > FLASH` on `.data` is what gives it the RAM/flash address
  split described above.
- `._stack_reserve` takes no space in the image. It only makes the link
  fail if static data leaves less than `_min_stack_size` of RAM. The stack
  itself is not limited at run time.

## 3.3 Compiler and linker flags

From the [`Makefile`](../Makefile), and why each one is there:

| Flag | Purpose |
|---|---|
| `-mcpu=cortex-m4 -mthumb` | The core; Cortex-M only runs Thumb-2 |
| `-mfpu=fpv4-sp-d16 -mfloat-abi=hard` | Use the single-precision FPU and pass floats in FPU registers |
| `-O2 -g3` | Optimise; full debug info (not loaded into flash) |
| `-ffreestanding` | No hosted C library assumed |
| `-fno-math-errno` | Lets `__builtin_sqrtf` become a single `vsqrt.f32` with no libm |
| `-fno-tree-loop-distribute-patterns` | Stops GCC turning copy/zero loops into `memcpy`/`memset` calls (which would recurse inside `libc_min.c`) |
| `-ffunction-sections -fdata-sections` + `-Wl,--gc-sections` | Drop unused functions and data |
| `-Wdouble-promotion` | Warn on any hidden `float` → `double`, which would run as slow software maths |
| `-nostdlib` | No crt0 and no libc; `startup.c` and `libc_min.c` are the whole runtime |
| `-lgcc` | Compiler helper routines, if any are needed |
| `-Wl,--print-memory-usage` | Prints the FLASH/RAM usage table after each link |

### Checking the result

```sh
make size      # per-section sizes
make disasm    # build/kalman.lst: C source interleaved with assembly
arm-none-eabi-nm -n build/kalman.elf | head    # symbols by address
arm-none-eabi-objdump -d build/kalman.elf | grep vsqrt   # hardware sqrt in use
less build/kalman.map                           # where every byte went
```

`vector_table` should be at `0x08000000`. The disassembly should contain no
calls to `__aeabi_d*` (soft double) or `memcpy`/`memset` other than our
own.

## 3.4 Debugging

The ELF has full debug info. Build with `make OPT=-Og` so the code steps
in source order, then do one of the following.

**OpenOCD + GDB:**

```sh
openocd -f board/st_nucleo_f4.cfg            # terminal 1
arm-none-eabi-gdb build/kalman.elf           # terminal 2
(gdb) target extended-remote :3333
(gdb) monitor reset halt
(gdb) load
(gdb) break kalman1d_update
(gdb) continue
(gdb) print *kf
```

**STM32CubeProgrammer's GDB server** (`ST-LINK_gdbserver`, shipped with
STM32CubeIDE) or **VS Code + Cortex-Debug** work the same way.

**After a fault** (red LED on, heartbeat stopped), halt in the debugger.
You'll be in `Default_Handler`. Then:

- `info registers` and look at `xPSR`: the low bits give the exception
  number (3 = HardFault).
- The stacked PC of the faulting instruction is at `[sp + 24]`:
  `x/8wx $sp`. This holds when the fault happened in main-loop code.
- `x/wx 0xE000ED28` reads the CFSR fault status register (PM0214 4.4.11).

## 3.5 Porting to other STM32s

What changes if you move the demo to another chip:

| Item | Where |
|---|---|
| Flash/RAM sizes | `MEMORY` in the linker script |
| Number of IRQs | `NUM_IRQS` in `startup.c` |
| Register addresses | `stm32f429.h` (most F4 parts are identical) |
| Sensor channel, VBAT sharing | `temp_sensor.c`; F40x uses channel 16 |
| Calibration addresses, temperatures | `stm32f429.h`; e.g. on L4/G4, CAL2 is at 130 °C and CAL VDDA is 3.0 V |
| VCP pins and USART | `uart.c`; see your board's user manual |
| LEDs and button | `board.h` |
