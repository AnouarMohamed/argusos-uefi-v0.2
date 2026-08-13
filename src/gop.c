#include "gop.h"

static argus_gop_t G;

static unsigned bit_shift(uint32_t mask) {
    unsigned s = 0;
    if (!mask) return 0;
    while ((mask & 1u) == 0u) { mask >>= 1; ++s; }
    return s;
}

static unsigned bit_count(uint32_t mask) {
    unsigned n = 0;
    while (mask) { n += mask & 1u; mask >>= 1; }
    return n;
}

static uint32_t scale_to_mask(uint8_t v, uint32_t mask) {
    if (!mask) return 0;
    unsigned shift = bit_shift(mask);
    unsigned bits = bit_count(mask);
    uint64_t maxv = bits >= 32 ? 0xffffffffULL : ((1ULL << bits) - 1ULL);
    uint32_t scaled = (uint32_t)(((uint64_t)v * maxv + 127ULL) / 255ULL);
    return (scaled << shift) & mask;
}

int gop_init(EFI_SYSTEM_TABLE *st) {
    EFI_GUID guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    void *iface = 0;
    if (!st || !st->BootServices || !st->BootServices->LocateProtocol) return 0;
    if (st->BootServices->LocateProtocol(&guid, 0, &iface) != EFI_SUCCESS || !iface) return 0;

    G.proto = (EFI_GRAPHICS_OUTPUT_PROTOCOL *)iface;
    if (!G.proto->Mode || !G.proto->Mode->Info) return 0;

    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *i = G.proto->Mode->Info;
    G.width = i->HorizontalResolution;
    G.height = i->VerticalResolution;
    G.pitch_pixels = i->PixelsPerScanLine;
    G.format = i->PixelFormat;
    G.masks = i->PixelInformation;
    G.fb_base = G.proto->Mode->FrameBufferBase;
    G.fb_size = G.proto->Mode->FrameBufferSize;
    G.fb = (volatile uint32_t *)(uintptr_t)G.fb_base;
    G.usable = G.fb && G.width && G.height && G.pitch_pixels && G.format != PixelBltOnly;
    return G.usable;
}

const argus_gop_t *gop_info(void) { return &G; }

uint32_t gop_rgb(uint8_t r, uint8_t g, uint8_t b) {
    switch (G.format) {
        case PixelRedGreenBlueReserved8BitPerColor:
            return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16);
        case PixelBlueGreenRedReserved8BitPerColor:
            return (uint32_t)b | ((uint32_t)g << 8) | ((uint32_t)r << 16);
        case PixelBitMask:
            return scale_to_mask(r, G.masks.RedMask) |
                   scale_to_mask(g, G.masks.GreenMask) |
                   scale_to_mask(b, G.masks.BlueMask);
        default:
            return 0;
    }
}

void gop_putpixel(uint32_t x, uint32_t y, uint32_t packed) {
    if (!G.usable || x >= G.width || y >= G.height) return;
    G.fb[(uint64_t)y * G.pitch_pixels + x] = packed;
}

void gop_fill(uint32_t packed) {
    if (!G.usable) return;
    for (uint32_t y = 0; y < G.height; ++y) {
        volatile uint32_t *row = G.fb + (uint64_t)y * G.pitch_pixels;
        for (uint32_t x = 0; x < G.width; ++x) row[x] = packed;
    }
}

void gop_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t packed) {
    if (!G.usable || x >= G.width || y >= G.height) return;
    if (w > G.width - x) w = G.width - x;
    if (h > G.height - y) h = G.height - y;
    for (uint32_t yy = 0; yy < h; ++yy) {
        volatile uint32_t *row = G.fb + (uint64_t)(y + yy) * G.pitch_pixels + x;
        for (uint32_t xx = 0; xx < w; ++xx) row[xx] = packed;
    }
}

void gop_scroll_up(uint32_t pixels, uint32_t fill_color) {
    if (!G.usable || pixels == 0) return;
    if (pixels >= G.height) { gop_fill(fill_color); return; }

    uint32_t rows = G.height - pixels;
    for (uint32_t y = 0; y < rows; ++y) {
        volatile uint32_t *dst = G.fb + (uint64_t)y * G.pitch_pixels;
        volatile uint32_t *src = G.fb + (uint64_t)(y + pixels) * G.pitch_pixels;
        for (uint32_t x = 0; x < G.width; ++x) dst[x] = src[x];
    }
    gop_fill_rect(0, rows, G.width, pixels, fill_color);
}
