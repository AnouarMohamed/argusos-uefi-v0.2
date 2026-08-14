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

static int mask_is_contiguous(uint32_t mask) {
    if (!mask) return 0;
    mask >>= bit_shift(mask);
    return (mask & (mask + 1u)) == 0u;
}

static int pixel_format_is_usable(const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *i) {
    if (i->PixelFormat == PixelRedGreenBlueReserved8BitPerColor ||
        i->PixelFormat == PixelBlueGreenRedReserved8BitPerColor)
        return 1;

    if (i->PixelFormat != PixelBitMask) return 0;

    uint32_t red = i->PixelInformation.RedMask;
    uint32_t green = i->PixelInformation.GreenMask;
    uint32_t blue = i->PixelInformation.BlueMask;
    uint32_t reserved = i->PixelInformation.ReservedMask;
    uint32_t colors = red | green | blue;

    return mask_is_contiguous(red) &&
           mask_is_contiguous(green) &&
           mask_is_contiguous(blue) &&
           !(red & green) && !(red & blue) && !(green & blue) &&
           !(colors & reserved);
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
    G.usable = 0;
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

    if (!pixel_format_is_usable(i) ||
        !G.width || !G.height || G.pitch_pixels < G.width ||
        !G.fb_base || (G.fb_base & 3u))
        return 0;

    uint64_t pixel_count = (uint64_t)G.pitch_pixels * G.height;
    if (pixel_count > UINT64_MAX / sizeof(uint32_t)) return 0;
    uint64_t required_bytes = pixel_count * sizeof(uint32_t);
    if (G.fb_size < required_bytes || G.fb_base > UINT64_MAX - required_bytes)
        return 0;

    G.fb = (volatile uint32_t *)(uintptr_t)G.fb_base;
    G.usable = 1;
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
    gop_scroll_rect_up(0, 0, G.width, G.height, pixels, fill_color);
}

void gop_scroll_rect_up(
    uint32_t x,
    uint32_t y,
    uint32_t w,
    uint32_t h,
    uint32_t pixels,
    uint32_t fill_color
) {
    if (!G.usable || !pixels || x >= G.width || y >= G.height) return;
    if (w > G.width - x) w = G.width - x;
    if (h > G.height - y) h = G.height - y;
    if (!w || !h) return;
    if (pixels >= h) {
        gop_fill_rect(x, y, w, h, fill_color);
        return;
    }

    uint32_t rows = h - pixels;
    for (uint32_t yy = 0; yy < rows; ++yy) {
        volatile uint32_t *dst = G.fb + (uint64_t)(y + yy) * G.pitch_pixels + x;
        volatile uint32_t *src = G.fb + (uint64_t)(y + yy + pixels) * G.pitch_pixels + x;
        for (uint32_t xx = 0; xx < w; ++xx) dst[xx] = src[xx];
    }
    gop_fill_rect(x, y + rows, w, pixels, fill_color);
}
