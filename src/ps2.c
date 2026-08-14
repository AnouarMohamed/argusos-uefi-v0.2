#include "ps2.h"
#include "apic.h"
#include "arch.h"
#include <stdint.h>

#define PS2_DATA_PORT 0x60u
#define PS2_STATUS_PORT 0x64u
#define PS2_KEY_QUEUE_CAPACITY 64u
#define PS2_MOUSE_QUEUE_CAPACITY 32u

_Static_assert((PS2_KEY_QUEUE_CAPACITY & (PS2_KEY_QUEUE_CAPACITY - 1u)) == 0,
               "PS/2 key queue capacity must be a power of two");
_Static_assert((PS2_MOUSE_QUEUE_CAPACITY & (PS2_MOUSE_QUEUE_CAPACITY - 1u)) == 0,
               "PS/2 mouse queue capacity must be a power of two");

extern uint8_t cpu_in8(uint16_t port);
extern void cpu_out8(uint16_t port, uint8_t value);

static int controller_available;
static int keyboard_available;
static int keyboard_irq_online;
static int mouse_available;
static int mouse_irq_online;
static int left_shift;
static int right_shift;
static int caps_lock;
static int extended_prefix;

static uint8_t key_queue[PS2_KEY_QUEUE_CAPACITY];
static uint8_t key_head;
static uint8_t key_tail;
static uint64_t dropped_keys;

static ps2_mouse_event_t mouse_queue[PS2_MOUSE_QUEUE_CAPACITY];
static uint8_t mouse_head;
static uint8_t mouse_tail;
static uint8_t mouse_packet[3];
static uint8_t mouse_packet_index;
static uint64_t mouse_packets;
static uint64_t dropped_mouse_events;

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

static void queue_key(uint8_t character) {
    uint8_t head = __atomic_load_n(&key_head, __ATOMIC_RELAXED);
    uint8_t next = (uint8_t)((head + 1u) & (PS2_KEY_QUEUE_CAPACITY - 1u));
    if (next == __atomic_load_n(&key_tail, __ATOMIC_ACQUIRE)) {
        (void)__atomic_fetch_add(&dropped_keys, 1u, __ATOMIC_RELAXED);
        return;
    }
    key_queue[head] = character;
    __atomic_store_n(&key_head, next, __ATOMIC_RELEASE);
}

static int dequeue_key(void) {
    uint8_t tail = __atomic_load_n(&key_tail, __ATOMIC_RELAXED);
    if (tail == __atomic_load_n(&key_head, __ATOMIC_ACQUIRE)) return -1;
    uint8_t character = key_queue[tail];
    __atomic_store_n(
        &key_tail,
        (uint8_t)((tail + 1u) & (PS2_KEY_QUEUE_CAPACITY - 1u)),
        __ATOMIC_RELEASE
    );
    return character;
}

static void queue_mouse_event(ps2_mouse_event_t event) {
    uint8_t head = __atomic_load_n(&mouse_head, __ATOMIC_RELAXED);
    uint8_t next = (uint8_t)((head + 1u) & (PS2_MOUSE_QUEUE_CAPACITY - 1u));
    if (next == __atomic_load_n(&mouse_tail, __ATOMIC_ACQUIRE)) {
        (void)__atomic_fetch_add(&dropped_mouse_events, 1u, __ATOMIC_RELAXED);
        return;
    }
    mouse_queue[head] = event;
    __atomic_store_n(&mouse_head, next, __ATOMIC_RELEASE);
}

static int dequeue_mouse_event(ps2_mouse_event_t *event) {
    uint8_t tail = __atomic_load_n(&mouse_tail, __ATOMIC_RELAXED);
    if (tail == __atomic_load_n(&mouse_head, __ATOMIC_ACQUIRE)) return 0;
    *event = mouse_queue[tail];
    __atomic_store_n(
        &mouse_tail,
        (uint8_t)((tail + 1u) & (PS2_MOUSE_QUEUE_CAPACITY - 1u)),
        __ATOMIC_RELEASE
    );
    return 1;
}

static void decode_mouse_byte(uint8_t value) {
    if (!mouse_packet_index && !(value & 0x08u)) return;
    mouse_packet[mouse_packet_index++] = value;
    if (mouse_packet_index < 3u) return;
    mouse_packet_index = 0;
    (void)__atomic_fetch_add(&mouse_packets, 1u, __ATOMIC_RELAXED);

    if (mouse_packet[0] & 0xC0u) return;
    int16_t dx = mouse_packet[1];
    int16_t raw_dy = mouse_packet[2];
    if (mouse_packet[0] & 0x10u) dx -= 256;
    if (mouse_packet[0] & 0x20u) raw_dy -= 256;

    ps2_mouse_event_t event = {
        dx,
        (int16_t)-raw_dy,
        (uint8_t)(mouse_packet[0] & 0x07u)
    };
    queue_mouse_event(event);
}

static void route_data_byte(uint8_t status, uint8_t value) {
    if (status & 0x20u) {
        if (mouse_available) decode_mouse_byte(value);
        return;
    }
    if (!keyboard_available) return;
    int character = decode_scan_code(value);
    if (character >= 0) queue_key((uint8_t)character);
}

static void drain_controller(void) {
    for (unsigned count = 0; count < 64u; ++count) {
        uint8_t status = cpu_in8(PS2_STATUS_PORT);
        if (!(status & 1u)) return;
        route_data_byte(status, cpu_in8(PS2_DATA_PORT));
    }
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

static int controller_command(uint8_t command) {
    if (!wait_input_empty()) return 0;
    cpu_out8(PS2_STATUS_PORT, command);
    return 1;
}

static int controller_read_byte(uint8_t *value) {
    if (!wait_output_full()) return 0;
    *value = cpu_in8(PS2_DATA_PORT);
    return 1;
}

static int controller_read_config(uint8_t *configuration) {
    return controller_command(0x20u) && controller_read_byte(configuration);
}

static int controller_write_config(uint8_t configuration) {
    if (!controller_command(0x60u) || !wait_input_empty()) return 0;
    cpu_out8(PS2_DATA_PORT, configuration);
    return 1;
}

static int configure_keyboard_port(void) {
    uint8_t configuration;
    if (!controller_command(0xAEu) || !controller_read_config(&configuration)) return 0;
    configuration |= 1u | (1u << 6);
    configuration &= (uint8_t)~(1u << 4);
    return controller_write_config(configuration);
}

static int read_mouse_response(uint8_t *response) {
    for (unsigned spin = 0; spin < 200000u; ++spin) {
        uint8_t status = cpu_in8(PS2_STATUS_PORT);
        if (!(status & 1u)) continue;
        uint8_t value = cpu_in8(PS2_DATA_PORT);
        if (status & 0x20u) {
            *response = value;
            return 1;
        }
        route_data_byte(status, value);
    }
    return 0;
}

static int send_mouse_command(uint8_t command) {
    for (unsigned attempt = 0; attempt < 3u; ++attempt) {
        if (!controller_command(0xD4u) || !wait_input_empty()) return 0;
        cpu_out8(PS2_DATA_PORT, command);
        uint8_t response;
        if (!read_mouse_response(&response)) return 0;
        if (response == 0xFAu) return 1;
        if (response != 0xFEu) return 0;
    }
    return 0;
}

static int configure_mouse_port(void) {
    uint8_t response;
    uint8_t configuration;
    if (!controller_command(0xA8u) ||
        !controller_command(0xA9u) ||
        !controller_read_byte(&response) ||
        response != 0u ||
        !controller_read_config(&configuration))
        return 0;

    configuration |= 1u << 1;
    configuration &= (uint8_t)~(1u << 5);
    if (!controller_write_config(configuration)) return 0;
    return send_mouse_command(0xF6u) && send_mouse_command(0xF4u);
}

static void ps2_interrupt(interrupt_frame_t *frame) {
    (void)frame;
    drain_controller();
    apic_eoi();
}

int ps2_keyboard_init(const acpi_info_t *acpi) {
    controller_available = 0;
    keyboard_available = 0;
    keyboard_irq_online = 0;
    if (cpu_in8(PS2_STATUS_PORT) == 0xFFu) return 0;

    for (unsigned i = 0; i < 32u && (cpu_in8(PS2_STATUS_PORT) & 1u); ++i)
        (void)cpu_in8(PS2_DATA_PORT);
    left_shift = 0;
    right_shift = 0;
    caps_lock = 0;
    extended_prefix = 0;
    __atomic_store_n(&key_head, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&key_tail, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&dropped_keys, 0, __ATOMIC_RELAXED);
    controller_available = 1;
    keyboard_available = 1;

    if (acpi && interrupt_register(ARGUS_PS2_KEYBOARD_VECTOR, ps2_interrupt)) {
        if (configure_keyboard_port() &&
            apic_route_gsi(acpi, acpi->keyboard_gsi, acpi->keyboard_flags,
                           ARGUS_PS2_KEYBOARD_VECTOR)) {
            keyboard_irq_online = 1;
        } else {
            (void)interrupt_unregister(ARGUS_PS2_KEYBOARD_VECTOR, ps2_interrupt);
        }
    }
    return 1;
}

int ps2_mouse_init(const acpi_info_t *acpi) {
    mouse_available = 0;
    mouse_irq_online = 0;
    mouse_packet_index = 0;
    __atomic_store_n(&mouse_head, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&mouse_tail, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&mouse_packets, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&dropped_mouse_events, 0, __ATOMIC_RELAXED);
    if (!controller_available || !configure_mouse_port()) return 0;
    mouse_available = 1;

    if (acpi && interrupt_register(ARGUS_PS2_MOUSE_VECTOR, ps2_interrupt)) {
        if (apic_route_gsi(acpi, acpi->mouse_gsi, acpi->mouse_flags,
                           ARGUS_PS2_MOUSE_VECTOR)) {
            mouse_irq_online = 1;
        } else {
            (void)interrupt_unregister(ARGUS_PS2_MOUSE_VECTOR, ps2_interrupt);
        }
    }
    return 1;
}

int ps2_keyboard_available(void) { return keyboard_available; }
int ps2_keyboard_irq_online(void) { return keyboard_irq_online; }
int ps2_mouse_available(void) { return mouse_available; }
int ps2_mouse_irq_online(void) { return mouse_irq_online; }

uint64_t ps2_dropped_input(void) {
    return __atomic_load_n(&dropped_keys, __ATOMIC_RELAXED);
}

uint64_t ps2_mouse_packets(void) {
    return __atomic_load_n(&mouse_packets, __ATOMIC_RELAXED);
}

uint64_t ps2_dropped_mouse_events(void) {
    return __atomic_load_n(&dropped_mouse_events, __ATOMIC_RELAXED);
}

int ps2_getc_nonblocking(void) {
    if (!keyboard_available) return -1;
    if (!keyboard_irq_online) drain_controller();
    return dequeue_key();
}

int ps2_mouse_get_event(ps2_mouse_event_t *event) {
    if (!mouse_available || !event) return 0;
    if (!mouse_irq_online) drain_controller();
    return dequeue_mouse_event(event);
}
