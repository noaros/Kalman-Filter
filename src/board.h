/**
 * @file    board.h
 * @brief   NUCLEO-F429ZI board definitions: clocks, LEDs, user button.
 *
 * Pin assignments come from UM1974 "STM32 Nucleo-144 boards user manual"
 * (MB1137), sections "LEDs", "Push-buttons" and "USART communication".
 *
 *   LD1 green  -> PB0   (active high)
 *   LD2 blue   -> PB7   (active high)
 *   LD3 red    -> PB14  (active high)
 *   B1  USER   -> PC13  (active high: reads 1 while pressed)
 *   ST-LINK virtual COM port -> USART3, PD8 = TX, PD9 = RX (see uart.c)
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

/* The demo runs on the 16 MHz internal RC oscillator (HSI), which is what
 * the chip runs on straight out of reset. No PLL, no external crystal and
 * no prescalers, so SYSCLK = HCLK = PCLK1 = PCLK2 = 16 MHz. That is plenty
 * for 10 samples per second, and it keeps the clock setup down to nothing.
 * See docs/02-hardware.md for what changes if you raise the clock. */
#define SYSCLK_HZ  16000000UL
#define PCLK1_HZ   SYSCLK_HZ   /* APB1: USART3 */
#define PCLK2_HZ   SYSCLK_HZ   /* APB2: ADC1   */

#define LED_GREEN_PIN  0u   /* GPIOB */
#define LED_BLUE_PIN   7u   /* GPIOB */
#define LED_RED_PIN    14u  /* GPIOB */
#define BUTTON_PIN     13u  /* GPIOC */

typedef enum {
    LED_GREEN = LED_GREEN_PIN,
    LED_BLUE  = LED_BLUE_PIN,
    LED_RED   = LED_RED_PIN,
} led_t;

/** Enable flash prefetch and caches (ART accelerator). */
void board_flash_init(void);

/** Configure the three LEDs as outputs (off) and the user button as input. */
void board_init(void);

void led_on(led_t led);
void led_off(led_t led);
void led_toggle(led_t led);
void led_set(led_t led, bool on);

/** Raw (undebounced) state of the blue user button: true while pressed. */
bool button_is_pressed(void);

#endif /* BOARD_H */
