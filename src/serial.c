#include "serial.h"

#define COM1_PORT 0x3F8u

extern void cpu_out8(uint16_t port, uint8_t value);
extern uint8_t cpu_in8(uint16_t port);

void serial_init(void) {
    cpu_out8(COM1_PORT + 1u, 0x00); /* Disable UART interrupts. */
    cpu_out8(COM1_PORT + 3u, 0x80); /* Enable divisor latch. */
    cpu_out8(COM1_PORT + 0u, 0x01); /* 115200 baud divisor. */
    cpu_out8(COM1_PORT + 1u, 0x00);
    cpu_out8(COM1_PORT + 3u, 0x03); /* 8 data bits, no parity, one stop bit. */
    cpu_out8(COM1_PORT + 2u, 0xC7); /* Enable FIFO, clear queues. */
    cpu_out8(COM1_PORT + 4u, 0x03); /* DTR and RTS. */
}

void serial_putc(char c) {
    if (c == '\n') serial_putc('\r');
    while ((cpu_in8(COM1_PORT + 5u) & 0x20u) == 0u) {}
    cpu_out8(COM1_PORT, (uint8_t)c);
}

void serial_write(const char *s) {
    while (*s) serial_putc(*s++);
}

void serial_write_hex64(uint64_t value) {
    static const char hex[] = "0123456789ABCDEF";
    serial_write("0x");
    for (int shift = 60; shift >= 0; shift -= 4)
        serial_putc(hex[(value >> shift) & 0xFu]);
}
