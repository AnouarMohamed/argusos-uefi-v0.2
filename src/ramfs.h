#ifndef ARGUS_RAMFS_H
#define ARGUS_RAMFS_H

#include "ramfs_abi.h"

int ramfs_init(void);
int ramfs_self_test(void);
uint64_t ramfs_file_count(void);
uint64_t ramfs_capacity(void);
uint64_t ramfs_max_file_size(void);
int32_t ramfs_write(const char *path, const uint8_t *data, uint64_t length);
int32_t ramfs_read(
    const char *path,
    uint8_t *output,
    uint64_t capacity,
    uint64_t *length
);
int32_t ramfs_entry(
    uint64_t index,
    char *path,
    uint64_t capacity,
    uint64_t *path_length,
    uint64_t *data_length
);
int32_t ramfs_remove(const char *path);
const argus_ramfs_v1_t *ramfs_descriptor(void);

#endif
