#include "input.h"
#include "ps2.h"
#include "serial.h"

void input_init(const acpi_info_t *acpi) {
    (void)ps2_keyboard_init(acpi);
    (void)ps2_mouse_init(acpi);
}

int input_getc_nonblocking(void) {
    int value = input_serial_getc_nonblocking();
    if (value >= 0) return value;
    return input_keyboard_getc_nonblocking();
}

int input_serial_getc_nonblocking(void) { return serial_getc_nonblocking(); }
int input_keyboard_getc_nonblocking(void) { return ps2_getc_nonblocking(); }

int input_pointer_event_nonblocking(input_pointer_event_t *event) {
    if (!event) return 0;
    ps2_mouse_event_t mouse_event;
    if (!ps2_mouse_get_event(&mouse_event)) return 0;
    event->dx = mouse_event.dx;
    event->dy = mouse_event.dy;
    event->buttons = mouse_event.buttons;
    return 1;
}

int input_has_serial(void) { return serial_available(); }
int input_has_keyboard(void) { return ps2_keyboard_available(); }
int input_keyboard_uses_irq(void) { return ps2_keyboard_irq_online(); }
int input_has_pointer(void) { return ps2_mouse_available(); }
int input_pointer_uses_irq(void) { return ps2_mouse_irq_online(); }
uint64_t input_dropped_keys(void) { return ps2_dropped_input(); }
uint64_t input_pointer_packets(void) { return ps2_mouse_packets(); }
uint64_t input_dropped_pointer_events(void) {
    return ps2_dropped_mouse_events();
}
