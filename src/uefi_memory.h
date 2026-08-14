#ifndef ARGUS_UEFI_MEMORY_H
#define ARGUS_UEFI_MEMORY_H

#include "efi.h"

typedef struct {
    unsigned char *buffer;
    UINTN size;
    UINTN capacity;
    UINTN key;
    UINTN descriptor_size;
    uint32_t descriptor_version;
} memory_map_t;

EFI_STATUS uefi_memory_map_acquire(EFI_BOOT_SERVICES *bs, memory_map_t *map);
void uefi_memory_map_release(EFI_BOOT_SERVICES *bs, memory_map_t *map);

#endif
