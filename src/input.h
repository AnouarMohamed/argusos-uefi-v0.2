#ifndef ARGUS_INPUT_H
#define ARGUS_INPUT_H

#include "acpi.h"

void input_init(const acpi_info_t *acpi);
int input_getc_nonblocking(void);
int input_has_serial(void);
int input_has_keyboard(void);
int input_keyboard_uses_irq(void);
uint64_t input_dropped_keys(void);

#endif
