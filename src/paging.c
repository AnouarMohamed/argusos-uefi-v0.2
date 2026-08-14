#include "paging.h"
#include "pmm.h"

#define PAGE_PRESENT  (1ULL << 0)
#define PAGE_WRITABLE (1ULL << 1)
#define PAGE_PWT      (1ULL << 3)
#define PAGE_PCD      (1ULL << 4)
#define PAGE_LARGE    (1ULL << 7)
#define PAGE_NX       (1ULL << 63)
#define LARGE_PAGE_SIZE 0x200000ULL

extern int cpu_has_nx(void);
extern void cpu_enable_nx(void);
extern void cpu_enable_write_protect(void);
extern void cpu_load_cr3(uint64_t root);

static uint64_t table_pages;

static void zero_page(uint64_t address) {
    uint64_t *page = (uint64_t *)(uintptr_t)address;
    for (unsigned i = 0; i < 512; ++i) page[i] = 0;
}

static uint64_t allocate_table(void) {
    uint64_t address = pmm_alloc_page();
    if (!address) return 0;
    zero_page(address);
    ++table_pages;
    return address;
}

static int overlaps(uint64_t base, uint64_t size, uint64_t other_base, uint64_t other_size) {
    if (!size || !other_size) return 0;
    uint64_t end = base > UINT64_MAX - size ? UINT64_MAX : base + size;
    uint64_t other_end = other_base > UINT64_MAX - other_size
        ? UINT64_MAX : other_base + other_size;
    return base < other_end && other_base < end;
}

static int range_is_mmio(
    const boot_info_t *boot_info,
    const acpi_info_t *acpi,
    uint64_t base
) {
    if (overlaps(base, LARGE_PAGE_SIZE,
                 boot_info->framebuffer.base, boot_info->framebuffer.size) ||
        overlaps(base, LARGE_PAGE_SIZE, acpi->local_apic_address, ARGUS_PAGE_SIZE) ||
        overlaps(base, LARGE_PAGE_SIZE, acpi->io_apic_address, ARGUS_PAGE_SIZE))
        return 1;

    unsigned char *p = boot_info->memory_map.buffer;
    unsigned char *end = p + boot_info->memory_map.size;
    while (p < end) {
        EFI_MEMORY_DESCRIPTOR *descriptor = (EFI_MEMORY_DESCRIPTOR *)p;
        if (descriptor->Type == EfiMemoryMappedIO &&
            descriptor->NumberOfPages <= UINT64_MAX / ARGUS_PAGE_SIZE &&
            overlaps(base, LARGE_PAGE_SIZE,
                     descriptor->PhysicalStart,
                     descriptor->NumberOfPages * ARGUS_PAGE_SIZE))
            return 1;
        p += boot_info->memory_map.descriptor_size;
    }
    return 0;
}

static int map_large_page(
    uint64_t root,
    uint64_t physical,
    uint64_t flags
) {
    uint64_t *pml4 = (uint64_t *)(uintptr_t)root;
    unsigned pml4_index = (unsigned)((physical >> 39) & 0x1FFu);
    unsigned pdpt_index = (unsigned)((physical >> 30) & 0x1FFu);
    unsigned pd_index = (unsigned)((physical >> 21) & 0x1FFu);

    if (!(pml4[pml4_index] & PAGE_PRESENT)) {
        uint64_t pdpt_address = allocate_table();
        if (!pdpt_address) return 0;
        pml4[pml4_index] = pdpt_address | PAGE_PRESENT | PAGE_WRITABLE;
    }
    uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4[pml4_index] & 0x000FFFFFFFFFF000ULL);
    if (!(pdpt[pdpt_index] & PAGE_PRESENT)) {
        uint64_t pd_address = allocate_table();
        if (!pd_address) return 0;
        pdpt[pdpt_index] = pd_address | PAGE_PRESENT | PAGE_WRITABLE;
    }
    uint64_t *pd = (uint64_t *)(uintptr_t)(pdpt[pdpt_index] & 0x000FFFFFFFFFF000ULL);
    pd[pd_index] = physical | flags | PAGE_PRESENT | PAGE_WRITABLE | PAGE_LARGE;
    return 1;
}

static uint64_t range_end(physical_range_t range) {
    if (range.base > UINT64_MAX - range.size) return UINT64_MAX;
    return range.base + range.size;
}

int paging_init(
    const boot_info_t *boot_info,
    const acpi_info_t *acpi,
    paging_info_t *paging_info
) {
    uint64_t maximum = boot_info->pmm_page_count * ARGUS_PAGE_SIZE;
    uint64_t candidates[] = {
        range_end(boot_info->kernel_image),
        range_end(boot_info->boot_info_storage),
        range_end(boot_info->kernel_stack),
        range_end(boot_info->pmm_bitmap_storage),
        range_end(boot_info->memory_map_storage),
        range_end((physical_range_t){
            boot_info->framebuffer.base, boot_info->framebuffer.size}),
        acpi->local_apic_address + ARGUS_PAGE_SIZE,
        acpi->io_apic_address + ARGUS_PAGE_SIZE
    };
    for (unsigned i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i)
        if (candidates[i] > maximum) maximum = candidates[i];
    if (!maximum || maximum > (1ULL << 47)) return 0;
    if (maximum > UINT64_MAX - (LARGE_PAGE_SIZE - 1u)) return 0;
    maximum = (maximum + LARGE_PAGE_SIZE - 1u) & ~(LARGE_PAGE_SIZE - 1u);

    table_pages = 0;
    uint64_t root = allocate_table();
    if (!root) return 0;
    int nx = cpu_has_nx();

    for (uint64_t address = 0; address < maximum; address += LARGE_PAGE_SIZE) {
        uint64_t flags = 0;
        if (nx && !overlaps(address, LARGE_PAGE_SIZE,
                            boot_info->kernel_image.base,
                            boot_info->kernel_image.size))
            flags |= PAGE_NX;
        if (range_is_mmio(boot_info, acpi, address)) flags |= PAGE_PCD | PAGE_PWT;
        if (!map_large_page(root, address, flags)) return 0;
    }

    if (nx) cpu_enable_nx();
    cpu_enable_write_protect();
    cpu_load_cr3(root);

    paging_info->root_table = root;
    paging_info->mapped_bytes = maximum;
    paging_info->table_pages = table_pages;
    paging_info->nx_enabled = nx;
    return 1;
}
