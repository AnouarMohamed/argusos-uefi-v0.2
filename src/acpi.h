#ifndef ARGUS_ACPI_H
#define ARGUS_ACPI_H

#include "boot_info.h"

typedef struct {
    uint64_t local_apic_address;
    uint64_t io_apic_address;
    uint32_t io_apic_gsi_base;
    uint32_t keyboard_gsi;
    uint32_t mouse_gsi;
    uint32_t enabled_cpu_count;
    uint32_t interrupt_override_count;
    uint32_t madt_flags;
    uint16_t keyboard_flags;
    uint16_t mouse_flags;
} acpi_info_t;

int acpi_init(const boot_info_t *boot_info, acpi_info_t *info);

#endif
