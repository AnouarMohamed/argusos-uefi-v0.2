#ifndef ARGUS_BOOT_H
#define ARGUS_BOOT_H

#include "efi.h"

EFI_STATUS boot_kernel(
    EFI_HANDLE image,
    EFI_SYSTEM_TABLE *system_table,
    int exception_self_test
);

#endif
