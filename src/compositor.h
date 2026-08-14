#ifndef ARGUS_COMPOSITOR_H
#define ARGUS_COMPOSITOR_H

#include "surface.h"
#include <stdint.h>

#define ARGUS_COMPOSITOR_MAX_WINDOWS 8u
#define ARGUS_COMPOSITOR_MAX_DAMAGE 16u

typedef struct {
    argus_surface_t *surface;
    uint32_t x;
    uint32_t y;
    int visible;
} argus_compositor_window_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t work_height;
    uint32_t background;
    argus_compositor_window_t windows[ARGUS_COMPOSITOR_MAX_WINDOWS];
    uint8_t z_order[ARGUS_COMPOSITOR_MAX_WINDOWS];
    uint32_t window_count;
    argus_surface_t *panel;
    uint32_t panel_y;
    argus_rect_t damage[ARGUS_COMPOSITOR_MAX_DAMAGE];
    uint32_t damage_count;
    uint64_t frame_count;
    uint64_t damage_pixels;
    uint32_t last_damage_count;
} argus_compositor_t;

int compositor_init(
    argus_compositor_t *compositor,
    uint32_t width,
    uint32_t height,
    uint32_t work_height,
    uint32_t background
);
int compositor_add_window(
    argus_compositor_t *compositor,
    argus_surface_t *surface,
    uint32_t x,
    uint32_t y,
    uint32_t *window_id
);
int compositor_set_panel(
    argus_compositor_t *compositor,
    argus_surface_t *surface,
    uint32_t y
);
void compositor_damage(
    argus_compositor_t *compositor,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height
);
void compositor_damage_all(argus_compositor_t *compositor);
int compositor_move_window(
    argus_compositor_t *compositor,
    uint32_t window_id,
    uint32_t x,
    uint32_t y
);
int compositor_raise_window(
    argus_compositor_t *compositor,
    uint32_t window_id
);
int compositor_set_window_visible(
    argus_compositor_t *compositor,
    uint32_t window_id,
    int visible
);
int compositor_window_visible(
    const argus_compositor_t *compositor,
    uint32_t window_id
);
uint32_t compositor_visible_window_count(
    const argus_compositor_t *compositor
);
int compositor_window_at(
    const argus_compositor_t *compositor,
    uint32_t x,
    uint32_t y,
    uint32_t *window_id,
    uint32_t *local_x,
    uint32_t *local_y
);
int compositor_present(argus_compositor_t *compositor);
int compositor_validate(const argus_compositor_t *compositor);
uint32_t compositor_window_x(
    const argus_compositor_t *compositor,
    uint32_t window_id
);
uint32_t compositor_window_y(
    const argus_compositor_t *compositor,
    uint32_t window_id
);

#endif
