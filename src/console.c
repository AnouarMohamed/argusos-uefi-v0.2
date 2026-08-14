#include "console.h"
#include "font5x7.h"
#include "gop.h"

static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *fallback_out;
static int framebuffer_mode;
static uint32_t fg;
static uint32_t bg;
static uint32_t cursor_x;
static uint32_t cursor_y;
static uint32_t scale;
static uint32_t cell_w;
static uint32_t cell_h;
static uint32_t region_x;
static uint32_t region_y;
static uint32_t region_w;
static uint32_t region_h;

static const uint8_t palette[16][3] = {
    {0,0,0},       {0,0,170},     {0,170,0},     {0,170,170},
    {170,0,0},     {170,0,170},   {170,85,0},    {170,170,170},
    {85,85,85},    {85,85,255},   {85,255,85},   {85,255,255},
    {255,85,85},   {255,85,255},  {255,255,85},  {255,255,255}
};

static void fallback_char(char c) {
    if (!fallback_out) return;
    CHAR16 t[2] = {(CHAR16)(unsigned char)c, 0};
    if (c == '\n') {
        t[0] = '\r'; fallback_out->OutputString(fallback_out, t);
        t[0] = '\n'; fallback_out->OutputString(fallback_out, t);
    } else {
        fallback_out->OutputString(fallback_out, t);
    }
}

static void draw_glyph(char c, uint32_t x, uint32_t y) {
    gop_fill_rect(x, y, cell_w, cell_h, bg);
    for (unsigned row = 0; row < 7; ++row) {
        uint8_t bits = font5x7_row(c, row);
        for (unsigned col = 0; col < 5; ++col) {
            if (bits & (1u << (4u - col))) {
                gop_fill_rect(x + col * scale, y + row * scale,
                              scale, scale, fg);
            }
        }
    }
}

static void ensure_cursor_visible(void) {
    if (cursor_x + cell_w > region_x + region_w) {
        cursor_x = region_x;
        cursor_y += cell_h;
    }
    if (cursor_y + cell_h > region_y + region_h) {
        gop_scroll_rect_up(region_x, region_y, region_w, region_h, cell_h, bg);
        cursor_y = region_y + region_h - cell_h;
    }
}

int console_init(EFI_SYSTEM_TABLE *st) {
    fallback_out = st->ConOut;
    framebuffer_mode = gop_init(st);
    if (!framebuffer_mode) return 0;

    const argus_gop_t *g = gop_info();
    scale = (g->width >= 800 && g->height >= 600) ? 2u : 1u;
    cell_w = 6u * scale;
    cell_h = 9u * scale;
    if (g->width < cell_w || g->height < cell_h) {
        framebuffer_mode = 0;
        return 0;
    }
    region_x = 0;
    region_y = 0;
    region_w = g->width;
    region_h = g->height;
    cursor_x = region_x;
    cursor_y = region_y;
    bg = gop_rgb(0, 0, 0);
    console_set_color(10);
    gop_fill(bg);
    return 1;
}

int console_uses_framebuffer(void) { return framebuffer_mode; }

void console_set_color(unsigned index) {
    index &= 15u;
    if (!framebuffer_mode) {
        if (fallback_out) fallback_out->SetAttribute(fallback_out, index);
        return;
    }
    fg = gop_rgb(palette[index][0], palette[index][1], palette[index][2]);
}

void console_clear(void) {
    cursor_x = region_x;
    cursor_y = region_y;
    if (!framebuffer_mode) {
        if (fallback_out) fallback_out->ClearScreen(fallback_out);
        return;
    }
    gop_fill_rect(region_x, region_y, region_w, region_h, bg);
}

void console_putc(char c) {
    if (!framebuffer_mode) { fallback_char(c); return; }

    if (c == '\r') { cursor_x = region_x; return; }
    if (c == '\n') {
        cursor_x = region_x;
        cursor_y += cell_h;
        ensure_cursor_visible();
        return;
    }
    if (c == '\t') {
        for (unsigned i = 0; i < 4; ++i) console_putc(' ');
        return;
    }
    if (c == '\b') {
        if (cursor_x >= region_x + cell_w) cursor_x -= cell_w;
        else if (cursor_y >= region_y + cell_h) {
            cursor_y -= cell_h;
            cursor_x = region_x + (region_w / cell_w) * cell_w;
            if (cursor_x >= region_x + cell_w) cursor_x -= cell_w;
        }
        gop_fill_rect(cursor_x, cursor_y, cell_w, cell_h, bg);
        return;
    }

    ensure_cursor_visible();
    draw_glyph(c, cursor_x, cursor_y);
    cursor_x += cell_w;
    ensure_cursor_visible();
}

void console_write(const char *s) {
    while (*s) console_putc(*s++);
}

void console_write16(const CHAR16 *s) {
    while (*s) {
        CHAR16 c = *s++;
        console_putc(c < 128 ? (char)c : '?');
    }
}

int console_set_region(
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint8_t foreground_r,
    uint8_t foreground_g,
    uint8_t foreground_b,
    uint8_t background_r,
    uint8_t background_g,
    uint8_t background_b
) {
    if (!framebuffer_mode) return 0;
    const argus_gop_t *g = gop_info();
    if (x >= g->width || y >= g->height ||
        width > g->width - x || height > g->height - y ||
        width < cell_w || height < cell_h)
        return 0;

    region_x = x;
    region_y = y;
    region_w = width - width % cell_w;
    region_h = height - height % cell_h;
    if (region_w < cell_w || region_h < cell_h) return 0;
    fg = gop_rgb(foreground_r, foreground_g, foreground_b);
    bg = gop_rgb(background_r, background_g, background_b);
    console_clear();
    return 1;
}
