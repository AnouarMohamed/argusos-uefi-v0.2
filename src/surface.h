#ifndef ARGUS_SURFACE_H
#define ARGUS_SURFACE_H

#include <stdint.h>

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} argus_rect_t;

typedef struct {
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    int dirty;
    argus_rect_t damage;
} argus_surface_t;

int surface_init(argus_surface_t *surface, uint32_t width, uint32_t height);
void surface_destroy(argus_surface_t *surface);
int surface_valid(const argus_surface_t *surface);
void surface_clear(argus_surface_t *surface, uint32_t color);
void surface_fill_rect(
    argus_surface_t *surface,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t color
);
void surface_putpixel(
    argus_surface_t *surface,
    uint32_t x,
    uint32_t y,
    uint32_t color
);
uint32_t surface_getpixel(
    const argus_surface_t *surface,
    uint32_t x,
    uint32_t y
);
void surface_scroll_rect_up(
    argus_surface_t *surface,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t pixels,
    uint32_t fill_color
);
void surface_draw_text(
    argus_surface_t *surface,
    const char *text,
    uint32_t x,
    uint32_t y,
    uint32_t scale,
    uint32_t color
);
void surface_mark_dirty(
    argus_surface_t *surface,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height
);
int surface_take_damage(argus_surface_t *surface, argus_rect_t *damage);
int surface_self_test(void);

#endif
