#include "acpi.h"

typedef struct __attribute__((packed)) {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} rsdp_t;

typedef struct __attribute__((packed)) {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} sdt_header_t;

typedef struct __attribute__((packed)) {
    sdt_header_t header;
    uint32_t local_apic_address;
    uint32_t flags;
    uint8_t entries[];
} madt_t;

static int bytes_equal(const char *a, const char *b, unsigned count) {
    for (unsigned i = 0; i < count; ++i)
        if (a[i] != b[i]) return 0;
    return 1;
}

static int checksum_ok(const void *address, uint32_t length) {
    const uint8_t *bytes = (const uint8_t *)address;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < length; ++i) sum = (uint8_t)(sum + bytes[i]);
    return sum == 0;
}

static int sdt_valid(const sdt_header_t *header) {
    return header && header->length >= sizeof(sdt_header_t) &&
           header->length <= 0x100000u && checksum_ok(header, header->length);
}

static const sdt_header_t *find_madt(const rsdp_t *rsdp) {
    if (rsdp->revision < 2 || !rsdp->xsdt_address) return 0;
    const sdt_header_t *xsdt =
        (const sdt_header_t *)(uintptr_t)rsdp->xsdt_address;
    if (!sdt_valid(xsdt) || !bytes_equal(xsdt->signature, "XSDT", 4)) return 0;

    uint32_t payload = xsdt->length - (uint32_t)sizeof(sdt_header_t);
    if (payload % sizeof(uint64_t) != 0) return 0;
    const uint64_t *entries = (const uint64_t *)(const void *)(xsdt + 1);
    uint32_t count = payload / (uint32_t)sizeof(uint64_t);
    for (uint32_t i = 0; i < count; ++i) {
        const sdt_header_t *candidate =
            (const sdt_header_t *)(uintptr_t)entries[i];
        if (sdt_valid(candidate) && bytes_equal(candidate->signature, "APIC", 4))
            return candidate;
    }
    return 0;
}

int acpi_init(const boot_info_t *boot_info, acpi_info_t *info) {
    info->local_apic_address = 0;
    info->io_apic_address = 0;
    info->io_apic_gsi_base = 0;
    info->keyboard_gsi = 1u;
    info->mouse_gsi = 12u;
    info->enabled_cpu_count = 0;
    info->interrupt_override_count = 0;
    info->madt_flags = 0;
    info->keyboard_flags = 0;
    info->mouse_flags = 0;

    if (!boot_info || !boot_info->acpi_rsdp) return 0;
    const rsdp_t *rsdp = (const rsdp_t *)boot_info->acpi_rsdp;
    if (!bytes_equal(rsdp->signature, "RSD PTR ", 8) ||
        !checksum_ok(rsdp, 20) || rsdp->revision < 2 ||
        rsdp->length < sizeof(rsdp_t) || rsdp->length > 4096u ||
        !checksum_ok(rsdp, rsdp->length))
        return 0;

    const sdt_header_t *header = find_madt(rsdp);
    if (!header || header->length < sizeof(madt_t)) return 0;
    const madt_t *madt = (const madt_t *)header;
    info->local_apic_address = madt->local_apic_address;
    info->madt_flags = madt->flags;

    const uint8_t *entry = madt->entries;
    const uint8_t *end = (const uint8_t *)madt + madt->header.length;
    while (entry + 2u <= end) {
        uint8_t type = entry[0];
        uint8_t length = entry[1];
        if (length < 2u || entry + length > end) return 0;

        if (type == 0u && length >= 8u) {
            uint32_t flags = *(const uint32_t *)(const void *)(entry + 4u);
            if (flags & 3u) ++info->enabled_cpu_count;
        } else if (type == 1u && length >= 12u && !info->io_apic_address) {
            info->io_apic_address =
                *(const uint32_t *)(const void *)(entry + 4u);
            info->io_apic_gsi_base =
                *(const uint32_t *)(const void *)(entry + 8u);
        } else if (type == 2u && length >= 10u) {
            ++info->interrupt_override_count;
            if (entry[2] == 0u) {
                uint32_t source = entry[3];
                uint32_t gsi = *(const uint32_t *)(const void *)(entry + 4u);
                uint16_t flags = *(const uint16_t *)(const void *)(entry + 8u);
                if (source == 1u) {
                    info->keyboard_gsi = gsi;
                    info->keyboard_flags = flags;
                } else if (source == 12u) {
                    info->mouse_gsi = gsi;
                    info->mouse_flags = flags;
                }
            }
        } else if (type == 5u && length >= 12u) {
            info->local_apic_address =
                *(const uint64_t *)(const void *)(entry + 4u);
        }
        entry += length;
    }

    return info->local_apic_address != 0 && info->enabled_cpu_count != 0;
}
