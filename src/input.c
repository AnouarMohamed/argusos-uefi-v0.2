#include "input.h"
#include "ps2.h"
#include "serial.h"

void input_init(void) { (void)ps2_keyboard_init(); }

int input_getc_nonblocking(void) {
    int value = serial_getc_nonblocking();
    if (value >= 0) return value;
    return ps2_getc_nonblocking();
}

int input_has_serial(void) { return serial_available(); }
int input_has_keyboard(void) { return ps2_keyboard_available(); }
