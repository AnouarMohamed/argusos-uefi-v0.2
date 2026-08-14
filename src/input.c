#include "input.h"
#include "ps2.h"
#include "serial.h"

void input_init(const acpi_info_t *acpi) { (void)ps2_keyboard_init(acpi); }

int input_getc_nonblocking(void) {
    int value = serial_getc_nonblocking();
    if (value >= 0) return value;
    return ps2_getc_nonblocking();
}

int input_has_serial(void) { return serial_available(); }
int input_has_keyboard(void) { return ps2_keyboard_available(); }
int input_keyboard_uses_irq(void) { return ps2_keyboard_irq_online(); }
uint64_t input_dropped_keys(void) { return ps2_dropped_input(); }
