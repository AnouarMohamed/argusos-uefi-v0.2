#include "kernel_shell.h"
#include "apic.h"
#include "heap.h"
#include "input.h"
#include "kconsole.h"
#include "pmm.h"

extern void cpu_halt_forever(void) __attribute__((noreturn));
extern void cpu_trigger_breakpoint(void);
extern void cpu_wait_for_interrupt(void);

static int strings_equal(const char *a, const char *b) {
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == 0 && *b == 0;
}

static int starts_with(const char *text, const char *prefix) {
    while (*prefix)
        if (*text++ != *prefix++) return 0;
    return 1;
}

static int parse_u64(const char *text, uint64_t *value_out) {
    uint64_t value = 0;
    if (!*text) return 0;
    while (*text) {
        if (*text < '0' || *text > '9') return 0;
        uint64_t digit = (uint64_t)(*text - '0');
        if (value > (UINT64_MAX - digit) / 10u) return 0;
        value = value * 10u + digit;
        ++text;
    }
    *value_out = value;
    return 1;
}

static void print_help(void) {
    kconsole_write("\nKernel commands:\n");
    kconsole_write("  help         command list\n");
    kconsole_write("  status       kernel/platform summary\n");
    kconsole_write("  mem          physical-page statistics\n");
    kconsole_write("  heap         heap statistics\n");
    kconsole_write("  heaptest     rerun allocator checks\n");
    kconsole_write("  alloc N      allocate, verify, and free N bytes\n");
    kconsole_write("  ticks        local-APIC timer ticks\n");
    kconsole_write("  input        native input backends\n");
    kconsole_write("  clear        clear framebuffer/terminal\n");
    kconsole_write("  echo TEXT    print text\n");
    kconsole_write("  fault        trigger breakpoint diagnostics\n");
    kconsole_write("  halt         stop the CPU\n\n");
}

static void print_status(
    const boot_info_t *boot_info,
    const acpi_info_t *acpi,
    const paging_info_t *paging
) {
    kconsole_write("\nArgusOS kernel v0.6\n");
    kconsole_write("Boot Services: exited\nCPUs: ");
    kconsole_write_dec(acpi->enabled_cpu_count);
    kconsole_write("\nCR3: ");
    kconsole_write_hex(paging->root_table);
    kconsole_write("\nNX: ");
    kconsole_write(paging->nx_enabled ? "enabled" : "unavailable");
    kconsole_write("\nFramebuffer: ");
    if (boot_info->framebuffer.usable) {
        kconsole_write_dec(boot_info->framebuffer.width);
        kconsole_putc('x');
        kconsole_write_dec(boot_info->framebuffer.height);
    } else {
        kconsole_write("unavailable");
    }
    kconsole_write("\nSTATUS_OK\n");
}

static void print_memory(void) {
    kconsole_write("Managed pages: ");
    kconsole_write_dec(pmm_managed_pages());
    kconsole_write("\nFree pages: ");
    kconsole_write_dec(pmm_free_pages());
    kconsole_write("\n");
}

static void print_heap(void) {
    kconsole_write("Heap capacity: ");
    kconsole_write_dec(heap_total_bytes());
    kconsole_write(" bytes\nHeap used: ");
    kconsole_write_dec(heap_used_bytes());
    kconsole_write(" bytes\nHeap free: ");
    kconsole_write_dec(heap_free_bytes());
    kconsole_write(" bytes\nHEAP_STATUS_OK\n");
}

static void allocation_probe(const char *argument) {
    uint64_t size;
    if (!parse_u64(argument, &size) || !size) {
        kconsole_write("usage: alloc BYTES\n");
        return;
    }

    uint8_t *allocation = (uint8_t *)kmalloc(size);
    if (!allocation) {
        kconsole_write("allocation failed\n");
        return;
    }
    allocation[0] = 0xA6u;
    allocation[size - 1u] = 0x5Au;
    int valid = allocation[0] == 0xA6u && allocation[size - 1u] == 0x5Au;
    kconsole_write("Allocation: ");
    kconsole_write_hex((uint64_t)(uintptr_t)allocation);
    kconsole_write("\n");
    valid = kfree(allocation) && valid;
    kconsole_write(valid ? "ALLOC_OK\n" : "ALLOC_CORRUPTION\n");
}

static void execute_command(
    char *line,
    const boot_info_t *boot_info,
    const acpi_info_t *acpi,
    const paging_info_t *paging
) {
    if (!line[0]) return;
    if (strings_equal(line, "help")) print_help();
    else if (strings_equal(line, "status")) print_status(boot_info, acpi, paging);
    else if (strings_equal(line, "mem")) print_memory();
    else if (strings_equal(line, "heap")) print_heap();
    else if (strings_equal(line, "heaptest"))
        kconsole_write(heap_self_test() ? "HEAP_SELF_TEST_PASS\n" : "HEAP_SELF_TEST_FAIL\n");
    else if (starts_with(line, "alloc ")) allocation_probe(line + 6);
    else if (strings_equal(line, "ticks")) {
        kconsole_write("APIC ticks: ");
        kconsole_write_dec(apic_timer_ticks());
        kconsole_write("\n");
    }
    else if (strings_equal(line, "input")) {
        kconsole_write("COM1: ");
        kconsole_write(input_has_serial() ? "online" : "unavailable");
        kconsole_write("\nPS/2 keyboard: ");
        kconsole_write(input_has_keyboard() ? "online" : "unavailable");
        kconsole_write("\n");
    }
    else if (strings_equal(line, "clear")) kconsole_clear();
    else if (starts_with(line, "echo ")) { kconsole_write(line + 5); kconsole_write("\n"); }
    else if (strings_equal(line, "fault")) cpu_trigger_breakpoint();
    else if (strings_equal(line, "halt")) {
        kconsole_write("Halting ArgusOS.\n");
        cpu_halt_forever();
    }
    else kconsole_write("Unknown kernel command. Type 'help'.\n");
}

void kernel_shell_run(
    const boot_info_t *boot_info,
    const acpi_info_t *acpi,
    const paging_info_t *paging
) {
    char line[128];
    unsigned used = 0;
    input_init();

    kconsole_write("Native input initialized.\n");
    kconsole_write("KERNEL_SHELL_READY\n");
    kconsole_write("argus-kernel> ");

    for (;;) {
        int value = input_getc_nonblocking();
        if (value < 0) {
            cpu_wait_for_interrupt();
            continue;
        }

        char c = (char)value;
        if (c == '\r' || c == '\n') {
            line[used] = 0;
            kconsole_write("\n");
            execute_command(line, boot_info, acpi, paging);
            used = 0;
            kconsole_write("argus-kernel> ");
        } else if (c == '\b') {
            if (used) {
                --used;
                kconsole_write("\b \b");
            }
        } else if (c == '\t') {
            if (used + 1u < sizeof(line)) {
                line[used++] = ' ';
                kconsole_putc(' ');
            }
        } else if (c >= 32 && c <= 126 && used + 1u < sizeof(line)) {
            line[used++] = c;
            kconsole_putc(c);
        }
    }
}
