#ifndef ARGUS_BOOT_H
#define ARGUS_BOOT_H

#include "boot_info.h"

EFI_STATUS boot_kernel(
    EFI_HANDLE image,
    EFI_SYSTEM_TABLE *system_table,
    uint32_t kernel_self_test
);

#endif
