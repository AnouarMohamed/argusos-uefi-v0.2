#include "efi.h"
#include "console.h"
#include "gop.h"

#define ARGUS_VERSION "0.3"

static EFI_SYSTEM_TABLE *ST;
static EFI_SIMPLE_TEXT_INPUT_PROTOCOL *IN;

static char linebuf[128];

typedef struct {
    unsigned char *buffer;
    UINTN size;
    UINTN key;
    UINTN descriptor_size;
    uint32_t descriptor_version;
} memory_map_t;

extern void cpu_vendor(char out[13]);
extern uint64_t cpu_read_tsc(void);
extern void cpu_cpuid1(uint32_t *ecx_out, uint32_t *edx_out);

static void print16(CHAR16 *s) { console_write16(s); }

static void print(const char *s) { console_write(s); }

static void putc_ascii(char c) { console_putc(c); }

static int streq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == 0 && *b == 0;
}

static int starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s++ != *prefix++) return 0;
    }
    return 1;
}

static void print_dec_u64(uint64_t v) {
    char b[21];
    unsigned i = 0;
    if (!v) { putc_ascii('0'); return; }
    while (v && i < sizeof(b)) {
        b[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i) putc_ascii(b[--i]);
}

static void print_hex_u64(uint64_t v, unsigned digits) {
    static const char hex[] = "0123456789ABCDEF";
    for (int shift = (int)(digits - 1) * 4; shift >= 0; shift -= 4)
        putc_ascii(hex[(v >> shift) & 0xF]);
}

static int parse_u32(const char *s, uint32_t *out) {
    uint32_t v = 0;
    if (!*s) return 0;
    while (*s) {
        if (*s < '0' || *s > '9') return 0;
        uint32_t digit = (uint32_t)(*s - '0');
        if (v > (UINT32_MAX - digit) / 10u) return 0;
        v = v * 10u + digit;
        ++s;
    }
    *out = v;
    return 1;
}

static void read_line(char *dst, unsigned cap) {
    unsigned n = 0;
    for (;;) {
        EFI_INPUT_KEY key;
        EFI_STATUS status = IN->ReadKeyStroke(IN, &key);
        if (status != EFI_SUCCESS) {
            EFI_EVENT ev = IN->WaitForKey;
            UINTN index = 0;
            ST->BootServices->WaitForEvent(1, &ev, &index);
            continue;
        }

        CHAR16 ch = key.UnicodeChar;
        if (ch == '\r') {
            dst[n] = 0;
            return;
        }
        if (ch == 8) {
            if (n) {
                --n;
                print("\b \b");
            }
            continue;
        }
        if (ch >= 32 && ch <= 126 && n + 1 < cap) {
            dst[n++] = (char)ch;
            putc_ascii((char)ch);
        }
    }
}

static const char *memory_type_name(uint32_t t) {
    switch (t) {
        case 0: return "Reserved";
        case 1: return "LoaderCode";
        case 2: return "LoaderData";
        case 3: return "BootSvcCode";
        case 4: return "BootSvcData";
        case 5: return "RuntimeCode";
        case 6: return "RuntimeData";
        case 7: return "Conventional";
        case 8: return "Unusable";
        case 9: return "ACPIReclaim";
        case 10: return "ACPINVS";
        case 11: return "MMIO";
        case 12: return "MMIOPort";
        case 13: return "PAL";
        case 14: return "Persistent";
        default: return "Other";
    }
}

static EFI_STATUS acquire_memory_map(memory_map_t *map) {
    EFI_BOOT_SERVICES *bs = ST->BootServices;
    UINTN required = 0;
    UINTN key = 0;
    UINTN descriptor_size = 0;
    uint32_t descriptor_version = 0;

    map->buffer = 0;
    map->size = 0;
    map->key = 0;
    map->descriptor_size = 0;
    map->descriptor_version = 0;

    EFI_STATUS status = bs->GetMemoryMap(
        &required, 0, &key, &descriptor_size, &descriptor_version);
    if (status != EFI_BUFFER_TOO_SMALL) return status;
    if (descriptor_size < sizeof(EFI_MEMORY_DESCRIPTOR)) return EFI_BAD_BUFFER_SIZE;

    /* Allocate extra descriptors because AllocatePool itself can grow the map. */
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        if (descriptor_size > (UINT64_MAX - required) / 8u)
            return EFI_OUT_OF_RESOURCES;

        UINTN capacity = required + descriptor_size * 8u;
        VOID *buffer = 0;
        status = bs->AllocatePool(EfiLoaderData, capacity, &buffer);
        if (status != EFI_SUCCESS) return status;

        UINTN actual_size = capacity;
        status = bs->GetMemoryMap(
            &actual_size,
            (EFI_MEMORY_DESCRIPTOR *)buffer,
            &key,
            &descriptor_size,
            &descriptor_version
        );
        if (status == EFI_SUCCESS) {
            if (descriptor_size < sizeof(EFI_MEMORY_DESCRIPTOR) ||
                actual_size % descriptor_size != 0) {
                bs->FreePool(buffer);
                return EFI_BAD_BUFFER_SIZE;
            }
            map->buffer = (unsigned char *)buffer;
            map->size = actual_size;
            map->key = key;
            map->descriptor_size = descriptor_size;
            map->descriptor_version = descriptor_version;
            return EFI_SUCCESS;
        }

        bs->FreePool(buffer);
        if (status != EFI_BUFFER_TOO_SMALL) return status;
        required = actual_size;
    }

    return EFI_BUFFER_TOO_SMALL;
}

static void release_memory_map(memory_map_t *map) {
    if (map->buffer) {
        ST->BootServices->FreePool(map->buffer);
        map->buffer = 0;
    }
}

static void cmd_mem(void) {
    memory_map_t map;
    EFI_STATUS s = acquire_memory_map(&map);
    if (s != EFI_SUCCESS) {
        print("GetMemoryMap failed: 0x");
        print_hex_u64(s, 16);
        print("\n");
        return;
    }

    uint64_t conventional_pages = 0;
    uint64_t ramlike_pages = 0;
    unsigned char *p = map.buffer;
    unsigned char *end = map.buffer + map.size;

    while (p < end) {
        EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)p;
        if (d->Type == EfiConventionalMemory)
            conventional_pages += d->NumberOfPages;
        if (d->Type >= 1 && d->Type <= 10 && d->Type != 8)
            ramlike_pages += d->NumberOfPages;
        p += map.descriptor_size;
    }

    print("Currently conventional/usable: ");
    print_dec_u64((conventional_pages * 4096ULL) / (1024ULL * 1024ULL));
    print(" MiB\nRAM-like pages in UEFI map: ");
    print_dec_u64((ramlike_pages * 4096ULL) / (1024ULL * 1024ULL));
    print(" MiB\nDescriptors: ");
    print_dec_u64(map.size / map.descriptor_size);
    print("\n");
    release_memory_map(&map);
}

static void cmd_memmap(void) {
    memory_map_t map;
    EFI_STATUS s = acquire_memory_map(&map);
    if (s != EFI_SUCCESS) {
        print("GetMemoryMap failed: 0x");
        print_hex_u64(s, 16);
        print("\n");
        return;
    }

    print("TYPE          START               PAGES\n");
    print("-------------------------------------------\n");
    unsigned shown = 0;
    unsigned char *p = map.buffer;
    unsigned char *end = map.buffer + map.size;
    while (p < end && shown < 32) {
        EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)p;
        const char *name = memory_type_name(d->Type);
        print(name);
        unsigned len = 0; while (name[len]) ++len;
        while (len++ < 13) putc_ascii(' ');
        print("0x"); print_hex_u64(d->PhysicalStart, 16);
        print("  "); print_dec_u64(d->NumberOfPages);
        print("\n");
        p += map.descriptor_size;
        ++shown;
    }
    if (p < end) print("... truncated to first 32 descriptors\n");
    release_memory_map(&map);
}

static void cmd_time(void) {
    EFI_TIME t;
    EFI_STATUS s = ST->RuntimeServices->GetTime(&t, 0);
    if (s != EFI_SUCCESS) {
        print("UEFI GetTime failed.\n");
        return;
    }
    print_dec_u64(t.Year); putc_ascii('-');
    if (t.Month < 10) putc_ascii('0'); print_dec_u64(t.Month); putc_ascii('-');
    if (t.Day < 10) putc_ascii('0'); print_dec_u64(t.Day); putc_ascii(' ');
    if (t.Hour < 10) putc_ascii('0'); print_dec_u64(t.Hour); putc_ascii(':');
    if (t.Minute < 10) putc_ascii('0'); print_dec_u64(t.Minute); putc_ascii(':');
    if (t.Second < 10) putc_ascii('0'); print_dec_u64(t.Second);
    print("\n");
}

static void cmd_cpu(void) {
    char vendor[13];
    uint32_t ecx, edx;
    cpu_vendor(vendor);
    cpu_cpuid1(&ecx, &edx);
    print("CPU vendor: "); print(vendor); print("\n");
    print("CPUID.1 ECX: 0x"); print_hex_u64(ecx, 8); print("\n");
    print("CPUID.1 EDX: 0x"); print_hex_u64(edx, 8); print("\n");
    print("Features: ");
    if (edx & (1u << 25)) print("SSE ");
    if (edx & (1u << 26)) print("SSE2 ");
    if (ecx & (1u << 0)) print("SSE3 ");
    if (ecx & (1u << 9)) print("SSSE3 ");
    if (ecx & (1u << 19)) print("SSE4.1 ");
    if (ecx & (1u << 20)) print("SSE4.2 ");
    if (ecx & (1u << 28)) print("AVX ");
    print("\n");
}

static const char *pixel_format_name(EFI_GRAPHICS_PIXEL_FORMAT f) {
    switch (f) {
        case PixelRedGreenBlueReserved8BitPerColor: return "RGBR8";
        case PixelBlueGreenRedReserved8BitPerColor: return "BGRR8";
        case PixelBitMask: return "BITMASK";
        case PixelBltOnly: return "BLT-ONLY";
        default: return "UNKNOWN";
    }
}

static void cmd_video(void) {
    if (!console_uses_framebuffer()) {
        print("GOP linear framebuffer unavailable; using UEFI text fallback.\n");
        return;
    }
    const argus_gop_t *g = gop_info();
    print("Resolution: "); print_dec_u64(g->width); putc_ascii('x'); print_dec_u64(g->height); print("\n");
    print("Pixels/scanline: "); print_dec_u64(g->pitch_pixels); print("\n");
    print("Pixel format: "); print(pixel_format_name(g->format)); print("\n");
    print("Framebuffer: 0x"); print_hex_u64(g->fb_base, 16); print("\n");
    print("Framebuffer bytes: "); print_dec_u64(g->fb_size); print("\n");
}

static void help(void) {
    print("\nCommands:\n");
    print("  help       command list\n");
    print("  about      what this stage is\n");
    print("  clear      clear Argus console\n");
    print("  firmware   firmware vendor/revision\n");
    print("  video      framebuffer/GOP information\n");
    print("  cpu        CPUID vendor + feature bits\n");
    print("  tsc        read x86 RDTSC counter\n");
    print("  time       firmware real-time clock\n");
    print("  mem        summarize UEFI memory map\n");
    print("  memmap     first 32 memory descriptors\n");
    print("  color N    foreground color 0..15\n");
    print("  echo TEXT  print text\n");
    print("  reboot     cold reboot\n");
    print("  shutdown   firmware power-off request\n");
    print("  exit       return to firmware\n\n");
}

static void about(void) {
    print("\nArgusOS UEFI Study Monitor v" ARGUS_VERSION "\n");
    print("x86-64 PE/COFF executable loaded directly by UEFI.\n");
    print("No libc, no host OS. CPU inspection is done in assembly.\n");
    print("GOP framebuffer output is owned by Argus when available.\n");
    print("UEFI keyboard services remain active until the kernel transition stage.\n");
    print("Next stage: page allocator, ExitBootServices, then IDT/APIC.\n\n");
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system_table) {
    (void)image;
    ST = system_table;
    IN = ST->ConIn;

    EFI_STATUS watchdog_status = EFI_SUCCESS;
    if (ST->BootServices->SetWatchdogTimer)
        watchdog_status = ST->BootServices->SetWatchdogTimer(0, 0, 0, 0);

    console_init(ST);
    console_set_color(EFI_TEXT_LIGHTGREEN);
    console_clear();
    print("ArgusOS UEFI Study Monitor v" ARGUS_VERSION "\n");
    if (watchdog_status != EFI_SUCCESS) {
        print("Warning: could not disable firmware watchdog: 0x");
        print_hex_u64(watchdog_status, 16);
        print("\n");
    }
    print(console_uses_framebuffer()
        ? "Argus framebuffer console online. Type 'help'.\n\n"
        : "GOP unavailable; UEFI text fallback active. Type 'help'.\n\n");

    for (;;) {
        print("argus64> ");
        read_line(linebuf, sizeof(linebuf));
        print("\n");

        if (!linebuf[0]) continue;
        if (streq(linebuf, "help")) help();
        else if (streq(linebuf, "about")) about();
        else if (streq(linebuf, "clear")) console_clear();
        else if (streq(linebuf, "firmware")) {
            print("Firmware: ");
            print16(ST->FirmwareVendor);
            print("\nRevision: 0x"); print_hex_u64(ST->FirmwareRevision, 8); print("\n");
        }
        else if (streq(linebuf, "video")) cmd_video();
        else if (streq(linebuf, "cpu")) cmd_cpu();
        else if (streq(linebuf, "tsc")) {
            print("TSC: 0x"); print_hex_u64(cpu_read_tsc(), 16); print("\n");
        }
        else if (streq(linebuf, "time")) cmd_time();
        else if (streq(linebuf, "mem")) cmd_mem();
        else if (streq(linebuf, "memmap")) cmd_memmap();
        else if (starts_with(linebuf, "echo ")) { print(linebuf + 5); print("\n"); }
        else if (starts_with(linebuf, "color ")) {
            uint32_t c;
            if (parse_u32(linebuf + 6, &c) && c <= 15)
                console_set_color(c);
            else
                print("usage: color 0..15\n");
        }
        else if (streq(linebuf, "reboot")) {
            print("Rebooting...\n");
            ST->RuntimeServices->ResetSystem(EFI_RESET_COLD, EFI_SUCCESS, 0, 0);
        }
        else if (streq(linebuf, "shutdown")) {
            print("Requesting power off...\n");
            ST->RuntimeServices->ResetSystem(EFI_RESET_SHUTDOWN, EFI_SUCCESS, 0, 0);
        }
        else if (streq(linebuf, "exit")) {
            print("Returning to firmware.\n");
            return EFI_SUCCESS;
        }
        else print("Unknown command. Type 'help'.\n");
    }
}
