#ifndef ARGUS_GOP_H
#define ARGUS_GOP_H

#include "efi.h"

typedef struct {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *proto;
    volatile uint32_t *fb;
    uint32_t width;
    uint32_t height;
    uint32_t pitch_pixels;
    EFI_GRAPHICS_PIXEL_FORMAT format;
    EFI_PIXEL_BITMASK masks;
    uint64_t fb_base;
    uint64_t fb_size;
    int usable;
} argus_gop_t;

int gop_init(EFI_SYSTEM_TABLE *st);
const argus_gop_t *gop_info(void);
uint32_t gop_rgb(uint8_t r, uint8_t g, uint8_t b);
void gop_putpixel(uint32_t x, uint32_t y, uint32_t packed);
void gop_fill(uint32_t packed);
void gop_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t packed);
void gop_scroll_up(uint32_t pixels, uint32_t fill_color);
void gop_scroll_rect_up(
    uint32_t x,
    uint32_t y,
    uint32_t w,
    uint32_t h,
    uint32_t pixels,
    uint32_t fill_color
);

#endif
