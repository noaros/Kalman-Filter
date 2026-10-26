/**
 * @file    board.c
 * @brief   LED and push-button GPIO setup for the NUCLEO-F429ZI.
 */
#include "board.h"
#include "stm32f429.h"

void board_flash_init(void)
{
    /* The ART accelerator hides flash latency at high clock speeds. At
     * 16 MHz flash runs with zero wait states anyway (RM0090 table 11), so
     * enabling it makes little difference here, but it costs nothing and
     * is the right habit: you must set LATENCY *before* raising SYSCLK. */
    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY_Msk)   /* 0 wait states */
               | FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;
}

/* Set the 2-bit field for `pin` in a MODER/PUPDR/OSPEEDR-style register. */
static void set_2bit_field(volatile uint32_t *reg, uint32_t pin, uint32_t value)
{
    *reg = (*reg & ~(3UL << (pin * 2u))) | (value << (pin * 2u));
}

void board_init(void)
{
    /* Peripheral clocks are off after reset; GPIOB has the LEDs, GPIOC
     * the button. */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;
    /* Errata sheet ES0206, "Delay after an RCC peripheral clock enabling":
     * wait a couple of bus cycles after enabling a clock before touching
     * that peripheral. Reading the enable register back does exactly that. */
    (void)RCC->AHB1ENR;

    /* LEDs: push-pull outputs. Turn them off (BSRR upper half = reset)
     * *before* switching to output mode so they never glitch on. */
    const uint32_t leds[] = { LED_GREEN_PIN, LED_BLUE_PIN, LED_RED_PIN };
    for (unsigned i = 0; i < sizeof leds / sizeof leds[0]; i++) {
        GPIOB->BSRR = 1UL << (leds[i] + 16u);
        GPIOB->OTYPER &= ~(1UL << leds[i]);
        set_2bit_field(&GPIOB->MODER, leds[i], GPIO_MODE_OUTPUT);
    }

    /* Button: input. The board already has an external pull-down (and an
     * RC filter), so the internal pull-down is redundant but harmless. */
    set_2bit_field(&GPIOC->MODER, BUTTON_PIN, GPIO_MODE_INPUT);
    set_2bit_field(&GPIOC->PUPDR, BUTTON_PIN, GPIO_PULL_DOWN);
}

/* BSRR lets us set or clear single pins atomically (no read-modify-write
 * of ODR), so an interrupt can never corrupt another pin's state. */
void led_on(led_t led)  { GPIOB->BSRR = 1UL << (uint32_t)led; }
void led_off(led_t led) { GPIOB->BSRR = 1UL << ((uint32_t)led + 16u); }

void led_set(led_t led, bool on)
{
    if (on) {
        led_on(led);
    } else {
        led_off(led);
    }
}

void led_toggle(led_t led)
{
    led_set(led, (GPIOB->ODR & (1UL << (uint32_t)led)) == 0u);
}

bool button_is_pressed(void)
{
    return (GPIOC->IDR & (1UL << BUTTON_PIN)) != 0u;
}
