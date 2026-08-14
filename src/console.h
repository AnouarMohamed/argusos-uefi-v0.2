#ifndef ARGUS_CONSOLE_H
#define ARGUS_CONSOLE_H

#include "efi.h"
#include <stdint.h>

int console_init(EFI_SYSTEM_TABLE *st);
int console_uses_framebuffer(void);
void console_clear(void);
void console_putc(char c);
void console_write(const char *s);
void console_write16(const CHAR16 *s);
void console_set_color(unsigned index);
int console_set_region(
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint8_t foreground_r,
    uint8_t foreground_g,
    uint8_t foreground_b,
    uint8_t background_r,
    uint8_t background_g,
    uint8_t background_b
);

#endif
