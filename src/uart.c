/**
 * @file    uart.c
 * @brief   USART3 <-> ST-LINK virtual COM port.
 *
 * On the Nucleo-144 boards, the ST-LINK/V2-1 debugger chip doubles as a
 * USB-to-serial adapter wired to USART3 on PD8 (MCU TX) and PD9 (MCU RX)
 * (UM1974, "USART communication"). So no extra hardware is needed -- the
 * same USB cable powers the board, flashes it and carries the serial data.
 *
 * Everything is polled (no interrupts, no DMA). At 10 lines/s of ~60
 * characters we use about 5 % of 115200 baud, so blocking writes take
 * at most a few milliseconds per sample.
 */
#include "uart.h"
#include "board.h"
#include "stm32f429.h"

#define UART_TX_PIN 8u   /* PD8 */
#define UART_RX_PIN 9u   /* PD9 */
#define UART_AF     7u   /* AF7 = USART1..3 (DS9405 table 12, "Alternate function mapping") */

void uart_init(uint32_t baud)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART3EN;
    (void)RCC->APB1ENR;  /* let the clock enable take effect */

    /* Route PD8/PD9 to the USART: alternate-function mode, AF7. Pins 8..15
     * use the high AF register AFR[1], 4 bits per pin. */
    const uint32_t pins[] = { UART_TX_PIN, UART_RX_PIN };
    for (unsigned i = 0; i < 2; i++) {
        const uint32_t p = pins[i];
        GPIOD->AFR[1] = (GPIOD->AFR[1] & ~(0xFUL << ((p - 8u) * 4u)))
                      | (UART_AF << ((p - 8u) * 4u));
        GPIOD->MODER  = (GPIOD->MODER & ~(3UL << (p * 2u)))
                      | (GPIO_MODE_AF << (p * 2u));
    }
    /* Pull-up on RX so a floating line reads as idle (high). */
    GPIOD->PUPDR = (GPIOD->PUPDR & ~(3UL << (UART_RX_PIN * 2u)))
                 | (GPIO_PULL_UP << (UART_RX_PIN * 2u));

    /* Baud rate (RM0090 30.6.3). With 16x oversampling,
     *     baud = f_PCLK / (16 * USARTDIV)
     * and BRR holds USARTDIV as 12.4 fixed point, i.e. BRR = 16 * USARTDIV
     * = f_PCLK / baud. Adding baud/2 rounds to nearest.
     * 16 MHz / 115200 = 138.89 -> 139 -> actual 115108 baud, -0.08 % error
     * (anything under ~2 % is fine for 8N1). */
    USART3->BRR = (PCLK1_HZ + baud / 2u) / baud;

    /* Reset defaults already give 8 data bits, no parity, 1 stop bit. */
    USART3->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

void uart_putc(char c)
{
    while ((USART3->SR & USART_SR_TXE) == 0u) {
        /* wait until the previous byte has moved to the shift register */
    }
    USART3->DR = (uint8_t)c;
}

void uart_puts(const char *s)
{
    while (*s != '\0') {
        if (*s == '\n') {
            uart_putc('\r');
        }
        uart_putc(*s++);
    }
}

void uart_put_u32(uint32_t v)
{
    char buf[10];  /* 4294967295 has 10 digits */
    unsigned n = 0;
    do {
        buf[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v != 0u);
    while (n != 0u) {          /* digits were produced least-significant first */
        uart_putc(buf[--n]);
    }
}

void uart_put_i32(int32_t v)
{
    if (v < 0) {
        uart_putc('-');
        /* Negate in unsigned arithmetic so INT32_MIN doesn't overflow. */
        uart_put_u32(0u - (uint32_t)v);
    } else {
        uart_put_u32((uint32_t)v);
    }
}

/*
 * Fixed-point float printing without printf.
 *
 * newlib's printf("%f") would add ~20 KB and needs double-precision
 * soft-float routines (the M4 FPU is single precision only). Our values are
 * small (temperatures, variances, gains), so a simple method works:
 *   1. handle the sign,
 *   2. scale by 10^decimals and round to an integer,
 *   3. print integer part, '.', then the zero-padded fractional part.
 * Doing the rounding on the *scaled* integer means 0.9996 with 3 decimals
 * correctly prints "1.000" rather than "0.1000".
 */
void uart_put_float(float v, unsigned decimals)
{
    static const uint32_t pow10[] = {
        1u, 10u, 100u, 1000u, 10000u, 100000u,
        1000000u, 10000000u, 100000000u, 1000000000u
    };
    if (decimals > 9u) {
        decimals = 9u;
    }
    if (v != v) {              /* NaN is the only value not equal to itself */
        uart_puts("nan");
        return;
    }
    if (v < 0.0f) {
        uart_putc('-');
        v = -v;
    }
    const float scaled = v * (float)pow10[decimals] + 0.5f;
    if (scaled >= 4294967295.0f) { /* would overflow uint32 */
        uart_puts("ovf");
        return;
    }
    const uint32_t fixed = (uint32_t)scaled;
    uart_put_u32(fixed / pow10[decimals]);
    if (decimals == 0u) {
        return;
    }
    uart_putc('.');
    uint32_t frac = fixed % pow10[decimals];
    for (uint32_t div = pow10[decimals - 1u]; div != 0u; div /= 10u) {
        uart_putc((char)('0' + frac / div));
        frac %= div;
    }
}

bool uart_getc_nonblocking(char *out)
{
    const uint32_t sr = USART3->SR;
    if ((sr & (USART_SR_RXNE | USART_SR_ORE)) == 0u) {
        return false;
    }
    /* Reading DR clears RXNE -- and, after SR was read, also clears an
     * overrun error (ORE). If ORE is left set, reception stalls. */
    const char c = (char)(USART3->DR & 0xFFu);
    if ((sr & USART_SR_RXNE) == 0u) {
        return false;  /* overrun only: byte lost, error now cleared */
    }
    *out = c;
    return true;
}
