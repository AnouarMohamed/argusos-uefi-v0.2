#ifndef ARGUS_BLOCK_H
#define ARGUS_BLOCK_H

#include <stddef.h>
#include <stdint.h>

#define ARGUS_BLOCK_ABI_VERSION 1u
#define ARGUS_BLOCK_NAME_CAPACITY 24u

#define ARGUS_BLOCK_OK 0
#define ARGUS_BLOCK_INVALID (-1)
#define ARGUS_BLOCK_RANGE (-2)
#define ARGUS_BLOCK_BUFFER_TOO_SMALL (-3)
#define ARGUS_BLOCK_IO_ERROR (-4)

typedef int32_t (*argus_block_read_fn_t)(
    const void *context,
    uint64_t first_sector,
    uint32_t sector_count,
    uint8_t *output,
    uint64_t output_capacity
);

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    char name[ARGUS_BLOCK_NAME_CAPACITY];
    uint32_t sector_size;
    uint32_t reserved;
    uint64_t sector_count;
    const void *context;
    argus_block_read_fn_t read;
} argus_block_device_v1_t;

_Static_assert(sizeof(argus_block_device_v1_t) == 64u,
               "block-device ABI v1 layout must remain stable");
_Static_assert(offsetof(argus_block_device_v1_t, read) == 56u,
               "block-device ABI read offset changed");

int block_init(void);
int block_self_test(void);
int block_device_valid(const argus_block_device_v1_t *device);
int32_t block_read(
    const argus_block_device_v1_t *device,
    uint64_t first_sector,
    uint32_t sector_count,
    uint8_t *output,
    uint64_t output_capacity
);
const argus_block_device_v1_t *block_default_device(void);

#endif
