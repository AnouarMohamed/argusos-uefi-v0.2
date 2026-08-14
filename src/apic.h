#ifndef ARGUS_APIC_H
#define ARGUS_APIC_H

#include "acpi.h"

#define ARGUS_APIC_TIMER_VECTOR 0x40u
#define ARGUS_PS2_KEYBOARD_VECTOR 0x41u
#define ARGUS_APIC_SPURIOUS_VECTOR 0xFFu

int apic_init(const acpi_info_t *acpi);
int apic_route_gsi(
    const acpi_info_t *acpi,
    uint32_t gsi,
    uint16_t flags,
    uint8_t vector
);
void apic_eoi(void);
void apic_timer_interrupt(void);
uint64_t apic_timer_ticks(void);

#endif
