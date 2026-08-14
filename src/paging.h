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

int paging_init(
    const boot_info_t *boot_info,
    const acpi_info_t *acpi,
    paging_info_t *paging_info
);

#endif
