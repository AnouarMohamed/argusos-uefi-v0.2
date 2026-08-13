#include "efi.h"
#include "console.h"
#include "gop.h"

static EFI_SYSTEM_TABLE *ST;
static EFI_SIMPLE_TEXT_INPUT_PROTOCOL *IN;

static unsigned char memory_map_buffer[131072];
static char linebuf[128];

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
        v = v * 10u + (uint32_t)(*s - '0');
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

static EFI_STATUS get_memory_map(UINTN *size, UINTN *key, UINTN *desc_size, uint32_t *desc_version) {
    *size = sizeof(memory_map_buffer);
    return ST->BootServices->GetMemoryMap(
        size,
        (EFI_MEMORY_DESCRIPTOR *)memory_map_buffer,
        key,
        desc_size,
        desc_version
    );
}

static void cmd_mem(void) {
    UINTN size, key, desc_size;
    uint32_t version;
    EFI_STATUS s = get_memory_map(&size, &key, &desc_size, &version);
    if (s != EFI_SUCCESS) {
        print("GetMemoryMap failed; static 128 KiB buffer may be too small.\n");
        return;
    }

    uint64_t conventional_pages = 0;
    uint64_t ramlike_pages = 0;
    unsigned char *p = memory_map_buffer;
    unsigned char *end = memory_map_buffer + size;

    while (p < end) {
        EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)p;
        if (d->Type == EfiConventionalMemory)
            conventional_pages += d->NumberOfPages;
        if (d->Type >= 1 && d->Type <= 10 && d->Type != 8)
            ramlike_pages += d->NumberOfPages;
        p += desc_size;
    }

    print("Currently conventional/usable: ");
    print_dec_u64((conventional_pages * 4096ULL) / (1024ULL * 1024ULL));
    print(" MiB\nRAM-like pages in UEFI map: ");
    print_dec_u64((ramlike_pages * 4096ULL) / (1024ULL * 1024ULL));
    print(" MiB\nDescriptors: ");
    print_dec_u64(size / desc_size);
    print("\n");
}

static void cmd_memmap(void) {
    UINTN size, key, desc_size;
    uint32_t version;
    EFI_STATUS s = get_memory_map(&size, &key, &desc_size, &version);
    if (s != EFI_SUCCESS) {
        print("GetMemoryMap failed.\n");
        return;
    }

    print("TYPE          START               PAGES\n");
    print("-------------------------------------------\n");
    unsigned shown = 0;
    unsigned char *p = memory_map_buffer;
    unsigned char *end = memory_map_buffer + size;
    while (p < end && shown < 32) {
        EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)p;
        const char *name = memory_type_name(d->Type);
        print(name);
        unsigned len = 0; while (name[len]) ++len;
        while (len++ < 13) putc_ascii(' ');
        print("0x"); print_hex_u64(d->PhysicalStart, 16);
        print("  "); print_dec_u64(d->NumberOfPages);
        print("\n");
        p += desc_size;
        ++shown;
    }
    if (p < end) print("... truncated to first 32 descriptors\n");
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
    print("\nArgusOS UEFI Study Monitor v0.2\n");
    print("x86-64 PE/COFF executable loaded directly by UEFI.\n");
    print("No libc, no host OS. CPU inspection is done in assembly.\n");
    print("This stage intentionally keeps UEFI console/keyboard services alive.\n");
    print("Next stage: GOP framebuffer, memory allocator, ExitBootServices, IDT/APIC.\n\n");
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system_table) {
    (void)image;
    ST = system_table;
    IN = ST->ConIn;

    console_init(ST);
    console_set_color(EFI_TEXT_LIGHTGREEN);
    console_clear();
    print("ArgusOS UEFI Study Monitor v0.3\n");
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
