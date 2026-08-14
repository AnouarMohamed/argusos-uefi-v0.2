#ifndef ARGUS_RAMFS_ABI_H
#define ARGUS_RAMFS_ABI_H

#include <stddef.h>
#include <stdint.h>

#define ARGUS_RAMFS_ABI_VERSION 1u
#define ARGUS_RAMFS_NAME_CAPACITY 24u
#define ARGUS_RAMFS_MAX_FILES 16u
#define ARGUS_RAMFS_MAX_PATH 48u
#define ARGUS_RAMFS_MAX_DATA 1024u

#define ARGUS_RAMFS_OK 0
#define ARGUS_RAMFS_NOT_FOUND (-1)
#define ARGUS_RAMFS_INVALID (-2)
#define ARGUS_RAMFS_NO_SPACE (-3)
#define ARGUS_RAMFS_BUFFER_TOO_SMALL (-4)

typedef int32_t (*argus_ramfs_init_fn_t)(void *state, uint64_t state_size);
typedef int32_t (*argus_ramfs_write_fn_t)(
    void *state,
    const uint8_t *path,
    uint64_t path_length,
    const uint8_t *data,
    uint64_t data_length
);
typedef int32_t (*argus_ramfs_read_fn_t)(
    const void *state,
    const uint8_t *path,
    uint64_t path_length,
    uint8_t *output,
    uint64_t output_capacity,
    uint64_t *output_length
);
typedef int32_t (*argus_ramfs_entry_fn_t)(
    const void *state,
    uint64_t index,
    uint8_t *path_output,
    uint64_t path_capacity,
    uint64_t *path_length,
    uint64_t *data_length
);
typedef int32_t (*argus_ramfs_remove_fn_t)(
    void *state,
    const uint8_t *path,
    uint64_t path_length
);

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    char name[ARGUS_RAMFS_NAME_CAPACITY];
    uint32_t state_size;
    uint32_t state_alignment;
    uint32_t max_files;
    uint32_t max_path;
    uint32_t max_data;
    uint32_t reserved;
    argus_ramfs_init_fn_t initialize;
    argus_ramfs_write_fn_t write;
    argus_ramfs_read_fn_t read;
    argus_ramfs_entry_fn_t entry;
    argus_ramfs_remove_fn_t remove;
} argus_ramfs_v1_t;

_Static_assert(sizeof(argus_ramfs_v1_t) == 96u,
               "RAMFS ABI v1 layout must remain stable");
_Static_assert(offsetof(argus_ramfs_v1_t, initialize) == 56u,
               "RAMFS ABI function table offset changed");

/* Implemented by the bounded no_std Rust object. */
const argus_ramfs_v1_t *argus_rust_ramfs_entry(void);

#endif
