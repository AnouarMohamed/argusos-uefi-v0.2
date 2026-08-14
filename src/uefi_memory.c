#include "uefi_memory.h"

EFI_STATUS uefi_memory_map_acquire(EFI_BOOT_SERVICES *bs, memory_map_t *map) {
    UINTN required = 0;
    UINTN key = 0;
    UINTN descriptor_size = 0;
    uint32_t descriptor_version = 0;

    map->buffer = 0;
    map->size = 0;
    map->capacity = 0;
    map->key = 0;
    map->descriptor_size = 0;
    map->descriptor_version = 0;

    EFI_STATUS status = bs->GetMemoryMap(
        &required, 0, &key, &descriptor_size, &descriptor_version);
    if (status != EFI_BUFFER_TOO_SMALL) return status;
    if (descriptor_size < sizeof(EFI_MEMORY_DESCRIPTOR)) return EFI_BAD_BUFFER_SIZE;

    /* Allocate extra descriptors because AllocatePool itself can grow the map. */
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        if (descriptor_size > (UINT64_MAX - required) / 8u)
            return EFI_OUT_OF_RESOURCES;

        UINTN capacity = required + descriptor_size * 8u;
        VOID *buffer = 0;
        status = bs->AllocatePool(EfiLoaderData, capacity, &buffer);
        if (status != EFI_SUCCESS) return status;

        UINTN actual_size = capacity;
        status = bs->GetMemoryMap(
            &actual_size,
            (EFI_MEMORY_DESCRIPTOR *)buffer,
            &key,
            &descriptor_size,
            &descriptor_version
        );
        if (status == EFI_SUCCESS) {
            if (descriptor_size < sizeof(EFI_MEMORY_DESCRIPTOR) ||
                actual_size % descriptor_size != 0) {
                bs->FreePool(buffer);
                return EFI_BAD_BUFFER_SIZE;
            }
            map->buffer = (unsigned char *)buffer;
            map->size = actual_size;
            map->capacity = capacity;
            map->key = key;
            map->descriptor_size = descriptor_size;
            map->descriptor_version = descriptor_version;
            return EFI_SUCCESS;
        }

        bs->FreePool(buffer);
        if (status != EFI_BUFFER_TOO_SMALL) return status;
        required = actual_size;
    }

    return EFI_BUFFER_TOO_SMALL;
}

void uefi_memory_map_release(EFI_BOOT_SERVICES *bs, memory_map_t *map) {
    if (map->buffer) {
        bs->FreePool(map->buffer);
        map->buffer = 0;
        map->size = 0;
        map->capacity = 0;
    }
}
