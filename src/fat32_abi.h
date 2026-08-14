#ifndef ARGUS_FAT32_ABI_H
#define ARGUS_FAT32_ABI_H

#include "block.h"
#include <stddef.h>
#include <stdint.h>

#define ARGUS_FAT32_ABI_VERSION 1u
#define ARGUS_FAT32_NAME_CAPACITY 24u
#define ARGUS_FAT32_MAX_PATH 48u
#define ARGUS_FAT32_MAX_READ 4096u

#define ARGUS_FAT32_OK 0
#define ARGUS_FAT32_NOT_FOUND (-1)
#define ARGUS_FAT32_INVALID (-2)
#define ARGUS_FAT32_UNSUPPORTED (-3)
#define ARGUS_FAT32_CORRUPT (-4)
#define ARGUS_FAT32_IO_ERROR (-5)
#define ARGUS_FAT32_BUFFER_TOO_SMALL (-6)

typedef struct {
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t fat_count;
    uint32_t root_cluster;
    uint64_t total_sectors;
    uint64_t data_clusters;
    uint64_t first_data_sector;
} argus_fat32_info_v1_t;

typedef int32_t (*argus_fat32_mount_fn_t)(
    void *state,
    uint64_t state_size,
    const argus_block_device_v1_t *device
);
typedef int32_t (*argus_fat32_info_fn_t)(
    const void *state,
    argus_fat32_info_v1_t *info
);
typedef int32_t (*argus_fat32_entry_fn_t)(
    const void *state,
    const argus_block_device_v1_t *device,
    uint64_t index,
    uint8_t *path_output,
    uint64_t path_capacity,
    uint64_t *path_length,
    uint64_t *file_size,
    uint32_t *attributes
);
typedef int32_t (*argus_fat32_read_fn_t)(
    const void *state,
    const argus_block_device_v1_t *device,
    const uint8_t *path,
    uint64_t path_length,
    uint8_t *output,
    uint64_t output_capacity,
    uint64_t *output_length
);

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    char name[ARGUS_FAT32_NAME_CAPACITY];
    uint32_t state_size;
    uint32_t state_alignment;
    uint32_t max_path;
    uint32_t max_read;
    uint32_t reserved[2];
    argus_fat32_mount_fn_t mount;
    argus_fat32_info_fn_t info;
    argus_fat32_entry_fn_t entry;
    argus_fat32_read_fn_t read;
} argus_fat32_v1_t;

_Static_assert(sizeof(argus_fat32_info_v1_t) == 40u,
               "FAT32 info ABI v1 layout must remain stable");
_Static_assert(sizeof(argus_fat32_v1_t) == 88u,
               "FAT32 ABI v1 layout must remain stable");
_Static_assert(offsetof(argus_fat32_v1_t, mount) == 56u,
               "FAT32 ABI function table offset changed");

const argus_fat32_v1_t *argus_rust_fat32_entry(void);

#endif
