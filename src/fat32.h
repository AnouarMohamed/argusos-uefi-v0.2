#ifndef ARGUS_FAT32_H
#define ARGUS_FAT32_H

#include "fat32_abi.h"

int fat32_init(const argus_block_device_v1_t *device);
int fat32_self_test(void);
int32_t fat32_info(argus_fat32_info_v1_t *info);
int32_t fat32_entry(
    uint64_t index,
    char *path,
    uint64_t path_capacity,
    uint64_t *path_length,
    uint64_t *file_size,
    uint32_t *attributes
);
int32_t fat32_read(
    const char *path,
    uint8_t *output,
    uint64_t output_capacity,
    uint64_t *output_length
);
const argus_fat32_v1_t *fat32_descriptor(void);

#endif
