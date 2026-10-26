# 2. Hardware walkthrough

This chapter goes through every peripheral the firmware touches, which
registers it writes and why. Section numbers refer to these ST documents,
all free from st.com:

| Doc | Title | Used for |
|---|---|---|
| **RM0090** | STM32F405/415/407/417/427/437/429/439 reference manual | Register descriptions |
| **DS9405** | STM32F427xx/429xx datasheet | Pinout, alternate functions, electrical specs, calibration addresses |
| **UM1974** | STM32 Nucleo-144 boards user manual | Board wiring (LEDs, button, VCP) |
| **PM0214** | STM32 Cortex-M4 programming manual | SysTick, FPU, SCB |
| **ES0206** | STM32F427/437/429/439 errata sheet | Known silicon issues |

---

## 2.1 Board resources used

```
             NUCLEO-F429ZI
 ┌──────────────────────────────────────────┐
 │  ST-LINK/V2-1 ──USB── host PC            │
 │     │  SWD  (flashing/debug)             │
 │     │  VCP  USART3: PD8=TX → , PD9=RX ←  │
 │     ▼                                    │
 │  ┌──────────────┐                        │
 │  │ STM32F429ZI  │  PB0  → LD1 green      │
 │  │              │  PB7  → LD2 blue       │
 │  │  [temp sens] │  PB14 → LD3 red        │
 │  │  [VREFINT ]  │  PC13 ← B1 USER button │
 │  │  → ADC1      │                        │
 │  └──────────────┘                        │
 └──────────────────────────────────────────┘
```

Nothing outside the chip is measured. The sensor, the voltage reference and
the ADC are all on the die.

## 2.2 Clocks: 16 MHz HSI, no PLL

After reset the STM32F429 runs from **HSI**, an internal 16 MHz RC
oscillator, with all bus prescalers at /1. The firmware **leaves it that way**:

| Clock | Frequency | Feeds |
|---|---|---|
| SYSCLK = HCLK | 16 MHz | CPU, SysTick, AHB (GPIO) |
| PCLK1 (APB1) | 16 MHz | USART3 |
| PCLK2 (APB2) | 16 MHz | ADC1 (÷2 → ADCCLK 8 MHz) |

16 MHz is far more than this demo needs. Each sample takes one 62 µs
conversion and a few hundred FPU cycles. Staying on HSI means there is no
clock code to get wrong, and the constants in `board.h` are easy to check.

HSI is factory-trimmed to ±1 % at 25 °C, which is good enough for UART.
Its temperature drift doesn't matter here.

**If you want to run at 180 MHz**, you'd need to, in this order:

1. Set the flash latency to 5 wait states in `FLASH->ACR` (RM0090 table 11).
2. Enable the PWR clock and set VOS = scale 1, then enable over-drive
   (`PWR->CR` ODEN/ODSWEN, RM0090 5.1.4).
3. Set the APB prescalers (APB1 ≤ 45 MHz, APB2 ≤ 90 MHz).
4. Configure and start the PLL, using HSE bypass at 8 MHz from the
   ST-LINK's MCO, then switch SYSCLK to it.
5. Update `SYSCLK_HZ`, `PCLK1_HZ` and `PCLK2_HZ` in `board.h`, and check
   ADCCLK ≤ 36 MHz (PCLK2 90 MHz ÷ 4 = 22.5 MHz).

## 2.3 Peripheral clock gating (RCC)

**Every peripheral clock is off after reset.** Writes to a disabled
peripheral are silently ignored. Forgetting this is the classic first
bare-metal bug. Each driver therefore starts by setting its bit:

| Peripheral | Register | Bit |
|---|---|---|
| GPIOB, GPIOC, GPIOD | `RCC->AHB1ENR` | 1, 2, 3 |
| USART3 | `RCC->APB1ENR` | 18 |
| ADC1 | `RCC->APB2ENR` | 8 |

Each enable is followed by a dummy read-back, `(void)RCC->AHB1ENR;`. Errata
ES0206 ("Delay after an RCC peripheral clock enabling") says a few bus cycles
must pass before the peripheral is accessed, and the read guarantees that.

## 2.4 The temperature sensor

### What it is

A sensor built into the die whose output voltage rises about linearly with
**junction temperature**. DS9405 "Temperature sensor characteristics" gives:

| Parameter | Typical | Notes |
|---|---|---|
| Avg_Slope | 2.5 mV/°C | |
| V₂₅ | 0.76 V | Varies a lot between chips |
| Linearity | ±1 °C (max ±2 °C) | |
| t_START | ≤ 10 µs | Start-up after enabling |
| t_S_temp | ≥ 10 µs | Minimum ADC sampling time |

RM0090 13.10 warns that the offset varies **by up to 45 °C between chips**.
Without calibration, the sensor is only good for *changes* in temperature.

### Factory calibration

During production test, ST measures every chip at two temperatures and
writes the raw ADC codes into system memory, which can't be erased
(DS9405 "Temperature sensor calibration values" and "Reference voltage"):

| Name | Address | Conditions | This board |
|---|---|---|---|
| `TS_CAL1` | `0x1FFF7A2C` | 30 °C, VDDA = 3.3 V | 927 |
| `TS_CAL2` | `0x1FFF7A2E` | 110 °C, VDDA = 3.3 V | 1195 |
| `VREFINT_CAL` | `0x1FFF7A2A` | 30 °C, VDDA = 3.3 V | 1491 |

A straight line through the two temperature points gives:

$$
T = 30\,^\circ\mathrm{C} + (\text{raw}_{3.3} - \text{TS\_CAL1})
\cdot \frac{110 - 30}{\text{TS\_CAL2} - \text{TS\_CAL1}}
$$

On this board that is 80 / 268 = **0.2985 °C per ADC code**. This sets the
resolution of a single reading.

The firmware reads all three values at start-up and sanity-checks them:
CAL2 must be greater than CAL1, and none may be blank (0xFFFF or 0). If the
check fails it falls back to the datasheet typicals and prints a warning.

### VDDA compensation using VREFINT

The ADC measures **relative to VREF+**, which is tied to VDDA on the Nucleo.
The calibration codes were recorded at exactly 3.3 V. This board's VDDA
measures **3.279 V**, 0.6 % low, which makes every code 0.6 % *high*. At
about 940 codes that's 6 codes, or **≈ 1.7 °C of error**.

VREFINT is an internal bandgap reference of about 1.21 V. It sits on ADC
channel 17 and is enabled by the same `TSVREFE` bit as the sensor. Its code
at 3.3 V is also factory-stored, so:

$$
V_{DDA} = 3.3\,\mathrm{V}\cdot\frac{\text{VREFINT\_CAL}}{\text{VREFINT}_\text{measured}},
\qquad
\text{raw}_{3.3} = \text{raw}\cdot\frac{\text{VREFINT\_CAL}}{\text{VREFINT}_\text{measured}}
$$

VREFINT is measured **once at start-up as the average of 64 conversions**.
Measuring it alongside every sample would add VREFINT's own noise to every
reading. On the Nucleo, VDDA comes from a regulator and doesn't move, so a
one-off correction is the better choice.

The whole conversion is folded into one multiply-add,
`T = s_gain * raw + s_offset`, both computed at init.

### Die temperature is not air temperature

The sensor measures the **silicon**, which sits a few degrees above ambient
because of the chip's own power draw. On the development board it read
about 31.6 °C. Load, the regulator and the ST-LINK chip nearby all affect
it. Expect it to read high compared with a room thermometer. For a filtering
demo this is fine: what matters is a slow, real signal under fast noise.

## 2.5 ADC configuration

ADC1, single conversion, software trigger, polled, 12-bit, right-aligned.

| Setting | Register / field | Value | Why |
|---|---|---|---|
| ADC prescaler | `ADC_COMMON->CCR` ADCPRE | 00 (÷2) | ADCCLK = 8 MHz (max 36 MHz) |
| Sensor + VREFINT | `CCR` TSVREFE (bit 23) | 1 | Power them up |
| VBAT channel | `CCR` VBATE (bit 22) | 0 | **On F42x/43x, VBAT and the sensor share channel 18**, and VBAT wins if both are enabled |
| Resolution | `CR1` RES | 00 | 12 bits |
| Mode | `CR2` CONT, ALIGN | 0, 0 | Single conversion, right-aligned |
| Sequence length | `SQR1` L | 0 | One conversion |
| Sample time ch17, ch18 | `SMPR1` SMP17, SMP18 | 111 (480 cycles) | 60 µs ≫ 10 µs minimum |
| Channel select | `SQR3` SQ1 | 17 or 18 | Written before each conversion |

Conversion sequence (`adc_convert()`):

```c
ADC1->SQR3 = channel;              // which channel
ADC1->CR2 |= ADC_CR2_SWSTART;      // go
while (!(ADC1->SR & ADC_SR_EOC));  // (480 + 12) / 8 MHz = 61.5 us
return ADC1->DR;                   // reading DR clears EOC
```

After `ADON` the ADC needs t_STAB (a few µs), and the sensor needs t_START
(≤ 10 µs). A 1 ms delay covers both. The first conversion after enabling
or switching channel is thrown away, because the sampling capacitor may
still hold charge from the previous channel.

**Why the long sample time?** The sensor has a high output impedance, so the
ADC's sampling capacitor needs time to charge through it. ST requires at
least 10 µs. Using the longest option (480 cycles) also averages a little
noise for free.

**Why no oversampling?** Averaging, say, 16 conversions per sample would
make the raw readings much cleaner. That would defeat the purpose of the
demo, which is to let the Kalman filter remove the noise. You could add it
and then lower R to match.

### Noise on this board

On the development board, 100 readings at rest gave σ = 0.255 °C (R = 0.065 °C²),
with readings spread over about ±1 code. Note that **the noise is about the
same size as the quantisation step** (0.30 °C). Pure quantisation of a
steady input gives a variance of LSB²/12 ≈ 0.0074 °C², about 9× less than
measured. So real analogue noise is present, which is actually helpful: it
dithers the input, and the filter can resolve temperature changes *finer
than one ADC code* (look at `est_c`).

## 2.6 UART: the ST-LINK virtual COM port

The ST-LINK/V2-1 on the board is also a USB-to-serial bridge, wired to
**USART3** on **PD8 (TX)** and **PD9 (RX)** (UM1974, "USART
communication"). On Linux it shows up as `/dev/ttyACM0`.

| Step | Register | Value |
|---|---|---|
| Pin mode | `GPIOD->MODER` pins 8, 9 | 10 (alternate function) |
| Alternate function | `GPIOD->AFR[1]` pins 8, 9 | **AF7** (USART1..3; DS9405 alternate function table) |
| RX pull-up | `GPIOD->PUPDR` pin 9 | 01, so a floating line reads as idle |
| Baud rate | `USART3->BRR` | `(16 MHz + 57600) / 115200 = 139` |
| Enable | `USART3->CR1` | UE, TE, RE |

**Baud-rate maths** (RM0090 30.6.3). With 16× oversampling,
baud = f_PCLK / (16 · USARTDIV), and BRR holds USARTDIV as a 12.4
fixed-point value. That means **BRR = f_PCLK / baud** exactly, so a
plain rounded integer division is enough. 16 MHz / 139 = 115 108 baud,
an error of −0.08 %. UART tolerates about ±2 %.

**Transmit** busy-waits on `SR.TXE`, then writes `DR`. **Receive** checks
`SR.RXNE`/`SR.ORE`. Reading `DR` after `SR` clears both. An overrun flag
that is never cleared would block reception for good, which is why it gets
handled even though bytes are rarely lost here.

**No printf.** `uart_put_float()` scales by 10^decimals, rounds to an
integer and prints the integer and fractional parts. newlib's `printf("%f")`
would need double-precision soft-float, because the M4 FPU is single
precision only, and would add around 20 KB.

## 2.7 GPIO: LEDs and button

LEDs are push-pull outputs on GPIOB. They are switched with
**`BSRR`** (bit set/reset register): writing `1 << n` sets pin n, and
`1 << (n+16)` clears it. Each write is one atomic store, unlike a
read-modify-write of `ODR`, so an interrupt can never undo another pin's
change. Before each pin is switched to output it is cleared, so it never
flashes on during init.

The USER button on PC13 is **active high** (it reads 1 while pressed). The
board has an external pull-down and an RC filter on it. The firmware samples
it every 10 ms and acts on a released-to-pressed transition. Contact bounce
is shorter than 10 ms, so this simple debounce is enough.

## 2.8 SysTick

The 24-bit SysTick down-counter is part of the Cortex-M4 core (PM0214 4.5),
not an ST peripheral. `LOAD = 16 000 − 1` with the processor clock gives
an exception every 1 ms. The handler increments a `volatile uint32_t`.

Main-loop deadlines are compared with `(int32_t)(now − deadline) >= 0`,
which stays correct when the 32-bit millisecond counter wraps after
49.7 days. The sample deadline advances by exactly `SAMPLE_PERIOD_MS` each
time rather than being set to `now + period`, so timing jitter doesn't
build up into drift.

## 2.9 Flash accelerator

`FLASH->ACR` turns on prefetch and the instruction and data caches (the
"ART accelerator") with 0 wait states, which is valid up to 30 MHz at
3.3 V (RM0090 table 11). At 16 MHz this has little effect. It is included
because the wait states **must** be set *before* raising the clock.
