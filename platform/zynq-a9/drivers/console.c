/* SPDX-License-Identifier: MIT */
/*
 * Console I/O driver for Zynq-A9 QEMU (Cadence UART).
 * Implements claw_console_init/read/write for the shell.
 */

#include "drivers/serial/espressif/console.h"

#include "FreeRTOS.h"
#include "task.h"

#define UART_BASE  0xE0000000U

/* Cadence UART register offsets */
#define UART_CR    (*(volatile unsigned int *)(UART_BASE + 0x00))
#define UART_SR    (*(volatile unsigned int *)(UART_BASE + 0x2C))
#define UART_FIFO  (*(volatile unsigned int *)(UART_BASE + 0x30))

#define UART_CR_TX_EN   (1U << 4)
#define UART_CR_RX_EN   (1U << 2)
#define UART_SR_TXFULL  (1U << 4)
#define UART_SR_RXEMPTY (1U << 1)

void claw_console_init(void)
{
    UART_CR = UART_CR_TX_EN | UART_CR_RX_EN;
}

int claw_console_read(void *buf, uint32_t len, uint32_t timeout_ms)
{
    uint8_t *p = (uint8_t *)buf;
    TickType_t start = xTaskGetTickCount();
    TickType_t ticks = (timeout_ms == UINT32_MAX)
                       ? portMAX_DELAY
                       : pdMS_TO_TICKS(timeout_ms);
    uint32_t got = 0;

    while (got < len) {
        if (!(UART_SR & UART_SR_RXEMPTY)) {
            p[got++] = (uint8_t)(UART_FIFO & 0xFF);
        } else {
            if (got > 0) {
                break;
            }
            TickType_t elapsed = xTaskGetTickCount() - start;
            if (ticks != portMAX_DELAY && elapsed >= ticks) {
                break;
            }
            taskYIELD();
        }
    }

    return (int)got;
}

int claw_console_write(const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;

    for (size_t i = 0; i < len; i++) {
        while (UART_SR & UART_SR_TXFULL) {
            /* wait */
        }
        UART_FIFO = (unsigned int)p[i];
    }

    return (int)len;
}
