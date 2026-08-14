#ifndef ARGUS_USER_APP_RUNTIME_H
#define ARGUS_USER_APP_RUNTIME_H

#include "../src/app_abi.h"
#include <stdint.h>

namespace argus {

uint64_t pid();
uint64_t ticks();
uint64_t input_poll();
void yield();
void wait_until(uint64_t deadline);
void wait_for_input();
[[noreturn]] void exit(uint64_t status);
bool present(uint32_t sequence);

void clear(uint8_t color);
void pixel(uint32_t x, uint32_t y, uint8_t color);
void rect(
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint8_t color
);
void frame(
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint8_t color
);
void text(
    const char *value,
    uint32_t x,
    uint32_t y,
    uint32_t scale,
    uint8_t color
);
uint32_t text_width(const char *value, uint32_t scale);
void unsigned_decimal(uint64_t value, char *output, uint32_t capacity);
void signed_decimal(int64_t value, char *output, uint32_t capacity);

} // namespace argus

#endif
