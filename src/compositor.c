#include "compositor.h"
#include "gop.h"

static uint32_t minimum(uint32_t left, uint32_t right) {
    return left < right ? left : right;
}

static uint32_t maximum(uint32_t left, uint32_t right) {
    return left > right ? left : right;
}

static int rectangles_touch(const argus_rect_t *left, const argus_rect_t *right) {
    uint64_t left_right = (uint64_t)left->x + left->width;
    uint64_t left_bottom = (uint64_t)left->y + left->height;
    uint64_t right_right = (uint64_t)right->x + right->width;
    uint64_t right_bottom = (uint64_t)right->y + right->height;
    return left->x <= right_right && right->x <= left_right &&
           left->y <= right_bottom && right->y <= left_bottom;
}

int compositor_init(
    argus_compositor_t *compositor,
    uint32_t width,
    uint32_t height,
    uint32_t work_height,
    uint32_t background
) {
    if (!compositor || !width || !height || !work_height || work_height > height)
        return 0;
    *compositor = (argus_compositor_t){0};
    compositor->width = width;
    compositor->height = height;
    compositor->work_height = work_height;
    compositor->background = background;
    return 1;
}

int compositor_add_window(
    argus_compositor_t *compositor,
    argus_surface_t *surface,
    uint32_t x,
    uint32_t y,
    uint32_t *window_id
) {
    if (!compositor || !surface_valid(surface) || !window_id ||
        compositor->window_count >= ARGUS_COMPOSITOR_MAX_WINDOWS ||
        surface->width > compositor->width ||
        surface->height > compositor->work_height)
        return 0;

    if (x > compositor->width - surface->width)
        x = compositor->width - surface->width;
    if (y > compositor->work_height - surface->height)
        y = compositor->work_height - surface->height;

    uint32_t id = compositor->window_count++;
    compositor->windows[id] = (argus_compositor_window_t){surface, x, y, 1};
    compositor->z_order[id] = (uint8_t)id;
    *window_id = id;
    compositor_damage(compositor, x, y, surface->width, surface->height);
    return 1;
}

int compositor_set_panel(
    argus_compositor_t *compositor,
    argus_surface_t *surface,
    uint32_t y
) {
    if (!compositor || !surface_valid(surface) || surface->width != compositor->width ||
        y >= compositor->height || surface->height > compositor->height - y)
        return 0;
    compositor->panel = surface;
    compositor->panel_y = y;
    compositor_damage(compositor, 0u, y, surface->width, surface->height);
    return 1;
}

void compositor_damage(
    argus_compositor_t *compositor,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height
) {
    if (!compositor || x >= compositor->width || y >= compositor->height)
        return;
    width = minimum(width, compositor->width - x);
    height = minimum(height, compositor->height - y);
    if (!width || !height) return;

    argus_rect_t incoming = {x, y, width, height};
    for (uint32_t i = 0; i < compositor->damage_count; ++i) {
        if (!rectangles_touch(&compositor->damage[i], &incoming)) continue;
        argus_rect_t *current = &compositor->damage[i];
        uint32_t left = minimum(current->x, incoming.x);
        uint32_t top = minimum(current->y, incoming.y);
        uint32_t right = maximum(
            current->x + current->width,
            incoming.x + incoming.width
        );
        uint32_t bottom = maximum(
            current->y + current->height,
            incoming.y + incoming.height
        );
        *current = (argus_rect_t){left, top, right - left, bottom - top};
        return;
    }

    if (compositor->damage_count == ARGUS_COMPOSITOR_MAX_DAMAGE) {
        compositor->damage_count = 1u;
        compositor->damage[0] = (argus_rect_t){
            0u, 0u, compositor->width, compositor->height
        };
        return;
    }
    compositor->damage[compositor->damage_count++] = incoming;
}

void compositor_damage_all(argus_compositor_t *compositor) {
    if (!compositor) return;
    compositor->damage_count = 1u;
    compositor->damage[0] = (argus_rect_t){
        0u, 0u, compositor->width, compositor->height
    };
}

int compositor_move_window(
    argus_compositor_t *compositor,
    uint32_t window_id,
    uint32_t x,
    uint32_t y
) {
    if (!compositor || window_id >= compositor->window_count) return 0;
    argus_compositor_window_t *window = &compositor->windows[window_id];
    if (!window->visible || !surface_valid(window->surface)) return 0;
    if (x > compositor->width - window->surface->width)
        x = compositor->width - window->surface->width;
    if (y > compositor->work_height - window->surface->height)
        y = compositor->work_height - window->surface->height;
    if (x == window->x && y == window->y) return 1;

    compositor_damage(
        compositor,
        window->x,
        window->y,
        window->surface->width,
        window->surface->height
    );
    window->x = x;
    window->y = y;
    compositor_damage(
        compositor,
        window->x,
        window->y,
        window->surface->width,
        window->surface->height
    );
    return 1;
}

int compositor_raise_window(
    argus_compositor_t *compositor,
    uint32_t window_id
) {
    if (!compositor || window_id >= compositor->window_count) return 0;
    uint32_t position = compositor->window_count;
    for (uint32_t i = 0; i < compositor->window_count; ++i) {
        if (compositor->z_order[i] == window_id) {
            position = i;
            break;
        }
    }
    if (position == compositor->window_count) return 0;
    if (position + 1u == compositor->window_count) return 1;
    for (uint32_t i = position; i + 1u < compositor->window_count; ++i)
        compositor->z_order[i] = compositor->z_order[i + 1u];
    compositor->z_order[compositor->window_count - 1u] = (uint8_t)window_id;

    argus_compositor_window_t *window = &compositor->windows[window_id];
    compositor_damage(
        compositor,
        window->x,
        window->y,
        window->surface->width,
        window->surface->height
    );
    return 1;
}

int compositor_set_window_visible(
    argus_compositor_t *compositor,
    uint32_t window_id,
    int visible
) {
    if (!compositor || window_id >= compositor->window_count) return 0;
    argus_compositor_window_t *window = &compositor->windows[window_id];
    int next = visible != 0;
    if (window->visible == next) return 1;
    compositor_damage(
        compositor,
        window->x,
        window->y,
        window->surface->width,
        window->surface->height
    );
    window->visible = next;
    return 1;
}

int compositor_window_visible(
    const argus_compositor_t *compositor,
    uint32_t window_id
) {
    return compositor && window_id < compositor->window_count &&
        compositor->windows[window_id].visible;
}

uint32_t compositor_visible_window_count(
    const argus_compositor_t *compositor
) {
    if (!compositor) return 0u;
    uint32_t count = 0u;
    for (uint32_t id = 0; id < compositor->window_count; ++id)
        if (compositor->windows[id].visible) ++count;
    return count;
}

int compositor_window_at(
    const argus_compositor_t *compositor,
    uint32_t x,
    uint32_t y,
    uint32_t *window_id,
    uint32_t *local_x,
    uint32_t *local_y
) {
    if (!compositor || x >= compositor->width || y >= compositor->work_height)
        return 0;
    for (uint32_t i = compositor->window_count; i > 0u; --i) {
        uint32_t id = compositor->z_order[i - 1u];
        const argus_compositor_window_t *window = &compositor->windows[id];
        if (!window->visible || !surface_valid(window->surface) ||
            x < window->x || y < window->y ||
            x >= window->x + window->surface->width ||
            y >= window->y + window->surface->height)
            continue;
        if (window_id) *window_id = id;
        if (local_x) *local_x = x - window->x;
        if (local_y) *local_y = y - window->y;
        return 1;
    }
    return 0;
}

static void collect_surface_damage(argus_compositor_t *compositor) {
    for (uint32_t id = 0; id < compositor->window_count; ++id) {
        argus_compositor_window_t *window = &compositor->windows[id];
        argus_rect_t damage;
        if (!surface_take_damage(window->surface, &damage) || !window->visible)
            continue;
        compositor_damage(
            compositor,
            window->x + damage.x,
            window->y + damage.y,
            damage.width,
            damage.height
        );
    }
    if (compositor->panel) {
        argus_rect_t damage;
        if (surface_take_damage(compositor->panel, &damage))
            compositor_damage(
                compositor,
                damage.x,
                compositor->panel_y + damage.y,
                damage.width,
                damage.height
            );
    }
}

static uint32_t composed_pixel(
    const argus_compositor_t *compositor,
    uint32_t x,
    uint32_t y
) {
    uint32_t pixel = compositor->background;
    for (uint32_t i = 0; i < compositor->window_count; ++i) {
        const argus_compositor_window_t *window =
            &compositor->windows[compositor->z_order[i]];
        if (!window->visible || x < window->x || y < window->y ||
            x >= window->x + window->surface->width ||
            y >= window->y + window->surface->height)
            continue;
        pixel = surface_getpixel(
            window->surface,
            x - window->x,
            y - window->y
        );
    }
    if (compositor->panel && y >= compositor->panel_y &&
        y < compositor->panel_y + compositor->panel->height)
        pixel = surface_getpixel(compositor->panel, x, y - compositor->panel_y);
    return pixel;
}

int compositor_present(argus_compositor_t *compositor) {
    if (!compositor || !gop_info()->usable) return 0;
    collect_surface_damage(compositor);
    if (!compositor->damage_count) {
        compositor->last_damage_count = 0;
        return 1;
    }

    uint64_t pixels = 0;
    uint32_t presented = compositor->damage_count;
    for (uint32_t i = 0; i < compositor->damage_count; ++i) {
        const argus_rect_t *damage = &compositor->damage[i];
        pixels += (uint64_t)damage->width * damage->height;
        for (uint32_t y = damage->y; y < damage->y + damage->height; ++y)
            for (uint32_t x = damage->x; x < damage->x + damage->width; ++x)
                gop_putpixel(x, y, composed_pixel(compositor, x, y));
    }
    compositor->damage_count = 0;
    compositor->last_damage_count = presented;
    compositor->damage_pixels += pixels;
    ++compositor->frame_count;
    return 1;
}

int compositor_validate(const argus_compositor_t *compositor) {
    if (!compositor || !compositor->width || !compositor->height ||
        !compositor->work_height || compositor->work_height > compositor->height ||
        compositor->window_count > ARGUS_COMPOSITOR_MAX_WINDOWS ||
        compositor->damage_count > ARGUS_COMPOSITOR_MAX_DAMAGE)
        return 0;

    uint32_t seen = 0;
    for (uint32_t i = 0; i < compositor->window_count; ++i) {
        uint32_t id = compositor->z_order[i];
        if (id >= compositor->window_count || (seen & (1u << id))) return 0;
        seen |= 1u << id;
        const argus_compositor_window_t *window = &compositor->windows[id];
        if (!surface_valid(window->surface) ||
            window->x > compositor->width - window->surface->width ||
            window->y > compositor->work_height - window->surface->height)
            return 0;
    }
    if (compositor->panel &&
        (!surface_valid(compositor->panel) ||
         compositor->panel->width != compositor->width ||
         compositor->panel_y > compositor->height - compositor->panel->height))
        return 0;
    return 1;
}

uint32_t compositor_window_x(
    const argus_compositor_t *compositor,
    uint32_t window_id
) {
    return compositor && window_id < compositor->window_count
        ? compositor->windows[window_id].x
        : 0u;
}

uint32_t compositor_window_y(
    const argus_compositor_t *compositor,
    uint32_t window_id
) {
    return compositor && window_id < compositor->window_count
        ? compositor->windows[window_id].y
        : 0u;
}
