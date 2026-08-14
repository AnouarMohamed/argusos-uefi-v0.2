#include "elf_loader.h"

#define ELF_CLASS_64 2u
#define ELF_DATA_LITTLE_ENDIAN 1u
#define ELF_CURRENT_VERSION 1u
#define ELF_TYPE_EXECUTABLE 2u
#define ELF_MACHINE_X86_64 62u
#define ELF_PROGRAM_LOAD 1u
#define ELF_PROGRAM_GNU_STACK 0x6474E551u
#define ELF_FLAG_EXECUTE 1u
#define ELF_FLAG_WRITE 2u
#define ELF_FLAG_READ 4u
#define ELF_PAGE_SIZE 4096u

typedef struct __attribute__((packed)) {
    uint8_t identification[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t program_header_offset;
    uint64_t section_header_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_header_size;
    uint16_t program_header_count;
    uint16_t section_header_size;
    uint16_t section_header_count;
    uint16_t section_name_index;
} elf64_header_t;

typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
} elf64_program_header_t;

_Static_assert(sizeof(elf64_header_t) == 64u, "ELF64 header layout changed");
_Static_assert(sizeof(elf64_program_header_t) == 56u,
               "ELF64 program header layout changed");

static int add_overflows(uint64_t left, uint64_t right) {
    return left > UINT64_MAX - right;
}

static int ranges_overlap(
    uint64_t first_start,
    uint64_t first_size,
    uint64_t second_start,
    uint64_t second_size
) {
    return first_start < second_start + second_size &&
           second_start < first_start + first_size;
}

int elf_validate_user_image(
    const uint8_t *bytes,
    uint64_t size,
    uint64_t user_min,
    uint64_t user_max,
    argus_elf_image_t *image
) {
    if (!bytes || !image || size < sizeof(elf64_header_t) ||
        user_min >= user_max)
        return 0;

    const elf64_header_t *header = (const elf64_header_t *)(const void *)bytes;
    if (header->identification[0] != 0x7Fu ||
        header->identification[1] != 'E' ||
        header->identification[2] != 'L' ||
        header->identification[3] != 'F' ||
        header->identification[4] != ELF_CLASS_64 ||
        header->identification[5] != ELF_DATA_LITTLE_ENDIAN ||
        header->identification[6] != ELF_CURRENT_VERSION ||
        header->type != ELF_TYPE_EXECUTABLE ||
        header->machine != ELF_MACHINE_X86_64 ||
        header->version != ELF_CURRENT_VERSION ||
        header->flags != 0u ||
        header->header_size != sizeof(elf64_header_t) ||
        header->program_header_size != sizeof(elf64_program_header_t) ||
        !header->program_header_count ||
        header->program_header_count > ARGUS_ELF_MAX_SEGMENTS + 1u)
        return 0;

    uint64_t table_size =
        (uint64_t)header->program_header_count * sizeof(elf64_program_header_t);
    if (add_overflows(header->program_header_offset, table_size) ||
        header->program_header_offset + table_size > size)
        return 0;

    *image = (argus_elf_image_t){0};
    int entry_is_executable = 0;
    int stack_seen = 0;
    for (uint16_t index = 0; index < header->program_header_count; ++index) {
        const elf64_program_header_t *program =
            (const elf64_program_header_t *)(const void *)(
                bytes + header->program_header_offset +
                (uint64_t)index * sizeof(elf64_program_header_t));
        if (program->type == ELF_PROGRAM_GNU_STACK) {
            if (stack_seen ||
                program->flags != (ELF_FLAG_READ | ELF_FLAG_WRITE) ||
                program->offset || program->virtual_address ||
                program->physical_address || program->file_size ||
                program->memory_size)
                return 0;
            stack_seen = 1;
            continue;
        }
        if (program->type != ELF_PROGRAM_LOAD) return 0;
        if (image->segment_count == ARGUS_ELF_MAX_SEGMENTS ||
            !program->memory_size || program->file_size > program->memory_size ||
            !(program->flags & ELF_FLAG_READ) ||
            (program->flags & ~(ELF_FLAG_READ | ELF_FLAG_WRITE |
                                ELF_FLAG_EXECUTE)) ||
            ((program->flags & ELF_FLAG_WRITE) &&
             (program->flags & ELF_FLAG_EXECUTE)) ||
            (program->offset & (ELF_PAGE_SIZE - 1u)) ||
            (program->virtual_address & (ELF_PAGE_SIZE - 1u)) ||
            (program->alignment != ELF_PAGE_SIZE) ||
            add_overflows(program->offset, program->file_size) ||
            program->offset + program->file_size > size ||
            add_overflows(program->virtual_address, program->memory_size) ||
            program->virtual_address < user_min ||
            program->virtual_address + program->memory_size > user_max)
            return 0;

        uint64_t rounded_size = program->memory_size;
        if (add_overflows(rounded_size, ELF_PAGE_SIZE - 1u)) return 0;
        rounded_size = (rounded_size + ELF_PAGE_SIZE - 1u) &
            ~(uint64_t)(ELF_PAGE_SIZE - 1u);
        uint32_t page_count = (uint32_t)(rounded_size / ELF_PAGE_SIZE);
        if (!page_count || image->total_pages >
                ARGUS_ELF_MAX_IMAGE_PAGES - page_count)
            return 0;

        for (uint32_t prior = 0; prior < image->segment_count; ++prior) {
            const argus_elf_segment_t *other = &image->segments[prior];
            if (ranges_overlap(
                    program->virtual_address,
                    rounded_size,
                    other->virtual_address,
                    (uint64_t)other->page_count * ELF_PAGE_SIZE))
                return 0;
        }

        argus_elf_segment_t *segment =
            &image->segments[image->segment_count++];
        segment->file_offset = program->offset;
        segment->file_size = program->file_size;
        segment->virtual_address = program->virtual_address;
        segment->memory_size = program->memory_size;
        segment->flags = program->flags;
        segment->page_count = page_count;
        image->total_pages += page_count;

        if ((program->flags & ELF_FLAG_EXECUTE) &&
            header->entry >= program->virtual_address &&
            header->entry < program->virtual_address + program->file_size)
            entry_is_executable = 1;
    }

    if (!image->segment_count || !entry_is_executable || !stack_seen) return 0;
    image->entry = header->entry;
    return 1;
}
