#ifndef ARGUS_KCONSOLE_H
#define ARGUS_KCONSOLE_H

#include <stdint.h>

void kconsole_clear(void);
void kconsole_putc(char c);
void kconsole_write(const char *s);
void kconsole_write_dec(uint64_t value);
void kconsole_write_hex(uint64_t value);

#endif
