#ifndef ARGUS_BOOT_INFO_H
#define ARGUS_BOOT_INFO_H

#include "uefi_memory.h"

#define ARGUS_BOOT_INFO_MAGIC 0x4152475553424F4FULL
#define ARGUS_BOOT_INFO_VERSION 1u
#define ARGUS_PAGE_SIZE 4096ULL

typedef struct {
    uint64_t base;
    uint64_t size;
} physical_range_t;

typedef struct {
    uint64_t base;
    uint64_t size;
    uint32_t width;
    uint32_t height;
    uint32_t pitch_pixels;
    EFI_GRAPHICS_PIXEL_FORMAT format;
    EFI_PIXEL_BITMASK masks;
    uint32_t usable;
} framebuffer_info_t;

typedef struct {
    uint64_t magic;
    uint32_t version;
    uint32_t boot_services_exited;
    uint32_t exception_self_test;
    uint32_t reserved;

    memory_map_t memory_map;
    framebuffer_info_t framebuffer;
    VOID *acpi_rsdp;

    physical_range_t kernel_image;
    physical_range_t boot_info_storage;
    physical_range_t kernel_stack;
    physical_range_t pmm_bitmap_storage;
    physical_range_t memory_map_storage;

    uint64_t pmm_page_count;
    uint64_t pmm_bitmap_bytes;
} boot_info_t;

#endif
