#ifndef ARGUS_SERIAL_H
#define ARGUS_SERIAL_H

#include <stdint.h>

void serial_init(void);
int serial_available(void);
int serial_getc_nonblocking(void);
void serial_putc(char c);
void serial_write(const char *s);
void serial_write_hex64(uint64_t value);

#endif
