#include <stddef.h>
#include <stdint.h>

void *memset(void *destination, int value, size_t count) {
    uint8_t *bytes = (uint8_t *)destination;
    for (size_t i = 0; i < count; ++i) bytes[i] = (uint8_t)value;
    return destination;
}

void *memcpy(void *destination, const void *source, size_t count) {
    uint8_t *output = (uint8_t *)destination;
    const uint8_t *input = (const uint8_t *)source;
    for (size_t i = 0; i < count; ++i) output[i] = input[i];
    return destination;
}

void *memmove(void *destination, const void *source, size_t count) {
    uint8_t *output = (uint8_t *)destination;
    const uint8_t *input = (const uint8_t *)source;
    uintptr_t output_address = (uintptr_t)output;
    uintptr_t input_address = (uintptr_t)input;
    if (output_address < input_address) {
        for (size_t i = 0; i < count; ++i) output[i] = input[i];
    } else if (output_address > input_address) {
        while (count) {
            --count;
            output[count] = input[count];
        }
    }
    return destination;
}
