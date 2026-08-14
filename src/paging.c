#include "paging.h"
#include "pmm.h"

#define PAGE_PRESENT  (1ULL << 0)
#define PAGE_WRITABLE (1ULL << 1)
#define PAGE_USER     (1ULL << 2)
#define PAGE_PWT      (1ULL << 3)
#define PAGE_PCD      (1ULL << 4)
#define PAGE_LARGE    (1ULL << 7)
#define PAGE_NX       (1ULL << 63)
#define PAGE_ADDRESS_MASK 0x000FFFFFFFFFF000ULL
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

static int unmap_4k_page(uint64_t root, uint64_t physical) {
    uint64_t *pml4 = (uint64_t *)(uintptr_t)root;
    unsigned pml4_index = (unsigned)((physical >> 39) & 0x1FFu);
    unsigned pdpt_index = (unsigned)((physical >> 30) & 0x1FFu);
    unsigned pd_index = (unsigned)((physical >> 21) & 0x1FFu);
    unsigned pt_index = (unsigned)((physical >> 12) & 0x1FFu);

    if (!(pml4[pml4_index] & PAGE_PRESENT)) return 0;
    uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4[pml4_index] & PAGE_ADDRESS_MASK);
    if (!(pdpt[pdpt_index] & PAGE_PRESENT)) return 0;
    uint64_t *pd = (uint64_t *)(uintptr_t)(pdpt[pdpt_index] & PAGE_ADDRESS_MASK);
    if (!(pd[pd_index] & PAGE_PRESENT)) return 0;

    uint64_t *pt;
    if (pd[pd_index] & PAGE_LARGE) {
        uint64_t large_entry = pd[pd_index];
        uint64_t large_base = large_entry & 0x000FFFFFFFE00000ULL;
        uint64_t leaf_flags = large_entry & ~PAGE_ADDRESS_MASK;
        leaf_flags &= ~PAGE_LARGE;
        uint64_t pt_address = allocate_table();
        if (!pt_address) return 0;
        pt = (uint64_t *)(uintptr_t)pt_address;
        for (unsigned i = 0; i < 512; ++i)
            pt[i] = (large_base + (uint64_t)i * ARGUS_PAGE_SIZE) | leaf_flags;
        pd[pd_index] = pt_address | PAGE_PRESENT | PAGE_WRITABLE;
    } else {
        pt = (uint64_t *)(uintptr_t)(pd[pd_index] & PAGE_ADDRESS_MASK);
    }

    pt[pt_index] = 0;
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
    if (boot_info->kernel_stack.size < 2u * ARGUS_PAGE_SIZE ||
        boot_info->kernel_stack_guard.base != boot_info->kernel_stack.base ||
        boot_info->kernel_stack_guard.size != ARGUS_PAGE_SIZE ||
        (boot_info->kernel_stack_guard.base & (ARGUS_PAGE_SIZE - 1u)) != 0 ||
        !overlaps(boot_info->kernel_stack.base, boot_info->kernel_stack.size,
                  boot_info->kernel_stack_guard.base,
                  boot_info->kernel_stack_guard.size))
        return 0;

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

    if (!unmap_4k_page(root, boot_info->kernel_stack_guard.base)) return 0;

    if (nx) cpu_enable_nx();
    cpu_enable_write_protect();
    cpu_load_cr3(root);

    paging_info->root_table = root;
    paging_info->mapped_bytes = maximum;
    paging_info->table_pages = table_pages;
    paging_info->stack_guard_page = boot_info->kernel_stack_guard.base;
    paging_info->nx_enabled = nx;
    return 1;
}

int paging_mark_mmio(
    const paging_info_t *paging_info,
    uint64_t physical,
    uint64_t size
) {
    if (!paging_info || !paging_info->root_table || !size ||
        physical >= paging_info->mapped_bytes ||
        size - 1u > UINT64_MAX - physical ||
        physical + size > paging_info->mapped_bytes)
        return 0;

    uint64_t first = physical & ~(LARGE_PAGE_SIZE - 1u);
    uint64_t last = (physical + size - 1u) & ~(LARGE_PAGE_SIZE - 1u);
    uint64_t *pml4 = (uint64_t *)(uintptr_t)paging_info->root_table;
    for (uint64_t address = first;; address += LARGE_PAGE_SIZE) {
        unsigned pml4_index = (unsigned)((address >> 39) & 0x1FFu);
        unsigned pdpt_index = (unsigned)((address >> 30) & 0x1FFu);
        unsigned pd_index = (unsigned)((address >> 21) & 0x1FFu);
        if (!(pml4[pml4_index] & PAGE_PRESENT)) return 0;
        uint64_t *pdpt = (uint64_t *)(uintptr_t)
            (pml4[pml4_index] & PAGE_ADDRESS_MASK);
        if (!(pdpt[pdpt_index] & PAGE_PRESENT)) return 0;
        uint64_t *pd = (uint64_t *)(uintptr_t)
            (pdpt[pdpt_index] & PAGE_ADDRESS_MASK);
        if (!(pd[pd_index] & PAGE_PRESENT)) return 0;

        if (pd[pd_index] & PAGE_LARGE) {
            pd[pd_index] |= PAGE_PCD | PAGE_PWT;
        } else {
            uint64_t *pt = (uint64_t *)(uintptr_t)
                (pd[pd_index] & PAGE_ADDRESS_MASK);
            for (unsigned index = 0; index < 512u; ++index)
                if (pt[index] & PAGE_PRESENT)
                    pt[index] |= PAGE_PCD | PAGE_PWT;
        }
        if (address == last) break;
    }
    cpu_load_cr3(paging_info->root_table);
    return 1;
}

static uint64_t allocate_user_table(paging_user_space_t *user_space) {
    if (!user_space ||
        user_space->table_page_count >= ARGUS_USER_TABLE_MAX_PAGES)
        return 0;
    uint64_t address = pmm_alloc_page();
    if (!address) return 0;
    zero_page(address);
    user_space->table_pages[user_space->table_page_count++] = address;
    return address;
}

int paging_user_space_create(
    const paging_info_t *kernel_space,
    paging_user_space_t *user_space
) {
    if (!kernel_space || !kernel_space->root_table || !user_space) return 0;
    *user_space = (paging_user_space_t){0};
    user_space->nx_enabled = kernel_space->nx_enabled;

    uint64_t root = allocate_user_table(user_space);
    if (!root) return 0;
    uint64_t *destination = (uint64_t *)(uintptr_t)root;
    const uint64_t *source =
        (const uint64_t *)(uintptr_t)kernel_space->root_table;
    for (unsigned index = 0; index < 512u; ++index)
        destination[index] = source[index];

    unsigned user_index = 0;
    for (unsigned index = 1u; index < 256u; ++index) {
        if (!(destination[index] & PAGE_PRESENT)) {
            user_index = index;
            break;
        }
    }
    if (!user_index) {
        paging_user_space_destroy(user_space);
        return 0;
    }
    user_space->root_table = root;
    user_space->user_base = (uint64_t)user_index << 39;
    return 1;
}

int paging_user_map_page(
    paging_user_space_t *user_space,
    uint64_t virtual_address,
    uint64_t physical_address,
    int writable,
    int executable
) {
    if (!user_space || !user_space->root_table ||
        (virtual_address & (ARGUS_PAGE_SIZE - 1u)) ||
        (physical_address & (ARGUS_PAGE_SIZE - 1u)) ||
        virtual_address < user_space->user_base ||
        virtual_address >= user_space->user_base + (1ULL << 39))
        return 0;

    unsigned pml4_index = (unsigned)((virtual_address >> 39) & 0x1FFu);
    unsigned pdpt_index = (unsigned)((virtual_address >> 30) & 0x1FFu);
    unsigned pd_index = (unsigned)((virtual_address >> 21) & 0x1FFu);
    unsigned pt_index = (unsigned)((virtual_address >> 12) & 0x1FFu);
    uint64_t *pml4 = (uint64_t *)(uintptr_t)user_space->root_table;

    if (!(pml4[pml4_index] & PAGE_PRESENT)) {
        uint64_t table = allocate_user_table(user_space);
        if (!table) return 0;
        pml4[pml4_index] = table | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    }
    if (!(pml4[pml4_index] & PAGE_USER)) return 0;
    uint64_t *pdpt = (uint64_t *)(uintptr_t)
        (pml4[pml4_index] & PAGE_ADDRESS_MASK);

    if (!(pdpt[pdpt_index] & PAGE_PRESENT)) {
        uint64_t table = allocate_user_table(user_space);
        if (!table) return 0;
        pdpt[pdpt_index] = table | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    }
    if (!(pdpt[pdpt_index] & PAGE_USER) || (pdpt[pdpt_index] & PAGE_LARGE))
        return 0;
    uint64_t *pd = (uint64_t *)(uintptr_t)
        (pdpt[pdpt_index] & PAGE_ADDRESS_MASK);

    if (!(pd[pd_index] & PAGE_PRESENT)) {
        uint64_t table = allocate_user_table(user_space);
        if (!table) return 0;
        pd[pd_index] = table | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    }
    if (!(pd[pd_index] & PAGE_USER) || (pd[pd_index] & PAGE_LARGE)) return 0;
    uint64_t *pt = (uint64_t *)(uintptr_t)(pd[pd_index] & PAGE_ADDRESS_MASK);
    if (pt[pt_index] & PAGE_PRESENT) return 0;

    uint64_t flags = PAGE_PRESENT | PAGE_USER;
    if (writable) flags |= PAGE_WRITABLE;
    if (!executable && user_space->nx_enabled) flags |= PAGE_NX;
    pt[pt_index] = physical_address | flags;
    return 1;
}

int paging_user_translate(
    const paging_user_space_t *user_space,
    uint64_t virtual_address,
    uint64_t *physical_address,
    uint64_t *entry_flags
) {
    if (!user_space || !user_space->root_table) return 0;
    unsigned pml4_index = (unsigned)((virtual_address >> 39) & 0x1FFu);
    unsigned pdpt_index = (unsigned)((virtual_address >> 30) & 0x1FFu);
    unsigned pd_index = (unsigned)((virtual_address >> 21) & 0x1FFu);
    unsigned pt_index = (unsigned)((virtual_address >> 12) & 0x1FFu);
    const uint64_t *pml4 =
        (const uint64_t *)(uintptr_t)user_space->root_table;
    if (!(pml4[pml4_index] & PAGE_PRESENT) ||
        !(pml4[pml4_index] & PAGE_USER))
        return 0;
    const uint64_t *pdpt = (const uint64_t *)(uintptr_t)
        (pml4[pml4_index] & PAGE_ADDRESS_MASK);
    if (!(pdpt[pdpt_index] & PAGE_PRESENT) ||
        !(pdpt[pdpt_index] & PAGE_USER) || (pdpt[pdpt_index] & PAGE_LARGE))
        return 0;
    const uint64_t *pd = (const uint64_t *)(uintptr_t)
        (pdpt[pdpt_index] & PAGE_ADDRESS_MASK);
    if (!(pd[pd_index] & PAGE_PRESENT) || !(pd[pd_index] & PAGE_USER) ||
        (pd[pd_index] & PAGE_LARGE))
        return 0;
    const uint64_t *pt =
        (const uint64_t *)(uintptr_t)(pd[pd_index] & PAGE_ADDRESS_MASK);
    uint64_t entry = pt[pt_index];
    if (!(entry & PAGE_PRESENT) || !(entry & PAGE_USER)) return 0;
    if (physical_address)
        *physical_address = (entry & PAGE_ADDRESS_MASK) |
            (virtual_address & (ARGUS_PAGE_SIZE - 1u));
    if (entry_flags) *entry_flags = entry & ~PAGE_ADDRESS_MASK;
    return 1;
}

void paging_user_activate(const paging_user_space_t *user_space) {
    if (user_space && user_space->root_table)
        cpu_load_cr3(user_space->root_table);
}

void paging_kernel_activate(const paging_info_t *kernel_space) {
    if (kernel_space && kernel_space->root_table)
        cpu_load_cr3(kernel_space->root_table);
}

void paging_user_space_destroy(paging_user_space_t *user_space) {
    if (!user_space) return;
    for (uint32_t index = user_space->table_page_count; index > 0u; --index)
        (void)pmm_free_page(user_space->table_pages[index - 1u]);
    *user_space = (paging_user_space_t){0};
}
