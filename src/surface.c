#include "surface.h"
#include "font5x7.h"
#include "heap.h"

static uint32_t minimum(uint32_t left, uint32_t right) {
    return left < right ? left : right;
}

static uint32_t maximum(uint32_t left, uint32_t right) {
    return left > right ? left : right;
}

int surface_valid(const argus_surface_t *surface) {
    return surface && surface->pixels && surface->width && surface->height &&
           surface->stride >= surface->width;
}

int surface_init(argus_surface_t *surface, uint32_t width, uint32_t height) {
    if (!surface || !width || !height) return 0;
    uint64_t pixels = (uint64_t)width * height;
    if (pixels > UINT64_MAX / sizeof(uint32_t)) return 0;

    uint32_t *storage = (uint32_t *)kmalloc(pixels * sizeof(uint32_t));
    if (!storage) return 0;
    surface->pixels = storage;
    surface->width = width;
    surface->height = height;
    surface->stride = width;
    surface->dirty = 0;
    surface->damage = (argus_rect_t){0};
    return 1;
}

void surface_destroy(argus_surface_t *surface) {
    if (!surface) return;
    if (surface->pixels) (void)kfree(surface->pixels);
    *surface = (argus_surface_t){0};
}

void surface_mark_dirty(
    argus_surface_t *surface,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height
) {
    if (!surface_valid(surface) || x >= surface->width || y >= surface->height)
        return;
    width = minimum(width, surface->width - x);
    height = minimum(height, surface->height - y);
    if (!width || !height) return;

    if (!surface->dirty) {
        surface->damage = (argus_rect_t){x, y, width, height};
        surface->dirty = 1;
        return;
    }

    uint32_t left = minimum(surface->damage.x, x);
    uint32_t top = minimum(surface->damage.y, y);
    uint32_t right = maximum(
        surface->damage.x + surface->damage.width,
        x + width
    );
    uint32_t bottom = maximum(
        surface->damage.y + surface->damage.height,
        y + height
    );
    surface->damage = (argus_rect_t){left, top, right - left, bottom - top};
}

int surface_take_damage(argus_surface_t *surface, argus_rect_t *damage) {
    if (!surface_valid(surface) || !surface->dirty || !damage) return 0;
    *damage = surface->damage;
    surface->dirty = 0;
    surface->damage = (argus_rect_t){0};
    return 1;
}

void surface_clear(argus_surface_t *surface, uint32_t color) {
    if (!surface_valid(surface)) return;
    for (uint32_t y = 0; y < surface->height; ++y) {
        uint32_t *row = surface->pixels + (uint64_t)y * surface->stride;
        for (uint32_t x = 0; x < surface->width; ++x) row[x] = color;
    }
    surface_mark_dirty(surface, 0, 0, surface->width, surface->height);
}

void surface_fill_rect(
    argus_surface_t *surface,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t color
) {
    if (!surface_valid(surface) || x >= surface->width || y >= surface->height)
        return;
    width = minimum(width, surface->width - x);
    height = minimum(height, surface->height - y);
    if (!width || !height) return;

    for (uint32_t yy = 0; yy < height; ++yy) {
        uint32_t *row = surface->pixels +
            (uint64_t)(y + yy) * surface->stride + x;
        for (uint32_t xx = 0; xx < width; ++xx) row[xx] = color;
    }
    surface_mark_dirty(surface, x, y, width, height);
}

void surface_putpixel(
    argus_surface_t *surface,
    uint32_t x,
    uint32_t y,
    uint32_t color
) {
    if (!surface_valid(surface) || x >= surface->width || y >= surface->height)
        return;
    surface->pixels[(uint64_t)y * surface->stride + x] = color;
    surface_mark_dirty(surface, x, y, 1u, 1u);
}

uint32_t surface_getpixel(
    const argus_surface_t *surface,
    uint32_t x,
    uint32_t y
) {
    if (!surface_valid(surface) || x >= surface->width || y >= surface->height)
        return 0;
    return surface->pixels[(uint64_t)y * surface->stride + x];
}

void surface_scroll_rect_up(
    argus_surface_t *surface,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t pixels,
    uint32_t fill_color
) {
    if (!surface_valid(surface) || !pixels || x >= surface->width ||
        y >= surface->height)
        return;
    width = minimum(width, surface->width - x);
    height = minimum(height, surface->height - y);
    if (!width || !height) return;
    if (pixels >= height) {
        surface_fill_rect(surface, x, y, width, height, fill_color);
        return;
    }

    uint32_t rows = height - pixels;
    for (uint32_t yy = 0; yy < rows; ++yy) {
        uint32_t *destination = surface->pixels +
            (uint64_t)(y + yy) * surface->stride + x;
        const uint32_t *source = surface->pixels +
            (uint64_t)(y + yy + pixels) * surface->stride + x;
        for (uint32_t xx = 0; xx < width; ++xx)
            destination[xx] = source[xx];
    }
    surface_fill_rect(
        surface,
        x,
        y + rows,
        width,
        pixels,
        fill_color
    );
    surface_mark_dirty(surface, x, y, width, height);
}

void surface_draw_text(
    argus_surface_t *surface,
    const char *text,
    uint32_t x,
    uint32_t y,
    uint32_t scale,
    uint32_t color
) {
    if (!surface_valid(surface) || !text || !scale) return;
    while (*text) {
        char character = *text++;
        for (uint32_t row = 0; row < 7u; ++row) {
            uint8_t bits = font5x7_row(character, row);
            for (uint32_t column = 0; column < 5u; ++column) {
                if (bits & (1u << (4u - column)))
                    surface_fill_rect(
                        surface,
                        x + column * scale,
                        y + row * scale,
                        scale,
                        scale,
                        color
                    );
            }
        }
        if (x > UINT32_MAX - 6u * scale) return;
        x += 6u * scale;
    }
}

int surface_self_test(void) {
    argus_surface_t test = {0};
    if (!surface_init(&test, 8u, 8u)) return 0;
    surface_clear(&test, 0x11u);
    surface_fill_rect(&test, 2u, 2u, 3u, 3u, 0x22u);
    int valid = surface_getpixel(&test, 0u, 0u) == 0x11u &&
                surface_getpixel(&test, 2u, 2u) == 0x22u &&
                surface_getpixel(&test, 4u, 4u) == 0x22u &&
                surface_getpixel(&test, 5u, 5u) == 0x11u;
    surface_scroll_rect_up(&test, 2u, 2u, 3u, 3u, 1u, 0x33u);
    valid = surface_getpixel(&test, 2u, 4u) == 0x33u && valid;
    argus_rect_t damage;
    valid = surface_take_damage(&test, &damage) &&
            damage.x == 0u && damage.y == 0u &&
            damage.width == 8u && damage.height == 8u && valid;
    valid = !surface_take_damage(&test, &damage) && valid;
    surface_destroy(&test);
    return valid;
}
