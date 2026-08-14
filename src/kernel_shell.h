#ifndef ARGUS_KERNEL_SHELL_H
#define ARGUS_KERNEL_SHELL_H

#include "acpi.h"
#include "boot_info.h"
#include "paging.h"

void kernel_shell_run(
    const boot_info_t *boot_info,
    const acpi_info_t *acpi,
    const paging_info_t *paging
) __attribute__((noreturn));

#endif
