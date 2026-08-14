#ifndef ARGUS_PS2_H
#define ARGUS_PS2_H

#include "acpi.h"

int ps2_keyboard_init(const acpi_info_t *acpi);
int ps2_keyboard_available(void);
int ps2_keyboard_irq_online(void);
uint64_t ps2_dropped_input(void);
int ps2_getc_nonblocking(void);

#endif
