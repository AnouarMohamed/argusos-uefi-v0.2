#include "kernel.h"
#include "acpi.h"
#include "apic.h"
#include "arch.h"
#include "heap.h"
#include "kconsole.h"
#include "kernel_shell.h"
#include "module.h"
#include "paging.h"
#include "pmm.h"

extern void cpu_pause(void);
extern void cpu_trigger_breakpoint(void);

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
    cpu_halt_forever();
}

static int allocator_self_test(void) {
    uint64_t before = pmm_free_pages();
    if (before < 7u) return 0;

    uint64_t a = pmm_alloc_page();
    uint64_t b = pmm_alloc_page();
    uint64_t c = pmm_alloc_page();
    int valid = a && b && c && a != b && a != c && b != c &&
                pmm_free_pages() == before - 3u;

    if (a) valid = pmm_free_page(a) && valid;
    if (b) valid = pmm_free_page(b) && valid;
    if (c) valid = pmm_free_page(c) && valid;
    if (!valid || pmm_free_pages() != before) return 0;

    uint64_t run = pmm_alloc_pages(4);
    valid = run && pmm_free_pages() == before - 4u;
    if (run) valid = pmm_release_pages(run, 4) && valid;
    return valid && pmm_free_pages() == before;
}

void kernel_main(const boot_info_t *boot_info) {
    kconsole_clear();

    kprint("ArgusOS kernel v0.7\n");
    kprint("ARGUS_KERNEL_ONLINE\n");

    if (!boot_info || boot_info->magic != ARGUS_BOOT_INFO_MAGIC ||
        boot_info->version != ARGUS_BOOT_INFO_VERSION)
        panic("invalid boot information");
    if (!boot_info->boot_services_exited)
        panic("firmware handoff incomplete");

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

    if (!allocator_self_test()) panic("physical memory allocator self-test failed");
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

    if (!heap_init(128u)) panic("kernel heap initialization failed");
    if (!heap_self_test()) panic("kernel heap self-test failed");
    kprint("HEAP_SELF_TEST_PASS\nHeap capacity: ");
    kprint_dec(heap_total_bytes());
    kprint(" bytes\n");

    if (!module_init()) panic("module ABI validation failed");
    kprint("MODULE_ABI_V1_ONLINE\nRust module: ");
    kprint(module_at(0)->name);
    kprint("\n");
    if (!module_self_test()) panic("Rust module self-test failed");
    kprint("RUST_MODULE_SELF_TEST_PASS\n");

    arch_init();
    kprint("GDT_IDT_ONLINE\n");
    if (boot_info->exception_self_test) {
        kprint("EXCEPTION_SELF_TEST_BEGIN\n");
        cpu_trigger_breakpoint();
        panic("breakpoint exception returned unexpectedly");
    }
    if (!apic_init(&acpi)) panic("local APIC initialization failed");
    kprint("LOCAL_APIC_ONLINE\n");

    arch_enable_interrupts();
    for (uint64_t spins = 0; spins < 200000000u && !apic_timer_ticks(); ++spins)
        cpu_pause();
    if (!apic_timer_ticks()) panic("local APIC timer did not fire");
    kprint("APIC_TIMER_TICK\n");

    kprint("Argus-owned stack active. Entering native kernel shell.\n");
    kernel_shell_run(boot_info, &acpi, &paging);
}
