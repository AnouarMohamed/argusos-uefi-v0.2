#include "fat32.h"

#define FAT32_STATE_CAPACITY 256u
#define FAT32_STATE_ALIGNMENT 16u

static _Alignas(FAT32_STATE_ALIGNMENT) uint8_t state_storage[FAT32_STATE_CAPACITY];
static const argus_fat32_v1_t *reader;
static const argus_block_device_v1_t *mounted_device;

static int valid_name(const char name[ARGUS_FAT32_NAME_CAPACITY]) {
    for (unsigned i = 0; i < ARGUS_FAT32_NAME_CAPACITY; ++i) {
        uint8_t character = (uint8_t)name[i];
        if (!character) return i != 0;
        if (character < 32u || character > 126u) return 0;
    }
    return 0;
}

static int valid_descriptor(const argus_fat32_v1_t *descriptor) {
    return descriptor && descriptor->abi_version == ARGUS_FAT32_ABI_VERSION &&
           descriptor->struct_size == sizeof(argus_fat32_v1_t) &&
           valid_name(descriptor->name) && descriptor->state_size != 0 &&
           descriptor->state_size <= sizeof(state_storage) &&
           descriptor->state_alignment != 0 &&
           (descriptor->state_alignment & (descriptor->state_alignment - 1u)) == 0 &&
           descriptor->state_alignment <= FAT32_STATE_ALIGNMENT &&
           descriptor->max_path == ARGUS_FAT32_MAX_PATH &&
           descriptor->max_read == ARGUS_FAT32_MAX_READ &&
           descriptor->reserved[0] == 0 && descriptor->reserved[1] == 0 &&
           descriptor->mount && descriptor->info && descriptor->entry &&
           descriptor->read;
}

static int bounded_length(const char *text, uint64_t maximum, uint64_t *length) {
    if (!text || !length) return 0;
    uint64_t count = 0;
    while (count <= maximum && text[count]) ++count;
    if (count > maximum) return 0;
    *length = count;
    return 1;
}

static int strings_equal(const char *left, const char *right) {
    while (*left && *right && *left == *right) {
        ++left;
        ++right;
    }
    return *left == 0 && *right == 0;
}

int fat32_init(const argus_block_device_v1_t *device) {
    reader = argus_rust_fat32_entry();
    mounted_device = 0;
    if (!block_device_valid(device) || !valid_descriptor(reader)) {
        reader = 0;
        return 0;
    }
    if (reader->mount(state_storage, sizeof(state_storage), device) != ARGUS_FAT32_OK) {
        reader = 0;
        return 0;
    }
    mounted_device = device;
    return 1;
}

int fat32_self_test(void) {
    if (!reader || !mounted_device) return 0;
    argus_fat32_info_v1_t volume;
    int valid = fat32_info(&volume) == ARGUS_FAT32_OK &&
                volume.bytes_per_sector == 512u &&
                volume.sectors_per_cluster == 1u && volume.fat_count == 1u &&
                volume.root_cluster == 2u && volume.total_sectors == 66069u &&
                volume.data_clusters == 65525u &&
                volume.first_data_sector == 544u;

    char path[ARGUS_FAT32_MAX_PATH + 1u];
    uint64_t path_length = 0;
    uint64_t file_size = 0;
    uint32_t attributes = 0;
    valid = fat32_entry(0, path, sizeof(path), &path_length,
                        &file_size, &attributes) == ARGUS_FAT32_OK &&
            strings_equal(path, "/HELLO.TXT") && path_length == 10u &&
            file_size == 23u && attributes == 0x20u && valid;
    valid = fat32_entry(1u, path, sizeof(path), &path_length,
                        &file_size, &attributes) == ARGUS_FAT32_NOT_FOUND && valid;

    static const uint8_t expected[] = "ArgusOS FAT32 via Rust\n";
    uint8_t output[sizeof(expected) - 1u];
    uint64_t output_length = 0;
    valid = fat32_read("/HELLO.TXT", output, 0, &output_length) ==
                ARGUS_FAT32_BUFFER_TOO_SMALL &&
            output_length == sizeof(expected) - 1u && valid;
    valid = fat32_read("/hello.txt", output, sizeof(output), &output_length) ==
                ARGUS_FAT32_OK &&
            output_length == sizeof(expected) - 1u && valid;
    for (unsigned i = 0; i < sizeof(output); ++i)
        valid = output[i] == expected[i] && valid;
    valid = fat32_read("HELLO.TXT", output, sizeof(output), &output_length) ==
                ARGUS_FAT32_INVALID &&
            fat32_read("/DIR/FILE", output, sizeof(output), &output_length) ==
                ARGUS_FAT32_INVALID &&
            fat32_read("/MISSING.TXT", output, sizeof(output), &output_length) ==
                ARGUS_FAT32_NOT_FOUND && valid;
    return valid;
}

int32_t fat32_info(argus_fat32_info_v1_t *info) {
    if (!reader || !mounted_device || !info) return ARGUS_FAT32_INVALID;
    return reader->info(state_storage, info);
}

int32_t fat32_entry(
    uint64_t index,
    char *path,
    uint64_t path_capacity,
    uint64_t *path_length,
    uint64_t *file_size,
    uint32_t *attributes
) {
    if (!reader || !mounted_device || !path || !path_capacity ||
        !path_length || !file_size || !attributes)
        return ARGUS_FAT32_INVALID;
    int32_t status = reader->entry(
        state_storage,
        mounted_device,
        index,
        (uint8_t *)path,
        path_capacity - 1u,
        path_length,
        file_size,
        attributes
    );
    if (status == ARGUS_FAT32_OK) {
        if (*path_length >= path_capacity) return ARGUS_FAT32_CORRUPT;
        path[*path_length] = 0;
    }
    return status;
}

int32_t fat32_read(
    const char *path,
    uint8_t *output,
    uint64_t output_capacity,
    uint64_t *output_length
) {
    uint64_t path_length;
    if (!reader || !mounted_device || !output_length ||
        !bounded_length(path, reader->max_path, &path_length))
        return ARGUS_FAT32_INVALID;
    return reader->read(
        state_storage,
        mounted_device,
        (const uint8_t *)path,
        path_length,
        output,
        output_capacity,
        output_length
    );
}

const argus_fat32_v1_t *fat32_descriptor(void) { return reader; }
