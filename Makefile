# =============================================================================
#  Makefile - STM32F429ZI bare-metal Kalman filter demo
#
#  Targets
#    make            build build/kalman.elf, .bin and .hex
#    make flash      program the board via ST-LINK (STM32CubeProgrammer CLI)
#    make test       build and run the Kalman filter unit test on the host
#    make size       show flash/RAM usage per section
#    make disasm     write a full disassembly to build/kalman.lst
#    make clean      delete build/
#
#  Variables you may override on the command line, e.g. `make OPT=-O0`:
#    CROSS    toolchain prefix       (default arm-none-eabi-)
#    OPT      optimisation level     (default -O2; use -Og for debugging)
#    FLASHER  flash program command  (default STM32_Programmer_CLI)
# =============================================================================

CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
OBJDUMP := $(CROSS)objdump
SIZE    := $(CROSS)size
HOSTCC  ?= cc

TARGET  := kalman
BUILD   := build
LDSCRIPT := linker/stm32f429zi.ld

SRCS := src/startup.c \
        src/libc_min.c \
        src/board.c \
        src/systick.c \
        src/uart.c \
        src/temp_sensor.c \
        src/kalman.c \
        src/main.c
OBJS := $(SRCS:src/%.c=$(BUILD)/%.o)
DEPS := $(OBJS:.o=.d)

# ---- CPU ------------------------------------------------------------------
#  cortex-m4          the core inside the STM32F429
#  thumb              Cortex-M only executes the Thumb-2 instruction set
#  fpv4-sp-d16        its FPU: single precision, 16 double-word registers
#  float-abi=hard     pass floats in FPU registers (fastest; all code here
#                     is ours, so there's no soft-float library to match)
ARCH := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard

# ---- Compiler flags -------------------------------------------------------
#  -ffreestanding     no hosted C environment: main() isn't special, and no
#                     assumptions about libc beyond memcpy/memset & co.
#  -fno-math-errno    lets __builtin_sqrtf become one VSQRT instruction
#                     (sqrtf normally must set errno for negative input)
#  -fno-tree-loop-distribute-patterns
#                     stop GCC turning our copy/zero loops into calls to
#                     memcpy/memset (see libc_min.c)
#  -ffunction-sections -fdata-sections
#                     one section per function/object so the linker can
#                     drop unused ones with --gc-sections
#  -Wdouble-promotion warn on any silent float->double conversion: double
#                     math has no hardware support on this FPU
#  -g3                full debug info incl. macros; doesn't go into flash
OPT    ?= -O2
CFLAGS := $(ARCH) $(OPT) -g3 -std=c11 \
          -Wall -Wextra -Wshadow -Wdouble-promotion -Wundef \
          -ffreestanding -fno-math-errno -fno-tree-loop-distribute-patterns \
          -ffunction-sections -fdata-sections \
          -MMD -MP

# ---- Linker flags ---------------------------------------------------------
#  -nostdlib          no C library, no crt0 startup files: startup.c and
#                     libc_min.c are the entire runtime
#  -lgcc              compiler helper routines (e.g. 64-bit division) in
#                     case the compiler needs them
#  --print-memory-usage  the FLASH/RAM summary printed after linking
LDFLAGS := $(ARCH) -nostdlib -T $(LDSCRIPT) \
           -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map \
           -Wl,--print-memory-usage -Wl,--no-warn-rwx-segments
LDLIBS  := -lgcc

# ---- Flashing -------------------------------------------------------------
#  STM32CubeProgrammer: -c connect (SWD via the on-board ST-LINK),
#  -w write, -v verify, -rst reset and run afterwards.
FLASHER ?= STM32_Programmer_CLI

# =============================================================================

.PHONY: all flash test size disasm clean

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).bin $(BUILD)/$(TARGET).hex

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	$(CC) $(LDFLAGS) $(OBJS) $(LDLIBS) -o $@

# Raw image starting at 0x08000000: for drag-and-drop onto the NOD_F429ZI
# USB drive the Nucleo exposes, or `st-flash write kalman.bin 0x08000000`.
$(BUILD)/%.bin: $(BUILD)/%.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/%.hex: $(BUILD)/%.elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD):
	mkdir -p $@

flash: $(BUILD)/$(TARGET).elf
	$(FLASHER) -c port=SWD -w $< -v -rst

size: $(BUILD)/$(TARGET).elf
	$(SIZE) -A -x $<

disasm: $(BUILD)/$(TARGET).elf
	$(OBJDUMP) -d -S -C $< > $(BUILD)/$(TARGET).lst
	@echo "wrote $(BUILD)/$(TARGET).lst"

# The filter is plain C, so it can be tested on the PC: fast feedback, and
# the same source file that runs on the board.
$(BUILD)/test_kalman: test/test_kalman.c src/kalman.c src/kalman.h | $(BUILD)
	$(HOSTCC) -std=c11 -O2 -Wall -Wextra -Wdouble-promotion -fno-math-errno \
	    -Isrc test/test_kalman.c src/kalman.c -lm -o $@

test: $(BUILD)/test_kalman
	./$(BUILD)/test_kalman

clean:
	rm -rf $(BUILD)

-include $(DEPS)
