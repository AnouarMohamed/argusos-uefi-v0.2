#include "module.h"
#include "fat32_abi.h"
#include "ramfs_abi.h"

static const argus_module_v1_t *checksum_module;
static const argus_ramfs_v1_t *ramfs_module;
static const argus_fat32_v1_t *fat32_module;

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

static int valid_ramfs_descriptor(const argus_ramfs_v1_t *module) {
    return module && module->abi_version == ARGUS_RAMFS_ABI_VERSION &&
           module->struct_size == sizeof(argus_ramfs_v1_t) &&
           valid_name(module->name) && module->state_size != 0 &&
           module->state_alignment != 0 && module->max_files != 0 &&
           module->max_path != 0 && module->max_data != 0 &&
           module->reserved == 0 && module->initialize && module->write &&
           module->read && module->entry && module->remove;
}

static int valid_fat32_descriptor(const argus_fat32_v1_t *module) {
    return module && module->abi_version == ARGUS_FAT32_ABI_VERSION &&
           module->struct_size == sizeof(argus_fat32_v1_t) &&
           valid_name(module->name) && module->state_size != 0 &&
           module->state_alignment != 0 && module->max_path != 0 &&
           module->max_read != 0 && module->reserved[0] == 0 &&
           module->reserved[1] == 0 && module->mount && module->info &&
           module->entry && module->read;
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
    checksum_module = argus_rust_module_entry();
    ramfs_module = argus_rust_ramfs_entry();
    fat32_module = argus_rust_fat32_entry();
    if (!valid_descriptor(checksum_module) ||
        !valid_ramfs_descriptor(ramfs_module) ||
        !valid_fat32_descriptor(fat32_module)) {
        checksum_module = 0;
        ramfs_module = 0;
        fat32_module = 0;
        return 0;
    }
    return 1;
}

int module_self_test(void) {
    static const uint8_t probe[] = "ArgusOS module ABI v1";
    if (!valid_descriptor(checksum_module) ||
        !valid_ramfs_descriptor(ramfs_module) ||
        !valid_fat32_descriptor(fat32_module))
        return 0;
    uint64_t length = sizeof(probe) - 1u;
    return checksum_module->checksum(probe, length) ==
               reference_fnv1a(probe, length) &&
           checksum_module->checksum(0, 0) == reference_fnv1a(0, 0);
}

uint64_t module_count(void) {
    return checksum_module && ramfs_module && fat32_module ? 3u : 0u;
}

const char *module_name_at(uint64_t index) {
    if (index == 0u && checksum_module) return checksum_module->name;
    if (index == 1u && ramfs_module) return ramfs_module->name;
    if (index == 2u && fat32_module) return fat32_module->name;
    return 0;
}

uint32_t module_abi_version_at(uint64_t index) {
    if (index == 0u && checksum_module) return checksum_module->abi_version;
    if (index == 1u && ramfs_module) return ramfs_module->abi_version;
    if (index == 2u && fat32_module) return fat32_module->abi_version;
    return 0;
}
