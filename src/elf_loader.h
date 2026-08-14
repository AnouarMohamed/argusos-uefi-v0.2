#ifndef ARGUS_ELF_LOADER_H
#define ARGUS_ELF_LOADER_H

#include <stdint.h>

#define ARGUS_ELF_MAX_SEGMENTS 8u
#define ARGUS_ELF_MAX_IMAGE_PAGES 16u

typedef struct {
    uint64_t file_offset;
    uint64_t file_size;
    uint64_t virtual_address;
    uint64_t memory_size;
    uint32_t flags;
    uint32_t page_count;
} argus_elf_segment_t;

typedef struct {
    uint64_t entry;
    uint32_t segment_count;
    uint32_t total_pages;
    argus_elf_segment_t segments[ARGUS_ELF_MAX_SEGMENTS];
} argus_elf_image_t;

int elf_validate_user_image(
    const uint8_t *bytes,
    uint64_t size,
    uint64_t user_min,
    uint64_t user_max,
    argus_elf_image_t *image
);

#endif
