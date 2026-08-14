#include "kernel_shell.h"
#include "apic.h"
#include "ahci.h"
#include "block.h"
#include "desktop.h"
#include "fat32.h"
#include "heap.h"
#include "input.h"
#include "kconsole.h"
#include "module.h"
#include "pci.h"
#include "pmm.h"
#include "process.h"
#include "ramfs.h"
#include "serial.h"

extern void cpu_halt_forever(void) __attribute__((noreturn));
extern void cpu_trigger_breakpoint(void);
extern void cpu_wait_for_interrupt(void);

static uint8_t fat32_cat_buffer[ARGUS_FAT32_MAX_READ];

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
    kconsole_write("  memtest      rerun PMM + heap hardening tests\n");
    kconsole_write("  alloc N      allocate, verify, and free N bytes\n");
    kconsole_write("  modules      validated kernel modules\n");
    kconsole_write("  processes    user process and scheduler state\n");
    kconsole_write("  snake        C++ user game state\n");
    kconsole_write("  fs           Rust RAMFS statistics\n");
    kconsole_write("  ls           list RAMFS files\n");
    kconsole_write("  cat PATH     print a RAMFS file\n");
    kconsole_write("  write P TEXT create or replace a RAMFS file\n");
    kconsole_write("  rm PATH      remove a RAMFS file\n");
    kconsole_write("  pci          PCI discovery summary\n");
    kconsole_write("  ahci         SATA controller statistics\n");
    kconsole_write("  disks        block-device statistics\n");
    kconsole_write("  fatinfo      mounted FAT32 geometry\n");
    kconsole_write("  fatls        list FAT32 root files\n");
    kconsole_write("  fatcat PATH  print a FAT32 root file\n");
    kconsole_write("  ticks        local-APIC timer ticks\n");
    kconsole_write("  input        native input backends\n");
    kconsole_write("  irqtest      confirm IRQ keyboard command delivery\n");
    kconsole_write("  apps         surfaces and active application\n");
    kconsole_write("  focus NAME   focus console, system, files, or snake\n");
    kconsole_write("  ui           pointer and compositor state\n");
    kconsole_write("  desktop      redraw the framebuffer desktop\n");
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
    kconsole_write("\nArgusOS kernel v0.16\n");
    kconsole_write("Boot Services: exited\nCPUs: ");
    kconsole_write_dec(acpi->enabled_cpu_count);
    kconsole_write("\nCR3: ");
    kconsole_write_hex(paging->root_table);
    kconsole_write("\nNX: ");
    kconsole_write(paging->nx_enabled ? "enabled" : "unavailable");
    kconsole_write("\nStack guard: ");
    kconsole_write_hex(paging->stack_guard_page);
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

static void print_modules(void) {
    uint64_t count = module_count();
    kconsole_write("Loaded modules: ");
    kconsole_write_dec(count);
    kconsole_write("\n");
    for (uint64_t i = 0; i < count; ++i) {
        kconsole_write("  ");
        kconsole_write(module_name_at(i));
        kconsole_write(" (ABI ");
        kconsole_write_dec(module_abi_version_at(i));
        kconsole_write(")\n");
    }
    kconsole_write("MODULES_OK\n");
}

static void print_snake_status(void) {
    argus_snake_frame_v1_t frame;
    if (!process_snake_online() || !process_snake_frame(&frame)) {
        kconsole_write("Snake user app: unavailable\nSNAKE_FRAME_FAIL\n");
        return;
    }
    kconsole_write("Snake user app: C++ ring 3\nState: ");
    kconsole_write(frame.state == ARGUS_SNAKE_STATE_PLAYING
        ? "playing" : "game over");
    kconsole_write("\nScore: ");
    kconsole_write_dec(frame.score);
    kconsole_write("\nLength: ");
    kconsole_write_dec(frame.length);
    kconsole_write("\nFrame: ");
    kconsole_write_dec(frame.sequence);
    kconsole_write("\nInputs delivered: ");
    kconsole_write_dec(process_snake_input_count());
    kconsole_write(process_snake_input_count()
        ? "\nSNAKE_INPUT_OK\n" : "\nSNAKE_INPUT_IDLE\n");
    kconsole_write("CPP_USER_APP_OK\nSNAKE_FRAME_OK\n");
}

static void print_ramfs_error(int32_t status) {
    kconsole_write("RAMFS error: ");
    if (status == ARGUS_RAMFS_NOT_FOUND) kconsole_write("not found");
    else if (status == ARGUS_RAMFS_INVALID) kconsole_write("invalid path or argument");
    else if (status == ARGUS_RAMFS_NO_SPACE) kconsole_write("capacity exhausted");
    else if (status == ARGUS_RAMFS_BUFFER_TOO_SMALL) kconsole_write("buffer too small");
    else kconsole_write("ABI failure");
    kconsole_write("\n");
}

static void print_ramfs_status(void) {
    const argus_ramfs_v1_t *descriptor = ramfs_descriptor();
    kconsole_write("Driver: ");
    kconsole_write(descriptor ? descriptor->name : "unavailable");
    kconsole_write("\nFiles: ");
    kconsole_write_dec(ramfs_file_count());
    kconsole_write(" / ");
    kconsole_write_dec(ramfs_capacity());
    kconsole_write("\nMaximum file bytes: ");
    kconsole_write_dec(ramfs_max_file_size());
    kconsole_write("\nRAMFS_STATUS_OK\n");
}

static void list_ramfs(void) {
    char path[ARGUS_RAMFS_MAX_PATH + 1u];
    uint64_t path_length;
    uint64_t data_length;
    uint64_t count = ramfs_file_count();
    for (uint64_t index = 0; index < count; ++index) {
        int32_t status = ramfs_entry(
            index,
            path,
            sizeof(path),
            &path_length,
            &data_length
        );
        if (status != ARGUS_RAMFS_OK) {
            print_ramfs_error(status);
            return;
        }
        kconsole_write(path);
        kconsole_write("  ");
        kconsole_write_dec(data_length);
        kconsole_write(" bytes\n");
    }
    kconsole_write("RAMFS_LIST_OK\n");
}

static void cat_ramfs(const char *path) {
    uint8_t data[ARGUS_RAMFS_MAX_DATA];
    uint64_t length = 0;
    int32_t status = ramfs_read(path, data, sizeof(data), &length);
    if (status != ARGUS_RAMFS_OK) {
        print_ramfs_error(status);
        return;
    }
    for (uint64_t index = 0; index < length; ++index) {
        uint8_t byte = data[index];
        kconsole_putc(byte == '\n' || byte == '\t' ||
                      (byte >= 32u && byte <= 126u) ? (char)byte : '.');
    }
    if (!length || data[length - 1u] != '\n') kconsole_putc('\n');
    kconsole_write("RAMFS_CAT_OK\n");
}

static void write_ramfs(char *arguments) {
    char *separator = arguments;
    while (*separator && *separator != ' ') ++separator;
    if (separator == arguments || !*separator) {
        kconsole_write("usage: write PATH TEXT\n");
        return;
    }
    *separator = 0;
    const char *data = separator + 1;
    uint64_t length = 0;
    while (length <= ARGUS_RAMFS_MAX_DATA && data[length]) ++length;
    int32_t status = length > ARGUS_RAMFS_MAX_DATA
        ? ARGUS_RAMFS_NO_SPACE
        : ramfs_write(arguments, (const uint8_t *)data, length);
    if (status == ARGUS_RAMFS_OK) {
        desktop_refresh_apps();
        kconsole_write("RAMFS_WRITE_OK\n");
    }
    else print_ramfs_error(status);
}

static void remove_ramfs(const char *path) {
    int32_t status = ramfs_remove(path);
    if (status == ARGUS_RAMFS_OK) {
        desktop_refresh_apps();
        kconsole_write("RAMFS_REMOVE_OK\n");
    }
    else print_ramfs_error(status);
}

static void print_fat32_error(int32_t status) {
    kconsole_write("FAT32 error: ");
    if (status == ARGUS_FAT32_NOT_FOUND) kconsole_write("not found");
    else if (status == ARGUS_FAT32_INVALID) kconsole_write("invalid path or argument");
    else if (status == ARGUS_FAT32_UNSUPPORTED) kconsole_write("unsupported feature");
    else if (status == ARGUS_FAT32_CORRUPT) kconsole_write("corrupt filesystem");
    else if (status == ARGUS_FAT32_IO_ERROR) kconsole_write("block I/O failure");
    else if (status == ARGUS_FAT32_BUFFER_TOO_SMALL) kconsole_write("file too large");
    else kconsole_write("ABI failure");
    kconsole_write("\n");
}

static void print_pci(void) {
    const pci_info_t *info = pci_info();
    if (!info) {
        kconsole_write("PCI unavailable.\n");
        return;
    }
    kconsole_write("PCI functions: ");
    kconsole_write_dec(info->device_count);
    kconsole_write("\nAHCI controllers: ");
    kconsole_write_dec(info->ahci_count);
    if (info->ahci_count) {
        kconsole_write("\nFirst AHCI BDF: ");
        kconsole_write_hex(info->first_ahci.bus);
        kconsole_putc(':');
        kconsole_write_hex(info->first_ahci.device);
        kconsole_putc('.');
        kconsole_write_dec(info->first_ahci.function);
        kconsole_write("\nVendor/device: ");
        kconsole_write_hex(info->first_ahci.vendor_id);
        kconsole_putc('/');
        kconsole_write_hex(info->first_ahci.device_id);
    }
    kconsole_write("\nPCI_STATUS_OK\n");
}

static void print_ahci(void) {
    const ahci_info_t *info = ahci_info();
    if (!info) {
        kconsole_write("AHCI unavailable.\n");
        return;
    }
    kconsole_write("ABAR: ");
    kconsole_write_hex(info->abar);
    kconsole_write("\nVersion: ");
    kconsole_write_hex(info->version);
    kconsole_write("\nPort: ");
    kconsole_write_dec(info->port);
    kconsole_write("\nSectors: ");
    kconsole_write_dec(info->sector_count);
    kconsole_write("\n64-bit DMA: ");
    kconsole_write(info->supports_64bit_dma ? "yes" : "no");
    kconsole_write("\nAHCI_STATUS_OK\n");
}

static void print_disks(void) {
    const argus_block_device_v1_t *device = block_default_device();
    if (!device) {
        kconsole_write("No block device.\n");
        return;
    }
    kconsole_write("Device: ");
    kconsole_write(device->name);
    kconsole_write("\nSector bytes: ");
    kconsole_write_dec(device->sector_size);
    kconsole_write("\nSectors: ");
    kconsole_write_dec(device->sector_count);
    kconsole_write("\nBLOCK_STATUS_OK\n");
}

static void print_fat32_info(void) {
    argus_fat32_info_v1_t info;
    int32_t status = fat32_info(&info);
    if (status != ARGUS_FAT32_OK) {
        print_fat32_error(status);
        return;
    }
    kconsole_write("Bytes/sector: ");
    kconsole_write_dec(info.bytes_per_sector);
    kconsole_write("\nSectors/cluster: ");
    kconsole_write_dec(info.sectors_per_cluster);
    kconsole_write("\nData clusters: ");
    kconsole_write_dec(info.data_clusters);
    kconsole_write("\nRoot cluster: ");
    kconsole_write_dec(info.root_cluster);
    kconsole_write("\nFAT32_STATUS_OK\n");
}

static void list_fat32(void) {
    char path[ARGUS_FAT32_MAX_PATH + 1u];
    uint64_t path_length;
    uint64_t file_size;
    uint32_t attributes;
    for (uint64_t index = 0; index < 128u; ++index) {
        int32_t status = fat32_entry(
            index,
            path,
            sizeof(path),
            &path_length,
            &file_size,
            &attributes
        );
        if (status == ARGUS_FAT32_NOT_FOUND) {
            kconsole_write("FAT32_LIST_OK\n");
            return;
        }
        if (status != ARGUS_FAT32_OK) {
            print_fat32_error(status);
            return;
        }
        kconsole_write(path);
        kconsole_write("  ");
        kconsole_write_dec(file_size);
        kconsole_write(attributes & 0x10u ? " bytes [dir]\n" : " bytes\n");
    }
    kconsole_write("FAT32 error: root listing limit reached\n");
}

static void cat_fat32(const char *path) {
    uint64_t length = 0;
    int32_t status = fat32_read(
        path,
        fat32_cat_buffer,
        sizeof(fat32_cat_buffer),
        &length
    );
    if (status != ARGUS_FAT32_OK) {
        print_fat32_error(status);
        return;
    }
    for (uint64_t index = 0; index < length; ++index) {
        uint8_t byte = fat32_cat_buffer[index];
        kconsole_putc(byte == '\n' || byte == '\t' ||
                      (byte >= 32u && byte <= 126u) ? (char)byte : '.');
    }
    if (!length || fat32_cat_buffer[length - 1u] != '\n') kconsole_putc('\n');
    kconsole_write("FAT32_CAT_OK\n");
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
    else if (strings_equal(line, "memtest"))
        kconsole_write(pmm_self_test() && heap_self_test()
            ? "ALLOCATOR_HARDENING_PASS\n" : "ALLOCATOR_HARDENING_FAIL\n");
    else if (starts_with(line, "alloc ")) allocation_probe(line + 6);
    else if (strings_equal(line, "modules")) print_modules();
    else if (strings_equal(line, "processes")) {
        kconsole_write("Processes: ");
        kconsole_write_dec(process_count());
        kconsole_write("\nCompleted: ");
        kconsole_write_dec(process_completed_count());
        kconsole_write("\nSyscalls: ");
        kconsole_write_dec(process_syscall_count());
        kconsole_write("\nYields: ");
        kconsole_write_dec(process_yield_count());
        kconsole_write("\nContext switches: ");
        kconsole_write_dec(process_context_switch_count());
        kconsole_write("\nAddress spaces: ");
        kconsole_write(process_address_space_isolated()
            ? "isolated" : "invalid");
        kconsole_write("\nC++ app: ");
        kconsole_write(process_snake_online() ? "snake (ring 3)" : "unavailable");
        kconsole_write(process_scheduler_online()
            ? "\nUSERSPACE_STATUS_OK\n" : "\nUSERSPACE_STATUS_FAIL\n");
    }
    else if (strings_equal(line, "snake")) print_snake_status();
    else if (strings_equal(line, "fs")) print_ramfs_status();
    else if (strings_equal(line, "ls")) list_ramfs();
    else if (starts_with(line, "cat ")) cat_ramfs(line + 4);
    else if (starts_with(line, "write ")) write_ramfs(line + 6);
    else if (starts_with(line, "rm ")) remove_ramfs(line + 3);
    else if (strings_equal(line, "pci")) print_pci();
    else if (strings_equal(line, "ahci")) print_ahci();
    else if (strings_equal(line, "disks")) print_disks();
    else if (strings_equal(line, "fatinfo")) print_fat32_info();
    else if (strings_equal(line, "fatls")) list_fat32();
    else if (starts_with(line, "fatcat ")) cat_fat32(line + 7);
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
        kconsole_write("\nPS/2 mode: ");
        kconsole_write(input_keyboard_uses_irq() ? "I/O APIC IRQ" : "polling fallback");
        kconsole_write("\nDropped keys: ");
        kconsole_write_dec(input_dropped_keys());
        kconsole_write("\nPS/2 mouse: ");
        kconsole_write(input_has_pointer() ? "online" : "unavailable");
        kconsole_write("\nMouse mode: ");
        if (!input_has_pointer()) kconsole_write("unavailable");
        else kconsole_write(input_pointer_uses_irq()
            ? "I/O APIC IRQ" : "polling fallback");
        kconsole_write("\nMouse packets: ");
        kconsole_write_dec(input_pointer_packets());
        kconsole_write("\nDropped mouse events: ");
        kconsole_write_dec(input_dropped_pointer_events());
        kconsole_write("\n");
    }
    else if (strings_equal(line, "irqtest"))
        kconsole_write(input_keyboard_uses_irq()
            ? "PS2_IRQ_INPUT_OK\n" : "PS2_IRQ_INPUT_UNAVAILABLE\n");
    else if (strings_equal(line, "apps")) {
        kconsole_write("Surfaces: ");
        kconsole_write_dec(desktop_surface_count());
        kconsole_write("\nActive app: ");
        kconsole_write(desktop_active_app());
        kconsole_write("\nCompositor frames: ");
        kconsole_write_dec(desktop_compositor_frames());
        kconsole_write(desktop_compositor_valid()
            ? "\nCOMPOSITOR_APPS_OK\n" : "\nCOMPOSITOR_INVALID\n");
    }
    else if (starts_with(line, "focus "))
        kconsole_write(desktop_focus_app(line + 6)
            ? "APP_FOCUS_OK\n" : "usage: focus console|system|files|snake\n");
    else if (strings_equal(line, "ui")) {
        kconsole_write("Pointer: ");
        kconsole_write(input_has_pointer() ? "online" : "unavailable");
        kconsole_write("\nPointer position: ");
        kconsole_write_dec(desktop_pointer_x());
        kconsole_putc(',');
        kconsole_write_dec(desktop_pointer_y());
        kconsole_write("\nWindow position: ");
        kconsole_write_dec(desktop_window_x());
        kconsole_putc(',');
        kconsole_write_dec(desktop_window_y());
        kconsole_write("\nWindow moves: ");
        kconsole_write_dec(desktop_drag_moves());
        kconsole_write(desktop_drag_moves()
            ? "\nDESKTOP_DRAG_OK\n" : "\nDESKTOP_POINTER_IDLE\n");
        kconsole_write("Active app: ");
        kconsole_write(desktop_active_app());
        kconsole_write("\nCompositor frames: ");
        kconsole_write_dec(desktop_compositor_frames());
        kconsole_write("\nDamage pixels: ");
        kconsole_write_dec(desktop_damage_pixels());
        kconsole_write("\nLast damage rects: ");
        kconsole_write_dec(desktop_last_damage_count());
        kconsole_write(desktop_compositor_valid() && desktop_damage_pixels()
            ? "\nCOMPOSITOR_DAMAGE_OK\n" : "\nCOMPOSITOR_DAMAGE_FAIL\n");
    }
    else if (strings_equal(line, "desktop"))
        kconsole_write(desktop_redraw()
            ? "DESKTOP_UI_REDRAWN\n" : "DESKTOP_UI_UNAVAILABLE\n");
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

    int desktop_online = desktop_init();
    desktop_pointer_set_enabled(desktop_online && input_has_pointer());
    serial_write(desktop_online ? "DESKTOP_UI_ONLINE\n" : "DESKTOP_UI_UNAVAILABLE\n");
    if (desktop_online) {
        serial_write("SURFACE_SELF_TEST_PASS\n");
        serial_write("COMPOSITOR_ONLINE\n");
        serial_write("DESKTOP_APPS_ONLINE\n");
        serial_write(process_snake_online()
            ? "SNAKE_APP_ONLINE\n" : "SNAKE_APP_UNAVAILABLE\n");
    }
    serial_write(input_keyboard_uses_irq()
        ? "PS2_IRQ_ONLINE\n" : "PS2_POLLING_FALLBACK\n");
    if (!input_has_pointer()) serial_write("PS2_MOUSE_UNAVAILABLE\n");
    else serial_write(input_pointer_uses_irq()
        ? "PS2_MOUSE_IRQ_ONLINE\n" : "PS2_MOUSE_POLLING_FALLBACK\n");
    serial_write("KERNEL_SHELL_READY\n");
    kconsole_write("argus-kernel> ");
    (void)desktop_present();

    for (;;) {
        int value = input_serial_getc_nonblocking();
        if (value < 0) {
            int keyboard_value = input_keyboard_getc_nonblocking();
            if (keyboard_value >= 0 &&
                desktop_key_event((uint8_t)keyboard_value)) {
                (void)process_run_ready_once();
                desktop_tick(apic_timer_ticks());
                continue;
            }
            value = keyboard_value;
        }
        if (value < 0) {
            input_pointer_event_t pointer_event;
            if (input_pointer_event_nonblocking(&pointer_event)) {
                desktop_pointer_event(
                    pointer_event.dx,
                    pointer_event.dy,
                    pointer_event.buttons
                );
                (void)process_run_ready_once();
                desktop_tick(apic_timer_ticks());
                continue;
            }
            (void)process_run_ready_once();
            desktop_tick(apic_timer_ticks());
            cpu_wait_for_interrupt();
            continue;
        }

        desktop_pointer_hide();
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
        (void)process_run_ready_once();
        desktop_tick(apic_timer_ticks());
        (void)desktop_present();
    }
}
