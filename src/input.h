#ifndef ARGUS_INPUT_H
#define ARGUS_INPUT_H

#include "acpi.h"

typedef struct {
    int16_t dx;
    int16_t dy;
    uint8_t buttons;
} input_pointer_event_t;

void input_init(const acpi_info_t *acpi);
int input_getc_nonblocking(void);
int input_serial_getc_nonblocking(void);
int input_keyboard_getc_nonblocking(void);
int input_pointer_event_nonblocking(input_pointer_event_t *event);
int input_has_serial(void);
int input_has_keyboard(void);
int input_keyboard_uses_irq(void);
int input_has_pointer(void);
int input_pointer_uses_irq(void);
uint64_t input_dropped_keys(void);
uint64_t input_pointer_packets(void);
uint64_t input_dropped_pointer_events(void);

#endif
