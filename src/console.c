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
    const argus_gop_t *g = gop_info();
    if (cursor_x + cell_w > g->width) {
        cursor_x = 0;
        cursor_y += cell_h;
    }
    if (cursor_y + cell_h > g->height) {
        gop_scroll_up(cell_h, bg);
        cursor_y = g->height - cell_h;
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
    cursor_x = cursor_y = 0;
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
    cursor_x = cursor_y = 0;
    if (!framebuffer_mode) {
        if (fallback_out) fallback_out->ClearScreen(fallback_out);
        return;
    }
    gop_fill(bg);
}

void console_putc(char c) {
    if (!framebuffer_mode) { fallback_char(c); return; }

    const argus_gop_t *g = gop_info();
    if (c == '\r') { cursor_x = 0; return; }
    if (c == '\n') {
        cursor_x = 0;
        cursor_y += cell_h;
        ensure_cursor_visible();
        return;
    }
    if (c == '\t') {
        for (unsigned i = 0; i < 4; ++i) console_putc(' ');
        return;
    }
    if (c == '\b') {
        if (cursor_x >= cell_w) cursor_x -= cell_w;
        else if (cursor_y >= cell_h) {
            cursor_y -= cell_h;
            cursor_x = (g->width / cell_w) * cell_w;
            if (cursor_x >= cell_w) cursor_x -= cell_w;
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
