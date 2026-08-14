#include "kconsole.h"
#include "console.h"
#include "serial.h"

void kconsole_clear(void) {
    if (console_uses_framebuffer()) console_clear();
    serial_write("\x1B[2J\x1B[H");
}

void kconsole_putc(char c) {
    if (console_uses_framebuffer()) console_putc(c);
    serial_putc(c);
}

void kconsole_write(const char *s) {
    while (*s) kconsole_putc(*s++);
}

void kconsole_write_dec(uint64_t value) {
    char buffer[21];
    unsigned used = 0;
    if (!value) { kconsole_putc('0'); return; }
    while (value) {
        buffer[used++] = (char)('0' + value % 10u);
        value /= 10u;
    }
    while (used) kconsole_putc(buffer[--used]);
}

void kconsole_write_hex(uint64_t value) {
    static const char hex[] = "0123456789ABCDEF";
    kconsole_write("0x");
    for (int shift = 60; shift >= 0; shift -= 4)
        kconsole_putc(hex[(value >> shift) & 0xFu]);
}
