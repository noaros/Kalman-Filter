# Kalman-Filter

A bare-metal Kalman filter demo for the **NUCLEO-F429ZI** board. The STM32F429's
built-in temperature sensor gives slowly changing, noisy readings, and a scalar
Kalman filter smooths them in real time. Both the raw and the filtered values
stream over USB, so you can watch the filter work and retune it live.

"Bare metal" here means the firmware uses none of the usual support code:

- **No HAL, no CMSIS, no RTOS, no C library.** Every peripheral is set up by
  writing its registers directly, through a hand-written register map of about 270 lines
  ([`src/stm32f429.h`](src/stm32f429.h)).
- **No assembly.** The vector table and reset handler are written in C
  ([`src/startup.c`](src/startup.c)).
- **About 4.4 KB of flash** in total, including the startup code, the drivers,
  float formatting and the filter.

```
# ==== STM32F429 bare-metal Kalman filter demo ====
# SYSCLK=16 MHz (HSI), sample period=100 ms
# TS_CAL1(30C)=927 TS_CAL2(110C)=1195 VREFINT_CAL=1491
# VREFINT now=1500.6 -> VDDA=3.279 V
# Measuring sensor noise (100 samples, keep the board still)...
# mean=31.64 C  std=0.255 C  -> R=0.0649 C^2
# Q=0.001000 C^2/step  R=0.0649 C^2  -> steady-state K=0.1167, sigma_est=0.087 C, time constant ~0.9 s
t_ms,adc,meas_c,est_c,p,k,innov
0,938,31.493,31.569,0.032678,0.5038,-0.151
100,939,31.790,31.644,0.022167,0.3418,0.222
200,938,31.493,31.605,0.017070,0.2632,-0.151
...
```
*(This is real output from the board this project was developed on.)*

---

## Contents

| Path | What it is |
|---|---|
| [`src/kalman.c`](src/kalman.c), [`.h`](src/kalman.h) | The filter. Portable C with no hardware access, so the same file is unit-tested on the PC |
| [`src/main.c`](src/main.c) | Application: noise calibration, 10 Hz sampling loop, CSV output, commands |
| [`src/temp_sensor.c`](src/temp_sensor.c) | ADC1 driver for the internal sensor, with factory calibration and VDDA compensation |
| [`src/uart.c`](src/uart.c) | Polled USART3 driver for the ST-LINK virtual COM port, plus integer and float printing without `printf` |
| [`src/systick.c`](src/systick.c) | 1 ms tick and `delay_ms` |
| [`src/board.c`](src/board.c), [`.h`](src/board.h) | Clock constants, LEDs and the user button |
| [`src/startup.c`](src/startup.c) | Vector table, reset handler (FPU enable, `.data`/`.bss` setup), fault handler |
| [`src/libc_min.c`](src/libc_min.c) | The only two libc functions needed: `memcpy` and `memset` |
| [`src/stm32f429.h`](src/stm32f429.h) | Hand-written register definitions, with compile-time offset checks |
| [`linker/stm32f429zi.ld`](linker/stm32f429zi.ld) | Linker script: memory map and section placement |
| [`test/test_kalman.c`](test/test_kalman.c) | Host-side unit tests that check the filter against theory |
| [`tools/kf_plot.py`](tools/kf_plot.py) | Live plot and CSV logger; forwards key presses to the board |
| [`docs/`](docs/) | Longer explanations (see below) |

### Documentation

1. [**Kalman filter theory**](docs/01-kalman-theory.md): what the filter does,
   where the equations come from, the steady-state solution, and the
   general matrix form.
2. [**Hardware walkthrough**](docs/02-hardware.md): the temperature sensor, ADC,
   calibration, UART, clocks and LEDs, register by register.
3. [**Boot and build**](docs/03-boot-and-build.md): what happens between power-on
   and `main()`, the linker script, compiler flags and debugging.
4. [**Experiments and tuning**](docs/04-experiments.md): things to try with the
   running demo, and how to choose Q and R.

---

## What you need

- A **NUCLEO-F429ZI** board and a micro-USB cable to the ST-LINK connector
  (CN1, at the top). No other wiring is needed.
- The **Arm GNU Toolchain** (`arm-none-eabi-gcc`); version 10 or newer should work.
  The project was developed with 15.3.
- **GNU make**.
- A flashing tool. Any of these works:
  - [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html)
    (`STM32_Programmer_CLI`), which `make flash` uses by default
  - OpenOCD or stlink (`st-flash`); see [Flashing](#flashing)
  - nothing at all: copy the `.bin` file onto the USB drive the board shows up as
- Optional, for plotting: Python 3 with `pip install pyserial matplotlib`.
- Optional, for `make test`: any host C compiler (`cc`).

## Quick start

```sh
make            # build build/kalman.elf, kalman.bin and kalman.hex
make test       # run the filter's unit tests on the PC
make flash      # program the board over ST-LINK and reset it
```

Then open the serial port at **115200 8N1**:

```sh
python3 tools/kf_plot.py                 # live plot (Linux default /dev/ttyACM0)
python3 tools/kf_plot.py --port COM5     # Windows
# or any terminal:
picocom -b 115200 /dev/ttyACM0
screen /dev/ttyACM0 115200
```

After reset the firmware:

1. prints a banner with the chip's factory calibration and the measured supply
   voltage,
2. spends **1 second measuring the sensor noise** (the blue LED is on during
   this; keep the board still), then uses the measured variance as R,
3. samples at **10 Hz**, printing one CSV line per sample.

### Flashing

```sh
make flash                                         # STM32CubeProgrammer (default)
st-flash --reset write build/kalman.bin 0x08000000 # stlink tools
openocd -f board/st_nucleo_f4.cfg \
        -c "program build/kalman.elf verify reset exit"
cp build/kalman.bin /run/media/$USER/NOD_F429ZI/   # drag-and-drop (path varies)
```

On Linux you may need udev rules (installed with STM32CubeProgrammer or
stlink), or membership of the `uucp`/`dialout` group, to open the
ST-LINK and `/dev/ttyACM0` without root.

## Using the demo

### Commands

Type these in a serial terminal, or press them while the `kf_plot.py` window
has focus:

| Key | Effect |
|---|---|
| `+` / `-` | Next or previous **Q** preset: 1e-6, 1e-5, 1e-4, **1e-3**, 1e-2, 1e-1, 1 °C² per step |
| **USER button** (blue) | Same as `+`; wraps around after the last preset |
| `R` / `r` | Double or halve **R** |
| `z` | Reset the filter to the current reading with P = 100 °C², to watch it converge |
| `n` | Measure the noise again and set R from it |
| `h` / `?` | Help and current tuning |

Every time Q or R changes, the firmware prints the **steady-state gain** and
**time constant** that the new tuning will settle to, worked out from the
Riccati equation (see [theory](docs/01-kalman-theory.md#5-steady-state)).

### LEDs

| LED | Meaning |
|---|---|
| Green (LD1) | Heartbeat: toggles every 500 ms |
| Blue (LD2) | Toggles on every sample; steady on during noise calibration |
| Red (LD3) | Innovation larger than 3σ: the reading disagrees with the filter's prediction by more than the filter expects. This usually means Q is too small for how fast the temperature is changing. Solid red with the green heartbeat stopped means a fault (`Default_Handler`). |

### Output columns

| Column | Meaning |
|---|---|
| `t_ms` | Milliseconds since sampling started |
| `adc` | Raw 12-bit ADC code from the sensor |
| `meas_c` | That code converted to °C: the measurement *z* |
| `est_c` | Kalman estimate *x* |
| `p` | Variance of the estimate, *P* (°C²); `est_c ± 2√p` is a band of about 95 % |
| `k` | Kalman gain *K* used in this step (0 means ignore the reading, 1 means take it as-is) |
| `innov` | Innovation *z − x⁻*: how far the reading was from the prediction |

### Things to try

See [docs/04-experiments.md](docs/04-experiments.md) for more. In short:

- **Warm the chip** with a fingertip on the STM32 (the large square chip in the middle of the board).
  Then press `+` or `-` and compare how quickly the estimate follows.
- Set **Q = 1e-6** and warm the chip: the estimate lags badly and the red LED
  lights up, because the filter is surprised by the change.
- Set **Q = 1**: the estimate follows the noise, and the filter barely filters.
- Press **`z`**: K starts at about 1 and decays to its steady-state value
  within a few samples.

## The filter in four lines

```c
kf->p += kf->q;                    // predict: uncertainty grows
kf->k  = kf->p / (kf->p + kf->r);  // gain: how much to trust the reading
kf->x += kf->k * (z - kf->x);      // update: move toward the reading
kf->p *= 1.0f - kf->k;             //         and uncertainty shrinks
```

The full version, with comments, is in [`src/kalman.c`](src/kalman.c). The
reasons behind each line are in
[docs/01-kalman-theory.md](docs/01-kalman-theory.md).

## Make targets

| Target | Does |
|---|---|
| `make` | Build the ELF, BIN and HEX files in `build/`, and print flash/RAM usage |
| `make flash` | Program the board and reset it (`FLASHER=...` to override) |
| `make test` | Build `test/test_kalman.c` with the host compiler and run it |
| `make size` | Per-section size breakdown |
| `make disasm` | Annotated disassembly in `build/kalman.lst` |
| `make clean` | Delete `build/` |
| `make OPT=-Og` | Build for debugging (any target) |

## License

No license has been chosen yet. Add one before distributing this project.
