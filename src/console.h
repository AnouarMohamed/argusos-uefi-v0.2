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

#endif
