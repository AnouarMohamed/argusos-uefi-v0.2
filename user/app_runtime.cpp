#include "app_runtime.h"
#include "../src/user_abi.h"

extern "C" void *memset(void *destination, int value, __SIZE_TYPE__ length) {
    auto *bytes = static_cast<volatile unsigned char *>(destination);
    for (__SIZE_TYPE__ index = 0; index < length; ++index)
        bytes[index] = static_cast<unsigned char>(value);
    return destination;
}

namespace {

struct Glyph {
    char character;
    uint8_t rows[7];
};

constexpr Glyph kGlyphs[] = {
    {'0',{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}}, {'1',{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
    {'2',{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}}, {'3',{0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}},
    {'4',{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}}, {'5',{0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E}},
    {'6',{0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E}}, {'7',{0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
    {'8',{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}}, {'9',{0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E}},
    {'A',{0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}}, {'B',{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
    {'C',{0x0F,0x10,0x10,0x10,0x10,0x10,0x0F}}, {'D',{0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}},
    {'E',{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}}, {'F',{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
    {'G',{0x0F,0x10,0x10,0x17,0x11,0x11,0x0F}}, {'H',{0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
    {'I',{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}}, {'J',{0x01,0x01,0x01,0x01,0x11,0x11,0x0E}},
    {'K',{0x11,0x12,0x14,0x18,0x14,0x12,0x11}}, {'L',{0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
    {'M',{0x11,0x1B,0x15,0x15,0x11,0x11,0x11}}, {'N',{0x11,0x19,0x15,0x13,0x11,0x11,0x11}},
    {'O',{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}}, {'P',{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
    {'Q',{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}}, {'R',{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
    {'S',{0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}}, {'T',{0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
    {'U',{0x11,0x11,0x11,0x11,0x11,0x11,0x0E}}, {'V',{0x11,0x11,0x11,0x11,0x11,0x0A,0x04}},
    {'W',{0x11,0x11,0x11,0x15,0x15,0x15,0x0A}}, {'X',{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
    {'Y',{0x11,0x11,0x0A,0x04,0x04,0x04,0x04}}, {'Z',{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
    {'.',{0,0,0,0,0,0x0C,0x0C}}, {',',{0,0,0,0,0x0C,0x04,0x08}},
    {':',{0,0x0C,0x0C,0,0x0C,0x0C,0}}, {';',{0,0x0C,0x0C,0,0x0C,0x04,0x08}},
    {'-',{0,0,0,0x1F,0,0,0}}, {'_',{0,0,0,0,0,0,0x1F}},
    {'+',{0,0x04,0x04,0x1F,0x04,0x04,0}}, {'=',{0,0,0x1F,0,0x1F,0,0}},
    {'/',{0x01,0x02,0x02,0x04,0x08,0x08,0x10}}, {'\\',{0x10,0x08,0x08,0x04,0x02,0x02,0x01}},
    {'(',{0x02,0x04,0x08,0x08,0x08,0x04,0x02}}, {')',{0x08,0x04,0x02,0x02,0x02,0x04,0x08}},
    {'[',{0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}}, {']',{0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}},
    {'<',{0x02,0x04,0x08,0x10,0x08,0x04,0x02}}, {'>',{0x08,0x04,0x02,0x01,0x02,0x04,0x08}},
    {'!',{0x04,0x04,0x04,0x04,0x04,0,0x04}}, {'?',{0x0E,0x11,0x01,0x02,0x04,0,0x04}},
    {'#',{0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A}}, {'*',{0,0x0A,0x04,0x1F,0x04,0x0A,0}},
    {'@',{0x0E,0x11,0x17,0x15,0x17,0x10,0x0E}}, {'%',{0x19,0x19,0x02,0x04,0x08,0x13,0x13}},
    {'&',{0x0C,0x12,0x14,0x08,0x15,0x12,0x0D}}, {'|',{0x04,0x04,0x04,0x04,0x04,0x04,0x04}},
    {'\'',{0x04,0x04,0x08,0,0,0,0}}, {'"',{0x0A,0x0A,0x14,0,0,0,0}}
};

volatile uint8_t *surface() {
    return reinterpret_cast<volatile uint8_t *>(ARGUS_APP_SURFACE_ADDRESS);
}

uint64_t syscall0(uint64_t number) {
    register uint64_t result __asm__("rax") = number;
    __asm__ volatile("syscall" : "+a"(result) : : "rcx", "r11", "memory");
    return result;
}

uint64_t syscall1(uint64_t number, uint64_t first) {
    register uint64_t result __asm__("rax") = number;
    register uint64_t argument __asm__("rdi") = first;
    __asm__ volatile(
        "syscall" : "+a"(result) : "D"(argument) : "rcx", "r11", "memory"
    );
    return result;
}

uint64_t syscall2(uint64_t number, uint64_t first, uint64_t second) {
    register uint64_t result __asm__("rax") = number;
    register uint64_t argument1 __asm__("rdi") = first;
    register uint64_t argument2 __asm__("rsi") = second;
    __asm__ volatile(
        "syscall"
        : "+a"(result)
        : "D"(argument1), "S"(argument2)
        : "rcx", "r11", "memory"
    );
    return result;
}

uint8_t glyph_row(char character, uint32_t row) {
    if (row >= 7u) return 0;
    if (character >= 'a' && character <= 'z')
        character = static_cast<char>(character - 'a' + 'A');
    if (character == ' ') return 0;
    for (const Glyph &glyph : kGlyphs)
        if (glyph.character == character) return glyph.rows[row];
    return row == 0u || row == 6u ? 0x1Fu : 0x11u;
}

} // namespace

namespace argus {

uint64_t pid() { return syscall0(ARGUS_SYSCALL_GETPID); }
uint64_t ticks() { return syscall0(ARGUS_SYSCALL_CLOCK_TICKS); }
uint64_t input_poll() { return syscall0(ARGUS_SYSCALL_INPUT_POLL); }
void yield() { (void)syscall0(ARGUS_SYSCALL_YIELD); }
void wait_until(uint64_t deadline) {
    (void)syscall1(ARGUS_SYSCALL_EVENT_WAIT, deadline);
}
void wait_for_input() { wait_until(UINT64_MAX); }
[[noreturn]] void exit(uint64_t status) {
    (void)syscall1(ARGUS_SYSCALL_EXIT, status);
    for (;;) __asm__ volatile("ud2");
}

bool present(uint32_t sequence) {
    argus_app_present_v1_t request{
        ARGUS_APP_PRESENT_MAGIC,
        ARGUS_APP_ABI_VERSION,
        sequence,
        0u,
    };
    return syscall2(
        ARGUS_SYSCALL_APP_PRESENT,
        reinterpret_cast<uint64_t>(&request),
        sizeof(request)
    ) == 0u;
}

void clear(uint8_t color) {
    volatile uint8_t *pixels = surface();
    for (uint32_t index = 0; index < ARGUS_APP_SURFACE_BYTES; ++index)
        pixels[index] = color;
}

void pixel(uint32_t x, uint32_t y, uint8_t color) {
    if (x >= ARGUS_APP_SURFACE_WIDTH || y >= ARGUS_APP_SURFACE_HEIGHT) return;
    surface()[y * ARGUS_APP_SURFACE_STRIDE + x] = color;
}

void rect(
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint8_t color
) {
    if (x >= ARGUS_APP_SURFACE_WIDTH || y >= ARGUS_APP_SURFACE_HEIGHT) return;
    if (width > ARGUS_APP_SURFACE_WIDTH - x)
        width = ARGUS_APP_SURFACE_WIDTH - x;
    if (height > ARGUS_APP_SURFACE_HEIGHT - y)
        height = ARGUS_APP_SURFACE_HEIGHT - y;
    for (uint32_t row = 0; row < height; ++row)
        for (uint32_t column = 0; column < width; ++column)
            surface()[(y + row) * ARGUS_APP_SURFACE_STRIDE + x + column] = color;
}

void frame(
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint8_t color
) {
    if (!width || !height) return;
    rect(x, y, width, 1u, color);
    rect(x, y + height - 1u, width, 1u, color);
    rect(x, y, 1u, height, color);
    rect(x + width - 1u, y, 1u, height, color);
}

void text(
    const char *value,
    uint32_t x,
    uint32_t y,
    uint32_t scale,
    uint8_t color
) {
    if (!value || !scale) return;
    while (*value) {
        char character = *value++;
        for (uint32_t row = 0; row < 7u; ++row) {
            uint8_t bits = glyph_row(character, row);
            for (uint32_t column = 0; column < 5u; ++column)
                if (bits & (1u << (4u - column)))
                    rect(
                        x + column * scale,
                        y + row * scale,
                        scale,
                        scale,
                        color
                    );
        }
        x += 6u * scale;
        if (x >= ARGUS_APP_SURFACE_WIDTH) return;
    }
}

uint32_t text_width(const char *value, uint32_t scale) {
    uint32_t width = 0;
    while (value && *value++) width += 6u * scale;
    return width;
}

void unsigned_decimal(uint64_t value, char *output, uint32_t capacity) {
    if (!output || capacity < 2u) return;
    char reverse[21];
    uint32_t used = 0;
    do {
        reverse[used++] = static_cast<char>('0' + value % 10u);
        value /= 10u;
    } while (value && used < sizeof(reverse));
    uint32_t amount = used < capacity - 1u ? used : capacity - 1u;
    for (uint32_t index = 0; index < amount; ++index)
        output[index] = reverse[used - index - 1u];
    output[amount] = 0;
}

void signed_decimal(int64_t value, char *output, uint32_t capacity) {
    if (!output || capacity < 2u) return;
    if (value >= 0) {
        unsigned_decimal(static_cast<uint64_t>(value), output, capacity);
        return;
    }
    output[0] = '-';
    uint64_t magnitude = static_cast<uint64_t>(-(value + 1)) + 1u;
    unsigned_decimal(magnitude, output + 1, capacity - 1u);
}

} // namespace argus
