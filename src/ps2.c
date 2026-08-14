#include "ps2.h"
#include <stdint.h>

#define PS2_DATA_PORT 0x60u
#define PS2_STATUS_PORT 0x64u

extern uint8_t cpu_in8(uint16_t port);

static int keyboard_available;
static int left_shift;
static int right_shift;
static int caps_lock;
static int extended_prefix;

static char translate_letter(uint8_t scan_code, int shifted) {
    static const char letters[] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        'q','w','e','r','t','y','u','i','o','p', 0, 0, 0, 0,
        'a','s','d','f','g','h','j','k','l', 0, 0, 0, 0, 0,
        'z','x','c','v','b','n','m'
    };
    if (scan_code >= sizeof(letters) || !letters[scan_code]) return 0;
    char value = letters[scan_code];
    if (shifted) value = (char)(value - 'a' + 'A');
    return value;
}

static char translate_symbol(uint8_t scan_code, int shifted) {
    static const char normal[] = "\0\0" "1234567890-=\0\0";
    static const char upper[]  = "\0\0" "!@#$%^&*()_+\0\0";
    if (scan_code >= sizeof(normal)) return 0;
    char value = shifted ? upper[scan_code] : normal[scan_code];
    if (value) return value;

    switch (scan_code) {
        case 0x1Au: return shifted ? '{' : '[';
        case 0x1Bu: return shifted ? '}' : ']';
        case 0x27u: return shifted ? ':' : ';';
        case 0x28u: return shifted ? '"' : '\'';
        case 0x29u: return shifted ? '~' : '`';
        case 0x2Bu: return shifted ? '|' : '\\';
        case 0x33u: return shifted ? '<' : ',';
        case 0x34u: return shifted ? '>' : '.';
        case 0x35u: return shifted ? '?' : '/';
        default: return 0;
    }
}

int ps2_keyboard_init(void) {
    uint8_t status = cpu_in8(PS2_STATUS_PORT);
    if (status == 0xFFu) return 0;

    for (unsigned i = 0; i < 32 && (cpu_in8(PS2_STATUS_PORT) & 1u); ++i)
        (void)cpu_in8(PS2_DATA_PORT);
    left_shift = 0;
    right_shift = 0;
    caps_lock = 0;
    extended_prefix = 0;
    keyboard_available = 1;
    return 1;
}

int ps2_keyboard_available(void) { return keyboard_available; }

int ps2_getc_nonblocking(void) {
    if (!keyboard_available) return -1;

    while (cpu_in8(PS2_STATUS_PORT) & 1u) {
        uint8_t status = cpu_in8(PS2_STATUS_PORT);
        uint8_t scan_code = cpu_in8(PS2_DATA_PORT);
        if (status & 0x20u) continue; /* Ignore bytes from a second PS/2 port. */
        if (scan_code == 0xE0u) { extended_prefix = 1; continue; }
        if (extended_prefix) { extended_prefix = 0; continue; }

        int released = (scan_code & 0x80u) != 0;
        scan_code &= 0x7Fu;
        if (scan_code == 0x2Au) { left_shift = !released; continue; }
        if (scan_code == 0x36u) { right_shift = !released; continue; }
        if (released) continue;
        if (scan_code == 0x3Au) { caps_lock = !caps_lock; continue; }
        if (scan_code == 0x1Cu) return '\n';
        if (scan_code == 0x0Eu) return '\b';
        if (scan_code == 0x0Fu) return '\t';
        if (scan_code == 0x39u) return ' ';

        int shifted = left_shift || right_shift;
        char letter = translate_letter(scan_code, shifted ^ caps_lock);
        if (letter) return letter;
        char symbol = translate_symbol(scan_code, shifted);
        if (symbol) return symbol;
    }
    return -1;
}
