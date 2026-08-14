#include "block.h"

#define FIXTURE_SECTOR_SIZE 512u
#define FIXTURE_RESERVED_SECTORS 32u
#define FIXTURE_FAT_SECTORS 512u
#define FIXTURE_DATA_CLUSTERS 65525u
#define FIXTURE_TOTAL_SECTORS \
    (FIXTURE_RESERVED_SECTORS + FIXTURE_FAT_SECTORS + FIXTURE_DATA_CLUSTERS)
#define FIXTURE_FAT_LBA FIXTURE_RESERVED_SECTORS
#define FIXTURE_ROOT_LBA (FIXTURE_RESERVED_SECTORS + FIXTURE_FAT_SECTORS)
#define FIXTURE_FILE_LBA (FIXTURE_ROOT_LBA + 1u)

static const uint8_t fixture_file[] = "ArgusOS FAT32 via Rust\n";

static void zero_bytes(uint8_t *bytes, uint64_t count) {
    for (uint64_t i = 0; i < count; ++i) bytes[i] = 0;
}

static void write_u16(uint8_t *bytes, unsigned offset, uint16_t value) {
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1u] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t *bytes, unsigned offset, uint32_t value) {
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1u] = (uint8_t)(value >> 8);
    bytes[offset + 2u] = (uint8_t)(value >> 16);
    bytes[offset + 3u] = (uint8_t)(value >> 24);
}

static void copy_literal(uint8_t *output, unsigned offset, const char *text, unsigned count) {
    for (unsigned i = 0; i < count; ++i) output[offset + i] = (uint8_t)text[i];
}

static void synthesize_boot_sector(uint8_t output[FIXTURE_SECTOR_SIZE]) {
    output[0] = 0xEBu;
    output[1] = 0x58u;
    output[2] = 0x90u;
    copy_literal(output, 3u, "ARGUSOS ", 8u);
    write_u16(output, 11u, FIXTURE_SECTOR_SIZE);
    output[13] = 1u;
    write_u16(output, 14u, FIXTURE_RESERVED_SECTORS);
    output[16] = 1u;
    write_u16(output, 17u, 0u);
    write_u16(output, 19u, 0u);
    output[21] = 0xF8u;
    write_u16(output, 22u, 0u);
    write_u16(output, 24u, 63u);
    write_u16(output, 26u, 255u);
    write_u32(output, 28u, 0u);
    write_u32(output, 32u, FIXTURE_TOTAL_SECTORS);
    write_u32(output, 36u, FIXTURE_FAT_SECTORS);
    write_u16(output, 40u, 0u);
    write_u16(output, 42u, 0u);
    write_u32(output, 44u, 2u);
    write_u16(output, 48u, 1u);
    write_u16(output, 50u, 6u);
    output[64] = 0x80u;
    output[66] = 0x29u;
    write_u32(output, 67u, 0xA610F320u);
    copy_literal(output, 71u, "ARGUS FAT  ", 11u);
    copy_literal(output, 82u, "FAT32   ", 8u);
    output[510] = 0x55u;
    output[511] = 0xAAu;
}

static void synthesize_fsinfo(uint8_t output[FIXTURE_SECTOR_SIZE]) {
    write_u32(output, 0u, 0x41615252u);
    write_u32(output, 484u, 0x61417272u);
    write_u32(output, 488u, UINT32_MAX);
    write_u32(output, 492u, UINT32_MAX);
    write_u32(output, 508u, 0xAA550000u);
}

static void synthesize_fat(uint8_t output[FIXTURE_SECTOR_SIZE]) {
    write_u32(output, 0u, 0x0FFFFFF8u);
    write_u32(output, 4u, 0x0FFFFFFFu);
    write_u32(output, 8u, 0x0FFFFFFFu);
    write_u32(output, 12u, 0x0FFFFFFFu);
}

static void synthesize_root(uint8_t output[FIXTURE_SECTOR_SIZE]) {
    copy_literal(output, 0u, "HELLO   TXT", 11u);
    output[11] = 0x20u;
    write_u16(output, 20u, 0u);
    write_u16(output, 26u, 3u);
    write_u32(output, 28u, (uint32_t)(sizeof(fixture_file) - 1u));
}

static int32_t fixture_read(
    const void *context,
    uint64_t first_sector,
    uint32_t sector_count,
    uint8_t *output,
    uint64_t output_capacity
) {
    (void)context;
    if (!sector_count || !output) return ARGUS_BLOCK_INVALID;
    if (first_sector >= FIXTURE_TOTAL_SECTORS ||
        sector_count > FIXTURE_TOTAL_SECTORS - first_sector)
        return ARGUS_BLOCK_RANGE;
    uint64_t required = (uint64_t)sector_count * FIXTURE_SECTOR_SIZE;
    if (output_capacity < required) return ARGUS_BLOCK_BUFFER_TOO_SMALL;

    for (uint32_t i = 0; i < sector_count; ++i) {
        uint64_t sector = first_sector + i;
        uint8_t *destination = output + (uint64_t)i * FIXTURE_SECTOR_SIZE;
        zero_bytes(destination, FIXTURE_SECTOR_SIZE);
        if (sector == 0u || sector == 6u)
            synthesize_boot_sector(destination);
        else if (sector == 1u)
            synthesize_fsinfo(destination);
        else if (sector == FIXTURE_FAT_LBA)
            synthesize_fat(destination);
        else if (sector == FIXTURE_ROOT_LBA)
            synthesize_root(destination);
        else if (sector == FIXTURE_FILE_LBA)
            for (unsigned byte = 0; byte < sizeof(fixture_file) - 1u; ++byte)
                destination[byte] = fixture_file[byte];
    }
    return ARGUS_BLOCK_OK;
}

static const argus_block_device_v1_t fixture_device = {
    ARGUS_BLOCK_ABI_VERSION,
    (uint32_t)sizeof(argus_block_device_v1_t),
    "memory.fat32",
    FIXTURE_SECTOR_SIZE,
    0,
    FIXTURE_TOTAL_SECTORS,
    0,
    fixture_read
};

static int valid_name(const char name[ARGUS_BLOCK_NAME_CAPACITY]) {
    for (unsigned i = 0; i < ARGUS_BLOCK_NAME_CAPACITY; ++i) {
        uint8_t character = (uint8_t)name[i];
        if (!character) return i != 0;
        if (character < 32u || character > 126u) return 0;
    }
    return 0;
}

int block_device_valid(const argus_block_device_v1_t *device) {
    return device && device->abi_version == ARGUS_BLOCK_ABI_VERSION &&
           device->struct_size == sizeof(argus_block_device_v1_t) &&
           valid_name(device->name) && device->sector_size >= 512u &&
           (device->sector_size & (device->sector_size - 1u)) == 0 &&
           device->sector_count != 0 && device->reserved == 0 && device->read;
}

int32_t block_read(
    const argus_block_device_v1_t *device,
    uint64_t first_sector,
    uint32_t sector_count,
    uint8_t *output,
    uint64_t output_capacity
) {
    if (!block_device_valid(device) || !sector_count || !output)
        return ARGUS_BLOCK_INVALID;
    if (first_sector >= device->sector_count ||
        sector_count > device->sector_count - first_sector)
        return ARGUS_BLOCK_RANGE;
    if (sector_count > UINT64_MAX / device->sector_size)
        return ARGUS_BLOCK_INVALID;
    uint64_t required = (uint64_t)sector_count * device->sector_size;
    if (output_capacity < required) return ARGUS_BLOCK_BUFFER_TOO_SMALL;
    return device->read(
        device->context,
        first_sector,
        sector_count,
        output,
        output_capacity
    );
}

int block_init(void) { return block_device_valid(&fixture_device); }

int block_self_test(void) {
    uint8_t sector[FIXTURE_SECTOR_SIZE];
    int valid = block_read(&fixture_device, 0, 1u, sector, sizeof(sector)) ==
                    ARGUS_BLOCK_OK &&
                sector[510] == 0x55u && sector[511] == 0xAAu;
    valid = block_read(&fixture_device, FIXTURE_TOTAL_SECTORS, 1u,
                       sector, sizeof(sector)) == ARGUS_BLOCK_RANGE && valid;
    valid = block_read(&fixture_device, FIXTURE_FILE_LBA, 1u,
                       sector, sizeof(sector) - 1u) ==
                ARGUS_BLOCK_BUFFER_TOO_SMALL && valid;
    valid = block_read(&fixture_device, FIXTURE_FILE_LBA, 1u,
                       sector, sizeof(sector)) == ARGUS_BLOCK_OK && valid;
    for (unsigned i = 0; i < sizeof(fixture_file) - 1u; ++i)
        valid = sector[i] == fixture_file[i] && valid;
    return valid;
}

const argus_block_device_v1_t *block_default_device(void) {
    return block_device_valid(&fixture_device) ? &fixture_device : 0;
}
