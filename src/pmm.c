#include "pmm.h"

static uint8_t *usable_bitmap;
static uint8_t *used_bitmap;
static uint64_t page_count;
static uint64_t managed_count;
static uint64_t free_count;

static int bit_get(const uint8_t *bitmap, uint64_t page) {
    return (bitmap[page >> 3] >> (page & 7u)) & 1u;
}

static void bit_set(uint8_t *bitmap, uint64_t page) {
    bitmap[page >> 3] |= (uint8_t)(1u << (page & 7u));
}

static void bit_clear(uint8_t *bitmap, uint64_t page) {
    bitmap[page >> 3] &= (uint8_t)~(1u << (page & 7u));
}

static void make_usable(uint64_t page) {
    if (page >= page_count || bit_get(usable_bitmap, page)) return;
    bit_set(usable_bitmap, page);
    bit_clear(used_bitmap, page);
    ++managed_count;
    ++free_count;
}

static void reserve_page(uint64_t page) {
    if (page >= page_count || !bit_get(usable_bitmap, page)) return;
    bit_clear(usable_bitmap, page);
    if (!bit_get(used_bitmap, page)) --free_count;
    bit_set(used_bitmap, page);
    --managed_count;
}

static void reserve_range(physical_range_t range) {
    if (!range.size || range.base >= page_count * ARGUS_PAGE_SIZE) return;

    uint64_t start = range.base / ARGUS_PAGE_SIZE;
    uint64_t last_byte;
    if (range.size - 1u > UINT64_MAX - range.base)
        last_byte = UINT64_MAX;
    else
        last_byte = range.base + range.size - 1u;

    uint64_t end = last_byte / ARGUS_PAGE_SIZE + 1u;
    if (end > page_count) end = page_count;
    for (uint64_t page = start; page < end; ++page) reserve_page(page);
}

int pmm_init(const boot_info_t *boot_info) {
    if (!boot_info || boot_info->magic != ARGUS_BOOT_INFO_MAGIC ||
        !boot_info->boot_services_exited || !boot_info->memory_map.buffer ||
        !boot_info->memory_map.descriptor_size || !boot_info->pmm_page_count ||
        !boot_info->pmm_bitmap_storage.base || !boot_info->pmm_bitmap_bytes)
        return 0;

    page_count = boot_info->pmm_page_count;
    usable_bitmap = (uint8_t *)(uintptr_t)boot_info->pmm_bitmap_storage.base;
    used_bitmap = usable_bitmap + boot_info->pmm_bitmap_bytes;
    managed_count = 0;
    free_count = 0;

    for (uint64_t i = 0; i < boot_info->pmm_bitmap_bytes; ++i) {
        usable_bitmap[i] = 0;
        used_bitmap[i] = 0xFFu;
    }

    unsigned char *p = boot_info->memory_map.buffer;
    unsigned char *end = p + boot_info->memory_map.size;
    while (p < end) {
        EFI_MEMORY_DESCRIPTOR *descriptor = (EFI_MEMORY_DESCRIPTOR *)p;
        if (descriptor->Type == EfiConventionalMemory) {
            uint64_t first = descriptor->PhysicalStart / ARGUS_PAGE_SIZE;
            uint64_t count = descriptor->NumberOfPages;
            if (first < page_count) {
                if (count > page_count - first) count = page_count - first;
                for (uint64_t i = 0; i < count; ++i) make_usable(first + i);
            }
        }
        p += boot_info->memory_map.descriptor_size;
    }

    /* Keep legacy low memory and every live handoff allocation permanently reserved. */
    physical_range_t low_memory = {0, 0x100000u};
    reserve_range(low_memory);
    reserve_range(boot_info->kernel_image);
    reserve_range(boot_info->boot_info_storage);
    reserve_range(boot_info->kernel_stack);
    reserve_range(boot_info->pmm_bitmap_storage);
    reserve_range(boot_info->memory_map_storage);

    physical_range_t framebuffer = {
        boot_info->framebuffer.base,
        boot_info->framebuffer.size
    };
    reserve_range(framebuffer);

    if (boot_info->acpi_rsdp) {
        physical_range_t rsdp_page = {
            (uint64_t)(uintptr_t)boot_info->acpi_rsdp,
            ARGUS_PAGE_SIZE
        };
        reserve_range(rsdp_page);
    }

    return managed_count != 0;
}

uint64_t pmm_alloc_page(void) {
    for (uint64_t page = 0; page < page_count; ++page) {
        if (bit_get(usable_bitmap, page) && !bit_get(used_bitmap, page)) {
            bit_set(used_bitmap, page);
            --free_count;
            return page * ARGUS_PAGE_SIZE;
        }
    }
    return 0;
}

int pmm_free_page(uint64_t address) {
    if ((address & (ARGUS_PAGE_SIZE - 1u)) != 0) return 0;
    uint64_t page = address / ARGUS_PAGE_SIZE;
    if (page >= page_count || !bit_get(usable_bitmap, page) ||
        !bit_get(used_bitmap, page))
        return 0;
    bit_clear(used_bitmap, page);
    ++free_count;
    return 1;
}

uint64_t pmm_managed_pages(void) { return managed_count; }
uint64_t pmm_free_pages(void) { return free_count; }
