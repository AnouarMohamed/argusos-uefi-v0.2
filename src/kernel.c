#include "kernel.h"
#include "acpi.h"
#include "ahci.h"
#include "apic.h"
#include "arch.h"
#include "block.h"
#include "fat32.h"
#include "heap.h"
#include "input.h"
#include "kconsole.h"
#include "kernel_shell.h"
#include "module.h"
#include "paging.h"
#include "pci.h"
#include "pmm.h"
#include "process.h"
#include "ramfs.h"

extern void cpu_pause(void);
extern void cpu_trigger_breakpoint(void);

static uint64_t expected_guard_fault;

static void kputc(char c) {
    kconsole_putc(c);
}

static void kprint(const char *s) {
    kconsole_write(s);
}

static void kprint_dec(uint64_t value) {
    char buffer[21];
    unsigned used = 0;
    if (!value) { kputc('0'); return; }
    while (value) {
        buffer[used++] = (char)('0' + value % 10u);
        value /= 10u;
    }
    while (used) kputc(buffer[--used]);
}

static void kprint_hex(uint64_t value) {
    static const char hex[] = "0123456789ABCDEF";
    kprint("0x");
    for (int shift = 60; shift >= 0; shift -= 4)
        kputc(hex[(value >> shift) & 0xFu]);
}

static void panic(const char *reason) {
    kprint("\nKERNEL_PANIC: ");
    kprint(reason);
    kprint("\n");
    cpu_halt_forever();
}

void kernel_exception_panic(
    uint64_t vector,
    uint64_t error_code,
    uint64_t instruction_pointer,
    uint64_t fault_address
) {
    kprint("\nKERNEL_EXCEPTION\nVector: ");
    kprint_dec(vector);
    kprint("\nError: ");
    kprint_hex(error_code);
    kprint("\nRIP: ");
    kprint_hex(instruction_pointer);
    if (vector == 14u) {
        kprint("\nFault address: ");
        kprint_hex(fault_address);
    }
    kprint("\n");
    if (vector == 14u && fault_address == expected_guard_fault)
        kprint("STACK_GUARD_FAULT_CAUGHT\n");
    if (vector == 8u)
        kprint(arch_on_double_fault_ist()
            ? "DOUBLE_FAULT_IST_ACTIVE\n" : "DOUBLE_FAULT_IST_MISSING\n");
    cpu_halt_forever();
}

void kernel_main(const boot_info_t *boot_info) {
    kconsole_clear();

    kprint("ArgusOS kernel v0.15\n");
    kprint("ARGUS_KERNEL_ONLINE\n");

    if (!boot_info || boot_info->magic != ARGUS_BOOT_INFO_MAGIC ||
        boot_info->version != ARGUS_BOOT_INFO_VERSION)
        panic("invalid boot information");
    if (!boot_info->boot_services_exited)
        panic("firmware handoff incomplete");
    if (boot_info->kernel_self_test > ARGUS_SELF_TEST_DOUBLE_FAULT)
        panic("invalid kernel self-test request");

    kprint("BOOT_SERVICES_EXITED\n");
    kprint("Kernel image: ");
    kprint_hex(boot_info->kernel_image.base);
    kprint(" + ");
    kprint_dec(boot_info->kernel_image.size);
    kprint(" bytes\nMemory descriptors: ");
    kprint_dec(boot_info->memory_map.size / boot_info->memory_map.descriptor_size);
    kprint("\nACPI RSDP: ");
    kprint_hex((uint64_t)(uintptr_t)boot_info->acpi_rsdp);
    kprint("\n");

    if (!pmm_init(boot_info)) panic("physical memory manager initialization failed");

    kprint("Managed physical pages: ");
    kprint_dec(pmm_managed_pages());
    kprint("\nFree physical pages: ");
    kprint_dec(pmm_free_pages());
    kprint("\n");

    if (!pmm_self_test()) panic("physical memory allocator self-test failed");
    kprint("PMM_SELF_TEST_PASS\n");

    acpi_info_t acpi;
    if (!acpi_init(boot_info, &acpi)) panic("ACPI XSDT/MADT discovery failed");
    kprint("ACPI_MADT_ONLINE\nDetected CPUs: ");
    kprint_dec(acpi.enabled_cpu_count);
    kprint("\nLocal APIC: ");
    kprint_hex(acpi.local_apic_address);
    kprint("\nI/O APIC: ");
    kprint_hex(acpi.io_apic_address);
    kprint("\nInterrupt overrides: ");
    kprint_dec(acpi.interrupt_override_count);
    kprint("\n");

    paging_info_t paging;
    if (!paging_init(boot_info, &acpi, &paging))
        panic("kernel page-table construction failed");
    kprint("PAGING_ONLINE\nCR3: ");
    kprint_hex(paging.root_table);
    kprint("\nIdentity-mapped MiB: ");
    kprint_dec(paging.mapped_bytes / (1024u * 1024u));
    kprint("\nPage-table pages: ");
    kprint_dec(paging.table_pages);
    kprint("\nNX: ");
    kprint(paging.nx_enabled ? "enabled\n" : "unavailable\n");
    kprint("Stack guard: ");
    kprint_hex(paging.stack_guard_page);
    kprint("\nSTACK_GUARD_ONLINE\n");

    if (!heap_init(2048u)) panic("kernel heap initialization failed");
    if (!heap_self_test()) panic("kernel heap self-test failed");
    kprint("HEAP_SELF_TEST_PASS\nHeap capacity: ");
    kprint_dec(heap_total_bytes());
    kprint(" bytes\n");
    kprint("ALLOCATOR_HARDENING_PASS\n");

    if (!module_init()) panic("module ABI validation failed");
    kprint("MODULE_ABI_V1_ONLINE\nRust components: ");
    kprint_dec(module_count());
    kprint("\nChecksum module: ");
    kprint(module_name_at(0));
    kprint("\n");
    if (!module_self_test()) panic("Rust module self-test failed");
    kprint("RUST_MODULE_SELF_TEST_PASS\n");

    if (!ramfs_init()) panic("Rust RAMFS ABI initialization failed");
    kprint("RAMFS_ABI_V1_ONLINE\nRAMFS capacity: ");
    kprint_dec(ramfs_capacity());
    kprint(" files x ");
    kprint_dec(ramfs_max_file_size());
    kprint(" bytes\n");
    if (!ramfs_self_test()) panic("Rust RAMFS self-test failed");
    kprint("RUST_RAMFS_SELF_TEST_PASS\n");

    if (!block_init()) panic("block-device initialization failed");
    if (!pci_init() || !pci_self_test()) panic("PCI discovery failed");
    const pci_info_t *pci = pci_info();
    kprint("PCI_DISCOVERY_ONLINE\nPCI functions: ");
    kprint_dec(pci->device_count);
    kprint("\nAHCI controllers: ");
    kprint_dec(pci->ahci_count);
    kprint("\nPCI_SELF_TEST_PASS\n");

    if (pci->ahci_count &&
        ahci_init(&pci->first_ahci, &paging) &&
        ahci_self_test() && block_use_device(ahci_block_device())) {
        const ahci_info_t *storage = ahci_info();
        kprint("AHCI_SATA_ONLINE\nAHCI port: ");
        kprint_dec(storage->port);
        kprint("\nAHCI sectors: ");
        kprint_dec(storage->sector_count);
        kprint("\nAHCI_IDENTIFY_PASS\n");
    } else {
        kprint("AHCI unavailable; using memory fixture.\n");
    }

    const argus_block_device_v1_t *boot_device = block_default_device();
    if (!boot_device || !block_self_test()) panic("block-device self-test failed");
    kprint("BLOCK_DEVICE_ONLINE\nBlock device: ");
    kprint(boot_device->name);
    kprint(" (");
    kprint_dec(boot_device->sector_count);
    kprint(" sectors)\nBLOCK_DEVICE_SELF_TEST_PASS\n");

    if (!fat32_init(boot_device)) panic("Rust FAT32 mount failed");
    kprint("FAT32_ABI_V1_ONLINE\nFAT32 reader: ");
    kprint(fat32_descriptor()->name);
    kprint("\n");
    if (!fat32_self_test()) panic("Rust FAT32 self-test failed");
    kprint("RUST_FAT32_SELF_TEST_PASS\n");

    if (!arch_init()) panic("TSS/IDT initialization failed");
    kprint("GDT_IDT_ONLINE\n");
    kprint("DOUBLE_FAULT_IST_ONLINE\n");
    expected_guard_fault = paging.stack_guard_page;
    if (boot_info->kernel_self_test == ARGUS_SELF_TEST_BREAKPOINT) {
        kprint("EXCEPTION_SELF_TEST_BEGIN\n");
        cpu_trigger_breakpoint();
        panic("breakpoint exception returned unexpectedly");
    } else if (boot_info->kernel_self_test == ARGUS_SELF_TEST_STACK_GUARD) {
        kprint("STACK_GUARD_SELF_TEST_BEGIN\n");
        *(volatile uint8_t *)(uintptr_t)paging.stack_guard_page = 0xA5u;
        panic("stack guard page remained mapped");
    } else if (boot_info->kernel_self_test == ARGUS_SELF_TEST_DOUBLE_FAULT) {
        kprint("DOUBLE_FAULT_SELF_TEST_BEGIN\n");
        arch_trigger_double_fault(paging.stack_guard_page);
    }
    if (!process_init(&paging)) panic("user process initialization failed");
    if (!apic_init(&acpi)) panic("local APIC initialization failed");
    kprint("LOCAL_APIC_ONLINE\n");

    input_init(&acpi);

    arch_enable_interrupts();
    for (uint64_t spins = 0; spins < 200000000u && !apic_timer_ticks(); ++spins)
        cpu_pause();
    if (!apic_timer_ticks()) panic("local APIC timer did not fire");
    kprint("APIC_TIMER_TICK\n");

    if (!process_run_self_test()) panic("user process self-test failed");
    kprint("USER_RING3_ONLINE\n");
    kprint("SYSCALL_SYSRET_ONLINE\n");
    kprint("USER_ADDRESS_SPACE_ISOLATION_PASS\n");
    kprint("COOPERATIVE_SCHEDULER_PASS\n");
    kprint("USER_PROCESS_SELF_TEST_PASS\n");

    kprint("Argus-owned stack active. Entering native kernel shell.\n");
    kernel_shell_run(boot_info, &acpi, &paging);
}
