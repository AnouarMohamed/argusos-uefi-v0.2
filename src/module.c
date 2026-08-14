#include "module.h"

static const argus_module_v1_t *rust_module;

static int valid_name(const char name[ARGUS_MODULE_NAME_CAPACITY]) {
    for (unsigned i = 0; i < ARGUS_MODULE_NAME_CAPACITY; ++i) {
        uint8_t character = (uint8_t)name[i];
        if (!character) return i != 0;
        if (character < 32u || character > 126u) return 0;
    }
    return 0;
}

static int valid_descriptor(const argus_module_v1_t *module) {
    return module &&
           module->abi_version == ARGUS_MODULE_ABI_VERSION &&
           module->struct_size == sizeof(argus_module_v1_t) &&
           valid_name(module->name) && module->checksum;
}

static uint64_t reference_fnv1a(const uint8_t *bytes, uint64_t length) {
    uint64_t hash = 14695981039346656037ULL;
    for (uint64_t i = 0; i < length; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

int module_init(void) {
    rust_module = argus_rust_module_entry();
    if (!valid_descriptor(rust_module)) {
        rust_module = 0;
        return 0;
    }
    return 1;
}

int module_self_test(void) {
    static const uint8_t probe[] = "ArgusOS module ABI v1";
    if (!valid_descriptor(rust_module)) return 0;
    uint64_t length = sizeof(probe) - 1u;
    return rust_module->checksum(probe, length) == reference_fnv1a(probe, length) &&
           rust_module->checksum(0, 0) == reference_fnv1a(0, 0);
}

uint64_t module_count(void) { return rust_module ? 1u : 0u; }

const argus_module_v1_t *module_at(uint64_t index) {
    return index == 0u ? rust_module : 0;
}
