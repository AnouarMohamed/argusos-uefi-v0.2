#ifndef ARGUS_PAGING_H
#define ARGUS_PAGING_H

#include "acpi.h"
#include "boot_info.h"

typedef struct {
    uint64_t root_table;
    uint64_t mapped_bytes;
    uint64_t table_pages;
    uint64_t stack_guard_page;
    int nx_enabled;
} paging_info_t;

#define ARGUS_USER_TABLE_MAX_PAGES 16u
#define ARGUS_USER_BASE 0x0000008000000000ULL

typedef struct {
    uint64_t root_table;
    uint64_t user_base;
    uint64_t table_pages[ARGUS_USER_TABLE_MAX_PAGES];
    uint32_t table_page_count;
    int nx_enabled;
} paging_user_space_t;

int paging_init(
    const boot_info_t *boot_info,
    const acpi_info_t *acpi,
    paging_info_t *paging_info
);
int paging_mark_mmio(
    const paging_info_t *paging_info,
    uint64_t physical,
    uint64_t size
);
int paging_user_space_create(
    const paging_info_t *kernel_space,
    paging_user_space_t *user_space
);
int paging_user_map_page(
    paging_user_space_t *user_space,
    uint64_t virtual_address,
    uint64_t physical_address,
    int writable,
    int executable
);
int paging_user_translate(
    const paging_user_space_t *user_space,
    uint64_t virtual_address,
    uint64_t *physical_address,
    uint64_t *entry_flags
);
void paging_user_activate(const paging_user_space_t *user_space);
void paging_kernel_activate(const paging_info_t *kernel_space);
void paging_user_space_destroy(paging_user_space_t *user_space);

#endif
