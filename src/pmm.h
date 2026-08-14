#ifndef ARGUS_PMM_H
#define ARGUS_PMM_H

#include "boot_info.h"

int pmm_init(const boot_info_t *boot_info);
uint64_t pmm_alloc_page(void);
int pmm_free_page(uint64_t address);
uint64_t pmm_managed_pages(void);
uint64_t pmm_free_pages(void);

#endif
