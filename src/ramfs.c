#include "ramfs.h"

#define RAMFS_STATE_CAPACITY 32768u
#define RAMFS_STATE_ALIGNMENT 16u

static _Alignas(RAMFS_STATE_ALIGNMENT) uint8_t state_storage[RAMFS_STATE_CAPACITY];
static const argus_ramfs_v1_t *filesystem;

static int valid_name(const char name[ARGUS_RAMFS_NAME_CAPACITY]) {
    for (unsigned i = 0; i < ARGUS_RAMFS_NAME_CAPACITY; ++i) {
        uint8_t character = (uint8_t)name[i];
        if (!character) return i != 0;
        if (character < 32u || character > 126u) return 0;
    }
    return 0;
}

static int valid_descriptor(const argus_ramfs_v1_t *descriptor) {
    return descriptor && descriptor->abi_version == ARGUS_RAMFS_ABI_VERSION &&
           descriptor->struct_size == sizeof(argus_ramfs_v1_t) &&
           valid_name(descriptor->name) && descriptor->state_size != 0 &&
           descriptor->state_size <= sizeof(state_storage) &&
           descriptor->state_alignment != 0 &&
           (descriptor->state_alignment & (descriptor->state_alignment - 1u)) == 0 &&
           descriptor->state_alignment <= RAMFS_STATE_ALIGNMENT &&
           descriptor->max_files == ARGUS_RAMFS_MAX_FILES &&
           descriptor->max_path == ARGUS_RAMFS_MAX_PATH &&
           descriptor->max_data == ARGUS_RAMFS_MAX_DATA &&
           descriptor->reserved == 0 && descriptor->initialize &&
           descriptor->write && descriptor->read && descriptor->entry &&
           descriptor->remove;
}

static int bounded_length(const char *text, uint64_t maximum, uint64_t *length) {
    if (!text || !length) return 0;
    uint64_t count = 0;
    while (count <= maximum && text[count]) ++count;
    if (count > maximum) return 0;
    *length = count;
    return 1;
}

static int reset_filesystem(void) {
    return filesystem &&
           filesystem->initialize(state_storage, sizeof(state_storage)) == ARGUS_RAMFS_OK;
}

static int seed_filesystem(void) {
    static const uint8_t readme[] = "ArgusOS Rust RAMFS\n";
    static const uint8_t motd[] = "memory-safe files, bounded by design\n";
    return ramfs_write("/README", readme, sizeof(readme) - 1u) == ARGUS_RAMFS_OK &&
           ramfs_write("/etc/motd", motd, sizeof(motd) - 1u) == ARGUS_RAMFS_OK;
}

int ramfs_init(void) {
    filesystem = argus_rust_ramfs_entry();
    if (!valid_descriptor(filesystem)) {
        filesystem = 0;
        return 0;
    }
    return reset_filesystem();
}

int ramfs_self_test(void) {
    if (!reset_filesystem()) return 0;
    int valid = ramfs_write("relative", (const uint8_t *)"x", 1u) ==
                    ARGUS_RAMFS_INVALID &&
                ramfs_write("/bad//path", (const uint8_t *)"x", 1u) ==
                    ARGUS_RAMFS_INVALID &&
                ramfs_write("/../escape", (const uint8_t *)"x", 1u) ==
                    ARGUS_RAMFS_INVALID;

    static const char digits[] = "0123456789ABCDEF";
    for (unsigned i = 0; i < ARGUS_RAMFS_MAX_FILES; ++i) {
        char path[] = {'/', 'f', digits[i], 0};
        uint8_t payload = (uint8_t)(0x40u + i);
        valid = ramfs_write(path, &payload, 1u) == ARGUS_RAMFS_OK && valid;
    }
    valid = ramfs_file_count() == ARGUS_RAMFS_MAX_FILES && valid;
    valid = ramfs_write("/overflow", (const uint8_t *)"x", 1u) ==
                ARGUS_RAMFS_NO_SPACE && valid;

    static const uint8_t replacement_data[] = {0x4Au, 0x5Au};
    valid = ramfs_write("/fA", replacement_data, sizeof(replacement_data)) ==
                ARGUS_RAMFS_OK &&
            ramfs_file_count() == ARGUS_RAMFS_MAX_FILES && valid;
    uint8_t output[2] = {0};
    uint64_t output_length = 0;
    valid = ramfs_read("/fA", output, 1u, &output_length) ==
                ARGUS_RAMFS_BUFFER_TOO_SMALL && output_length == 2u && valid;
    valid = ramfs_read("/fA", output, sizeof(output), &output_length) ==
                ARGUS_RAMFS_OK &&
            output[0] == replacement_data[0] &&
            output[1] == replacement_data[1] && output_length == 2u && valid;
    valid = ramfs_remove("/f0") == ARGUS_RAMFS_OK &&
            ramfs_remove("/f0") == ARGUS_RAMFS_NOT_FOUND && valid;
    valid = ramfs_write("/replacement", (const uint8_t *)"ok", 2u) ==
                ARGUS_RAMFS_OK && valid;

    char path[ARGUS_RAMFS_MAX_PATH + 1u];
    uint64_t path_length = 0;
    uint64_t data_length = 0;
    for (uint64_t i = 0; i < ARGUS_RAMFS_MAX_FILES; ++i)
        valid = ramfs_entry(i, path, sizeof(path), &path_length, &data_length) ==
                    ARGUS_RAMFS_OK && path_length != 0 && valid;
    valid = ramfs_entry(ARGUS_RAMFS_MAX_FILES, path, sizeof(path),
                        &path_length, &data_length) == ARGUS_RAMFS_NOT_FOUND && valid;

    int seeded = reset_filesystem() && seed_filesystem();
    return valid && seeded && ramfs_file_count() == 2u;
}

uint64_t ramfs_file_count(void) {
    if (!filesystem) return 0;
    uint64_t count = 0;
    uint8_t path[ARGUS_RAMFS_MAX_PATH];
    uint64_t path_length;
    uint64_t data_length;
    while (count < filesystem->max_files &&
           filesystem->entry(state_storage, count, path, sizeof(path),
                             &path_length, &data_length) == ARGUS_RAMFS_OK)
        ++count;
    return count;
}

uint64_t ramfs_capacity(void) {
    return filesystem ? filesystem->max_files : 0;
}

uint64_t ramfs_max_file_size(void) {
    return filesystem ? filesystem->max_data : 0;
}

int32_t ramfs_write(const char *path, const uint8_t *data, uint64_t length) {
    uint64_t path_length;
    if (!filesystem || !bounded_length(path, filesystem->max_path, &path_length))
        return ARGUS_RAMFS_INVALID;
    return filesystem->write(
        state_storage,
        (const uint8_t *)path,
        path_length,
        data,
        length
    );
}

int32_t ramfs_read(
    const char *path,
    uint8_t *output,
    uint64_t capacity,
    uint64_t *length
) {
    uint64_t path_length;
    if (!filesystem || !bounded_length(path, filesystem->max_path, &path_length))
        return ARGUS_RAMFS_INVALID;
    return filesystem->read(
        state_storage,
        (const uint8_t *)path,
        path_length,
        output,
        capacity,
        length
    );
}

int32_t ramfs_entry(
    uint64_t index,
    char *path,
    uint64_t capacity,
    uint64_t *path_length,
    uint64_t *data_length
) {
    if (!filesystem || !path || !capacity || !path_length || !data_length)
        return ARGUS_RAMFS_INVALID;
    int32_t status = filesystem->entry(
        state_storage,
        index,
        (uint8_t *)path,
        capacity - 1u,
        path_length,
        data_length
    );
    if (status == ARGUS_RAMFS_OK) {
        if (*path_length >= capacity) return ARGUS_RAMFS_INVALID;
        path[*path_length] = 0;
    }
    return status;
}

int32_t ramfs_remove(const char *path) {
    uint64_t path_length;
    if (!filesystem || !bounded_length(path, filesystem->max_path, &path_length))
        return ARGUS_RAMFS_INVALID;
    return filesystem->remove(state_storage, (const uint8_t *)path, path_length);
}

const argus_ramfs_v1_t *ramfs_descriptor(void) { return filesystem; }
