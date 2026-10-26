/**
 * @file    uart.h
 * @brief   Polled USART3 driver on the ST-LINK virtual COM port, plus the
 *          tiny number formatting this project needs (no printf).
 *
 * On the host the port shows up as /dev/ttyACM0 (Linux), /dev/cu.usbmodem*
 * (macOS) or COMx (Windows). Settings: 115200 baud, 8 data bits, no
 * parity, 1 stop bit (8N1), no flow control.
 */
#ifndef UART_H
#define UART_H

#include <stdbool.h>
#include <stdint.h>

void uart_init(uint32_t baud);

/** Blocking: wait for room in the transmit register, then send a byte. */
void uart_putc(char c);
/** Send a NUL-terminated string. "\n" is sent as "\r\n" for terminals. */
void uart_puts(const char *s);

void uart_put_u32(uint32_t v);
void uart_put_i32(int32_t v);
/** Print a float in fixed-point notation with `decimals` digits (0..9). */
void uart_put_float(float v, unsigned decimals);

/** Non-blocking receive: returns true and stores a byte if one arrived. */
bool uart_getc_nonblocking(char *out);

#endif /* UART_H */
