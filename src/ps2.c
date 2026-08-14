#include "ps2.h"
#include "apic.h"
#include "arch.h"
#include <stdint.h>

#define PS2_DATA_PORT 0x60u
#define PS2_STATUS_PORT 0x64u
#define PS2_QUEUE_CAPACITY 64u

_Static_assert((PS2_QUEUE_CAPACITY & (PS2_QUEUE_CAPACITY - 1u)) == 0,
               "PS/2 queue capacity must be a power of two");

extern uint8_t cpu_in8(uint16_t port);
extern void cpu_out8(uint16_t port, uint8_t value);

static int keyboard_available;
static int keyboard_irq_online;
static int left_shift;
static int right_shift;
static int caps_lock;
static int extended_prefix;
static uint8_t input_queue[PS2_QUEUE_CAPACITY];
static uint8_t queue_head;
static uint8_t queue_tail;
static uint64_t dropped_input;

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

static int decode_scan_code(uint8_t scan_code) {
    if (scan_code == 0xE0u) { extended_prefix = 1; return -1; }
    if (extended_prefix) { extended_prefix = 0; return -1; }

    int released = (scan_code & 0x80u) != 0;
    scan_code &= 0x7Fu;
    if (scan_code == 0x2Au) { left_shift = !released; return -1; }
    if (scan_code == 0x36u) { right_shift = !released; return -1; }
    if (released) return -1;
    if (scan_code == 0x3Au) { caps_lock = !caps_lock; return -1; }
    if (scan_code == 0x1Cu) return '\n';
    if (scan_code == 0x0Eu) return '\b';
    if (scan_code == 0x0Fu) return '\t';
    if (scan_code == 0x39u) return ' ';

    int shifted = left_shift || right_shift;
    char letter = translate_letter(scan_code, shifted ^ caps_lock);
    if (letter) return letter;
    char symbol = translate_symbol(scan_code, shifted);
    return symbol ? symbol : -1;
}

static void queue_character(uint8_t character) {
    uint8_t head = __atomic_load_n(&queue_head, __ATOMIC_RELAXED);
    uint8_t next = (uint8_t)((head + 1u) & (PS2_QUEUE_CAPACITY - 1u));
    if (next == __atomic_load_n(&queue_tail, __ATOMIC_ACQUIRE)) {
        (void)__atomic_fetch_add(&dropped_input, 1u, __ATOMIC_RELAXED);
        return;
    }
    input_queue[head] = character;
    __atomic_store_n(&queue_head, next, __ATOMIC_RELEASE);
}

static int dequeue_character(void) {
    uint8_t tail = __atomic_load_n(&queue_tail, __ATOMIC_RELAXED);
    if (tail == __atomic_load_n(&queue_head, __ATOMIC_ACQUIRE)) return -1;
    uint8_t character = input_queue[tail];
    __atomic_store_n(
        &queue_tail,
        (uint8_t)((tail + 1u) & (PS2_QUEUE_CAPACITY - 1u)),
        __ATOMIC_RELEASE
    );
    return character;
}

static int wait_input_empty(void) {
    for (unsigned spin = 0; spin < 100000u; ++spin)
        if (!(cpu_in8(PS2_STATUS_PORT) & 2u)) return 1;
    return 0;
}

static int wait_output_full(void) {
    for (unsigned spin = 0; spin < 100000u; ++spin)
        if (cpu_in8(PS2_STATUS_PORT) & 1u) return 1;
    return 0;
}

static int enable_controller_irq(void) {
    if (!wait_input_empty()) return 0;
    cpu_out8(PS2_STATUS_PORT, 0x20u);
    if (!wait_output_full()) return 0;
    uint8_t configuration = cpu_in8(PS2_DATA_PORT);
    configuration |= 1u | (1u << 6);
    configuration &= (uint8_t)~(1u << 4);

    if (!wait_input_empty()) return 0;
    cpu_out8(PS2_STATUS_PORT, 0x60u);
    if (!wait_input_empty()) return 0;
    cpu_out8(PS2_DATA_PORT, configuration);
    if (!wait_input_empty()) return 0;
    cpu_out8(PS2_STATUS_PORT, 0xAEu);
    return 1;
}

static void keyboard_interrupt(interrupt_frame_t *frame) {
    (void)frame;
    while (cpu_in8(PS2_STATUS_PORT) & 1u) {
        uint8_t status = cpu_in8(PS2_STATUS_PORT);
        uint8_t scan_code = cpu_in8(PS2_DATA_PORT);
        if (status & 0x20u) continue;
        int character = decode_scan_code(scan_code);
        if (character >= 0) queue_character((uint8_t)character);
    }
    apic_eoi();
}

int ps2_keyboard_init(const acpi_info_t *acpi) {
    keyboard_available = 0;
    keyboard_irq_online = 0;
    if (cpu_in8(PS2_STATUS_PORT) == 0xFFu) return 0;

    for (unsigned i = 0; i < 32 && (cpu_in8(PS2_STATUS_PORT) & 1u); ++i)
        (void)cpu_in8(PS2_DATA_PORT);
    left_shift = 0;
    right_shift = 0;
    caps_lock = 0;
    extended_prefix = 0;
    __atomic_store_n(&queue_head, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&queue_tail, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&dropped_input, 0, __ATOMIC_RELAXED);
    keyboard_available = 1;

    if (acpi && interrupt_register(ARGUS_PS2_KEYBOARD_VECTOR, keyboard_interrupt)) {
        if (enable_controller_irq() &&
            apic_route_gsi(acpi, acpi->keyboard_gsi, acpi->keyboard_flags,
                           ARGUS_PS2_KEYBOARD_VECTOR)) {
            keyboard_irq_online = 1;
        } else {
            (void)interrupt_unregister(ARGUS_PS2_KEYBOARD_VECTOR, keyboard_interrupt);
        }
    }
    return 1;
}

int ps2_keyboard_available(void) { return keyboard_available; }
int ps2_keyboard_irq_online(void) { return keyboard_irq_online; }
uint64_t ps2_dropped_input(void) {
    return __atomic_load_n(&dropped_input, __ATOMIC_RELAXED);
}

int ps2_getc_nonblocking(void) {
    if (!keyboard_available) return -1;
    if (keyboard_irq_online) return dequeue_character();

    while (cpu_in8(PS2_STATUS_PORT) & 1u) {
        uint8_t status = cpu_in8(PS2_STATUS_PORT);
        uint8_t scan_code = cpu_in8(PS2_DATA_PORT);
        if (status & 0x20u) continue; /* Ignore bytes from a second PS/2 port. */
        int character = decode_scan_code(scan_code);
        if (character >= 0) return character;
    }
    return -1;
}
