#ifndef ARGUS_PS2_H
#define ARGUS_PS2_H

#include "acpi.h"
#include <stdint.h>

typedef struct {
    int16_t dx;
    int16_t dy;
    uint8_t buttons;
} ps2_mouse_event_t;

int ps2_keyboard_init(const acpi_info_t *acpi);
int ps2_mouse_init(const acpi_info_t *acpi);
int ps2_keyboard_available(void);
int ps2_keyboard_irq_online(void);
int ps2_mouse_available(void);
int ps2_mouse_irq_online(void);
uint64_t ps2_dropped_input(void);
uint64_t ps2_mouse_packets(void);
uint64_t ps2_dropped_mouse_events(void);
int ps2_getc_nonblocking(void);
int ps2_mouse_get_event(ps2_mouse_event_t *event);

#endif
