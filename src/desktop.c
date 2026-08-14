#include "desktop.h"
#include "anonymity.h"
#include "apic.h"
#include "app_abi.h"
#include "compositor.h"
#include "console.h"
#include "fat32.h"
#include "gop.h"
#include "heap.h"
#include "input.h"
#include "input_keys.h"
#include "pmm.h"
#include "process.h"
#include "ramfs.h"
#include "surface.h"

#define POINTER_WIDTH 12u
#define POINTER_HEIGHT 16u
#define APP_COUNT 7u
#define APP_CONSOLE 0u
#define APP_SYSTEM 1u
#define APP_FILES 2u
#define APP_APPLICATIONS 3u
#define APP_SNAKE 4u
#define APP_CALCULATOR 5u
#define APP_NOTES 6u
#define LAUNCHER_ENTRY_COUNT 6u

typedef struct {
    uint32_t field;
    uint32_t chrome;
    uint32_t highlight;
    uint32_t shadow;
    uint32_t terminal;
    uint32_t terminal_text;
    uint32_t title;
    uint32_t ink;
    uint32_t game_field;
    uint32_t game_ink;
} desktop_colors_t;

typedef struct {
    const char *title;
    const char *task;
    argus_surface_t surface;
    uint32_t compositor_id;
    uint32_t user_app_id;
} desktop_app_t;

static const uint16_t pointer_outer[POINTER_HEIGHT] = {
    0x800u, 0xC00u, 0xE00u, 0xF00u,
    0xF80u, 0xFC0u, 0xFE0u, 0xFF0u,
    0xFF8u, 0xFC0u, 0xDC0u, 0x8E0u,
    0x060u, 0x070u, 0x030u, 0x000u
};

static const uint16_t pointer_inner[POINTER_HEIGHT] = {
    0x000u, 0x400u, 0x600u, 0x700u,
    0x780u, 0x7C0u, 0x7E0u, 0x7F0u,
    0x7C0u, 0x780u, 0x4C0u, 0x040u,
    0x020u, 0x020u, 0x000u, 0x000u
};

static desktop_colors_t colors;
static desktop_app_t apps[APP_COUNT] = {
    {.title = "KERNEL CONSOLE", .task = "CONSOLE"},
    {.title = "SYSTEM", .task = "SYSTEM"},
    {.title = "FILES", .task = "FILES"},
    {.title = "APPLICATIONS", .task = "APPS"},
    {.title = "SNAKE", .task = "SNAKE", .user_app_id = ARGUS_APP_ID_SNAKE},
    {.title = "CALCULATOR", .task = "CALC", .user_app_id = ARGUS_APP_ID_CALCULATOR},
    {.title = "NOTES", .task = "NOTES", .user_app_id = ARGUS_APP_ID_NOTES},
};
static argus_surface_t panel_surface;
static argus_compositor_t compositor;
static uint32_t scale;
static uint32_t panel_height;
static uint32_t title_height;
static uint32_t active_app;
static uint32_t task_x[APP_COUNT];
static uint32_t task_width[APP_COUNT];
static int desktop_online;
static int dragging;
static uint32_t dragged_app;
static uint32_t drag_offset_x;
static uint32_t drag_offset_y;
static uint64_t drag_moves;
static uint64_t system_tick_bucket;
static uint32_t app_sequences[ARGUS_APP_ID_NOTES + 1u];
static uint32_t launcher_selection;

static const uint32_t launcher_targets[LAUNCHER_ENTRY_COUNT] = {
    APP_CONSOLE, APP_SYSTEM, APP_FILES, APP_SNAKE, APP_CALCULATOR, APP_NOTES
};

static int pointer_enabled;
static int pointer_visible;
static int pointer_initialized;
static uint32_t pointer_x;
static uint32_t pointer_y;
static uint8_t pointer_buttons;
static uint32_t pointer_saved[POINTER_WIDTH * POINTER_HEIGHT];
static uint32_t pointer_saved_width;
static uint32_t pointer_saved_height;

static uint32_t minimum(uint32_t left, uint32_t right) {
    return left < right ? left : right;
}

static uint32_t text_width(const char *text, uint32_t text_scale) {
    uint32_t count = 0;
    while (*text++) ++count;
    return count * 6u * text_scale;
}

static int strings_equal(const char *left, const char *right) {
    while (*left && *right && *left == *right) {
        ++left;
        ++right;
    }
    return *left == 0 && *right == 0;
}

static void load_colors(void) {
    colors.field = gop_rgb(0x4c, 0x51, 0x48);
    colors.chrome = gop_rgb(0xb8, 0xb4, 0xa5);
    colors.highlight = gop_rgb(0xd3, 0xd0, 0xc2);
    colors.shadow = gop_rgb(0x6e, 0x6c, 0x64);
    colors.terminal = gop_rgb(0x17, 0x1a, 0x17);
    colors.terminal_text = gop_rgb(0xc9, 0xc8, 0xb5);
    colors.title = gop_rgb(0x4e, 0x58, 0x69);
    colors.ink = gop_rgb(0x24, 0x27, 0x24);
    colors.game_field = gop_rgb(0x91, 0x94, 0x78);
    colors.game_ink = gop_rgb(0x2b, 0x2e, 0x27);
}

static void draw_bevel_edges(
    argus_surface_t *surface,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    int raised
) {
    if (width < 2u || height < 2u) return;
    uint32_t top_left = raised ? colors.highlight : colors.shadow;
    uint32_t bottom_right = raised ? colors.shadow : colors.highlight;
    surface_fill_rect(surface, x, y, width, 1u, top_left);
    surface_fill_rect(surface, x, y, 1u, height, top_left);
    surface_fill_rect(surface, x, y + height - 1u, width, 1u, bottom_right);
    surface_fill_rect(surface, x + width - 1u, y, 1u, height, bottom_right);
}

static void draw_bevel(
    argus_surface_t *surface,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t fill,
    int raised
) {
    surface_fill_rect(surface, x, y, width, height, fill);
    draw_bevel_edges(surface, x, y, width, height, raised);
}

static uint32_t content_x(void) { return 7u; }
static uint32_t content_y(void) { return title_height + 8u; }

static uint32_t content_width(const desktop_app_t *app) {
    return app->surface.width - 14u;
}

static uint32_t content_height(const desktop_app_t *app) {
    return app->surface.height - title_height - 15u;
}

static void render_chrome(uint32_t app_index) {
    desktop_app_t *app = &apps[app_index];
    argus_surface_t *surface = &app->surface;
    uint32_t outer_content_y = title_height + 5u;
    uint32_t outer_content_height = surface->height - title_height - 9u;

    surface_fill_rect(surface, 0u, 0u, surface->width, outer_content_y,
                      colors.chrome);
    surface_fill_rect(surface, 0u, outer_content_y, 4u, outer_content_height,
                      colors.chrome);
    surface_fill_rect(
        surface,
        surface->width - 4u,
        outer_content_y,
        4u,
        outer_content_height,
        colors.chrome
    );
    surface_fill_rect(
        surface,
        0u,
        surface->height - 4u,
        surface->width,
        4u,
        colors.chrome
    );
    draw_bevel_edges(surface, 0u, 0u, surface->width, surface->height, 1);
    draw_bevel_edges(
        surface,
        4u,
        outer_content_y,
        surface->width - 8u,
        outer_content_height,
        0
    );
    surface_fill_rect(
        surface,
        3u,
        3u,
        surface->width - 6u,
        title_height,
        app_index == active_app ? colors.title : colors.shadow
    );
    surface_draw_text(
        surface,
        app->title,
        7u,
        5u,
        scale,
        colors.terminal_text
    );
}

static uint32_t draw_decimal(
    argus_surface_t *surface,
    uint64_t value,
    uint32_t x,
    uint32_t y,
    uint32_t color
) {
    char digits[21];
    unsigned used = 0;
    if (!value) digits[used++] = '0';
    while (value) {
        digits[used++] = (char)('0' + value % 10u);
        value /= 10u;
    }
    char output[21];
    for (unsigned i = 0; i < used; ++i) output[i] = digits[used - i - 1u];
    output[used] = 0;
    surface_draw_text(surface, output, x, y, scale, color);
    return used;
}

static void draw_pair(
    argus_surface_t *surface,
    const char *label,
    const char *value,
    uint32_t y
) {
    uint32_t x = content_x() + 8u;
    uint32_t value_x = x + 12u * 6u * scale;
    surface_draw_text(surface, label, x, y, scale, colors.terminal_text);
    surface_draw_text(surface, value, value_x, y, scale, colors.terminal_text);
}

static void draw_pair_number(
    argus_surface_t *surface,
    const char *label,
    uint64_t value,
    const char *unit,
    uint32_t y
) {
    uint32_t x = content_x() + 8u;
    uint32_t value_x = x + 12u * 6u * scale;
    surface_draw_text(surface, label, x, y, scale, colors.terminal_text);
    uint32_t digits = draw_decimal(
        surface,
        value,
        value_x,
        y,
        colors.terminal_text
    );
    if (unit)
        surface_draw_text(
            surface,
            unit,
            value_x + (digits + 1u) * 6u * scale,
            y,
            scale,
            colors.terminal_text
        );
}

static const char *keyboard_state(void) {
    if (!input_has_keyboard()) return "OFF";
    return input_keyboard_uses_irq() ? "IRQ" : "POLL";
}

static const char *pointer_state(void) {
    if (!input_has_pointer()) return "OFF";
    return input_pointer_uses_irq() ? "IRQ" : "POLL";
}

static void render_system(void) {
    desktop_app_t *app = &apps[APP_SYSTEM];
    argus_surface_t *surface = &app->surface;
    uint32_t x = content_x();
    uint32_t y = content_y();
    uint32_t width = content_width(app);
    uint32_t height = content_height(app);
    surface_fill_rect(surface, x, y, width, height, colors.terminal);

    uint32_t line = 9u * scale;
    uint32_t row = y + 8u;
    draw_pair_number(surface, "FREE PAGES", pmm_free_pages(), 0, row);
    row += line;
    draw_pair_number(surface, "HEAP USED", heap_used_bytes() / 1024u, "K", row);
    row += line;
    draw_pair_number(surface, "HEAP FREE", heap_free_bytes() / 1024u, "K", row);
    row += line;
    draw_pair_number(surface, "APIC TICKS", apic_timer_ticks(), 0, row);
    row += line;
    draw_pair(surface, "KEYBOARD", keyboard_state(), row);
    row += line;
    draw_pair(surface, "POINTER", pointer_state(), row);
    row += line;
    draw_pair(
        surface,
        "SECURITY",
        process_security_boundaries_online() ? "DEFAULT DENY" : "INVALID",
        row
    );
    row += line;
    draw_pair(surface, "ANON NET", anonymity_transport_state_name(), row);
    row += line;
    draw_pair(
        surface,
        "CLEARNET",
        anonymity_clearnet_allowed() ? "ALLOWED" : "DENIED",
        row
    );
}

static void draw_file_entry(
    argus_surface_t *surface,
    const char *path,
    uint64_t size,
    const char *kind,
    uint32_t y
) {
    uint32_t x = content_x() + 8u;
    uint32_t value_x = x + 20u * 6u * scale;
    surface_draw_text(surface, path, x, y, scale, colors.terminal_text);
    if (kind)
        surface_draw_text(surface, kind, value_x, y, scale, colors.terminal_text);
    else {
        uint32_t digits = draw_decimal(
            surface,
            size,
            value_x,
            y,
            colors.terminal_text
        );
        surface_draw_text(
            surface,
            "B",
            value_x + (digits + 1u) * 6u * scale,
            y,
            scale,
            colors.terminal_text
        );
    }
}

static void render_files(void) {
    desktop_app_t *app = &apps[APP_FILES];
    argus_surface_t *surface = &app->surface;
    uint32_t x = content_x();
    uint32_t y = content_y();
    uint32_t width = content_width(app);
    uint32_t height = content_height(app);
    surface_fill_rect(surface, x, y, width, height, colors.terminal);

    uint32_t line = 9u * scale;
    uint32_t row = y + 8u;
    surface_draw_text(surface, "RAMFS", x + 8u, row, scale, colors.terminal_text);
    row += line;
    uint64_t ramfs_count = ramfs_file_count();
    if (ramfs_count > 5u) ramfs_count = 5u;
    for (uint64_t index = 0; index < ramfs_count; ++index) {
        char path[ARGUS_RAMFS_MAX_PATH + 1u];
        uint64_t path_length = 0;
        uint64_t size = 0;
        if (ramfs_entry(index, path, sizeof(path), &path_length, &size) !=
            ARGUS_RAMFS_OK)
            continue;
        draw_file_entry(surface, path, size, 0, row);
        row += line;
    }
    if (!ramfs_count) {
        surface_draw_text(
            surface,
            "EMPTY",
            x + 8u,
            row,
            scale,
            colors.terminal_text
        );
        row += line;
    }

    row += line / 2u;
    surface_draw_text(surface, "FAT32", x + 8u, row, scale, colors.terminal_text);
    row += line;
    int fat_entries = 0;
    for (uint64_t index = 0; index < 5u; ++index) {
        char path[ARGUS_FAT32_MAX_PATH + 1u];
        uint64_t path_length = 0;
        uint64_t size = 0;
        uint32_t attributes = 0;
        int32_t status = fat32_entry(
            index,
            path,
            sizeof(path),
            &path_length,
            &size,
            &attributes
        );
        if (status == ARGUS_FAT32_NOT_FOUND) break;
        if (status != ARGUS_FAT32_OK) continue;
        draw_file_entry(
            surface,
            path,
            size,
            attributes & 0x10u ? "DIR" : 0,
            row
        );
        row += line;
        ++fat_entries;
    }
    if (!fat_entries)
        surface_draw_text(
            surface,
            "UNAVAILABLE",
            x + 8u,
            row,
            scale,
            colors.terminal_text
        );
}

static void render_user_app(uint32_t app_index) {
    desktop_app_t *app = &apps[app_index];
    argus_surface_t *surface = &app->surface;
    uint32_t x = content_x();
    uint32_t y = content_y();
    const uint8_t *source = 0;
    uint32_t sequence = 0;
    if (!process_app_surface(app->user_app_id, &source, &sequence)) {
        argus_process_app_status_t status;
        const char *message = "UNAVAILABLE";
        if (process_app_status(app->user_app_id, &status)) {
            if (status.state == ARGUS_PROCESS_UNUSED) message = "STOPPED";
            else if (status.state == ARGUS_PROCESS_FAULTED) message = "FAULTED";
            else if (status.state == ARGUS_PROCESS_EXITED) message = "EXITED";
        }
        surface_fill_rect(
            surface,
            x,
            y,
            content_width(app),
            content_height(app),
            colors.terminal
        );
        surface_draw_text(
            surface,
            message,
            x + 8u,
            y + 8u,
            scale,
            colors.terminal_text
        );
        app_sequences[app->user_app_id] = 0;
        surface_mark_dirty(
            surface,
            x,
            y,
            content_width(app),
            content_height(app)
        );
        return;
    }
    const uint32_t palette[ARGUS_APP_PALETTE_COUNT] = {
        colors.terminal,
        colors.terminal_text,
        colors.chrome,
        colors.title,
        colors.game_field,
        colors.game_ink,
        colors.field,
        colors.highlight,
    };
    for (uint32_t row = 0; row < ARGUS_APP_SURFACE_HEIGHT; ++row) {
        uint32_t *destination = surface->pixels +
            (uint64_t)(y + row) * surface->stride + x;
        const uint8_t *source_row = source +
            (uint64_t)row * ARGUS_APP_SURFACE_STRIDE;
        for (uint32_t column = 0; column < ARGUS_APP_SURFACE_WIDTH; ++column)
            destination[column] = palette[source_row[column]];
    }
    app_sequences[app->user_app_id] = sequence;
    surface_mark_dirty(
        surface,
        x,
        y,
        ARGUS_APP_SURFACE_WIDTH,
        ARGUS_APP_SURFACE_HEIGHT
    );
}

static void render_applications(void) {
    desktop_app_t *app = &apps[APP_APPLICATIONS];
    argus_surface_t *surface = &app->surface;
    uint32_t x = content_x();
    uint32_t y = content_y();
    uint32_t width = content_width(app);
    uint32_t height = content_height(app);
    surface_fill_rect(surface, x, y, width, height, colors.terminal);
    surface_draw_text(
        surface,
        "UP DOWN  ENTER OPEN  1-6 QUICK",
        x + 8u,
        y + 8u,
        scale,
        colors.terminal_text
    );
    uint32_t row_y = y + 22u * scale;
    uint32_t row_height = 11u * scale;
    for (uint32_t row = 0; row < LAUNCHER_ENTRY_COUNT; ++row) {
        uint32_t target = launcher_targets[row];
        uint32_t background = row == launcher_selection
            ? colors.chrome : colors.terminal;
        uint32_t foreground = row == launcher_selection
            ? colors.ink : colors.terminal_text;
        surface_fill_rect(
            surface,
            x + 8u,
            row_y,
            width - 16u,
            row_height,
            background
        );
        surface_draw_text(
            surface,
            apps[target].title,
            x + 14u,
            row_y + 2u * scale,
            scale,
            foreground
        );
        surface_draw_text(
            surface,
            apps[target].user_app_id ? "C++ USER" : "KERNEL",
            x + width - 11u * 6u * scale,
            row_y + 2u * scale,
            scale,
            foreground
        );
        row_y += row_height + 2u * scale;
    }
}

static void render_panel(void) {
    surface_clear(&panel_surface, colors.chrome);
    surface_fill_rect(
        &panel_surface,
        0u,
        0u,
        panel_surface.width,
        1u,
        colors.highlight
    );
    surface_fill_rect(
        &panel_surface,
        0u,
        panel_surface.height - 1u,
        panel_surface.width,
        1u,
        colors.shadow
    );

    uint32_t next_x = 4u;
    uint32_t height = panel_height - 8u;
    for (uint32_t i = 0; i < APP_COUNT; ++i) {
        task_x[i] = next_x;
        task_width[i] = text_width(apps[i].task, scale) + 16u;
        draw_bevel(
            &panel_surface,
            task_x[i],
            4u,
            task_width[i],
            height,
            colors.chrome,
            i != active_app
        );
        surface_draw_text(
            &panel_surface,
            apps[i].task,
            task_x[i] + 8u,
            (panel_height - 7u * scale) / 2u,
            scale,
            colors.ink
        );
        next_x += task_width[i] + 4u;
    }
}

static void destroy_surfaces(void) {
    for (uint32_t i = 0; i < APP_COUNT; ++i)
        surface_destroy(&apps[i].surface);
    surface_destroy(&panel_surface);
}

static int initialize_surfaces(uint32_t width, uint32_t work_height) {
    uint32_t console_width = width >= 1000u ? 720u : (width * 3u) / 5u;
    uint32_t console_height = work_height >= 680u ? 560u :
        (work_height * 3u) / 4u;
    uint32_t utility_width = width >= 700u ? 400u : width - 24u;
    uint32_t system_height = minimum(220u, work_height - 24u);
    uint32_t files_height = minimum(320u, work_height - 24u);
    uint32_t applications_height = minimum(320u, work_height - 24u);
    uint32_t user_width = ARGUS_APP_SURFACE_WIDTH + 14u;
    uint32_t user_height = title_height + ARGUS_APP_SURFACE_HEIGHT + 15u;

    console_width = minimum(console_width, width - 24u);
    console_height = minimum(console_height, work_height - 24u);
    if (console_width < 320u || console_height < 200u ||
        utility_width < 280u || system_height < 160u || files_height < 200u ||
        applications_height < 240u || user_width > width - 24u ||
        user_height > work_height - 24u)
        return 0;

    if (!surface_init(&apps[APP_CONSOLE].surface, console_width, console_height) ||
        !surface_init(&apps[APP_SYSTEM].surface, utility_width, system_height) ||
        !surface_init(&apps[APP_FILES].surface, utility_width, files_height) ||
        !surface_init(
            &apps[APP_APPLICATIONS].surface,
            utility_width,
            applications_height
        ) ||
        !surface_init(&apps[APP_SNAKE].surface, user_width, user_height) ||
        !surface_init(&apps[APP_CALCULATOR].surface, user_width, user_height) ||
        !surface_init(&apps[APP_NOTES].surface, user_width, user_height) ||
        !surface_init(&panel_surface, width, panel_height)) {
        destroy_surfaces();
        return 0;
    }
    return 1;
}

static int initialize_compositor(const argus_gop_t *g, uint32_t work_height) {
    if (!compositor_init(
            &compositor,
            g->width,
            g->height,
            work_height,
            colors.field))
        return 0;

    uint32_t right_margin = g->width >= 700u ? 32u : 12u;
    uint32_t utility_x = g->width - apps[APP_SYSTEM].surface.width - right_margin;
    uint32_t console_x = g->width >= 900u ? 40u : 12u;
    uint32_t console_y = work_height >= 650u ? 96u : 32u;
    uint32_t system_y = work_height >= 300u ? 40u : 8u;
    uint32_t files_y = system_y + apps[APP_SYSTEM].surface.height + 24u;
    uint32_t user_x = (g->width - apps[APP_SNAKE].surface.width) / 2u;
    uint32_t user_y = work_height >= 500u ? 72u : 12u;
    uint32_t applications_x = g->width >= 900u ? 72u : 20u;
    uint32_t applications_y = work_height >= 500u ? 64u : 12u;
    if (files_y > work_height - apps[APP_FILES].surface.height)
        files_y = work_height - apps[APP_FILES].surface.height;

    if (!compositor_add_window(
            &compositor,
            &apps[APP_CONSOLE].surface,
            console_x,
            console_y,
            &apps[APP_CONSOLE].compositor_id) ||
        !compositor_add_window(
            &compositor,
            &apps[APP_SYSTEM].surface,
            utility_x,
            system_y,
            &apps[APP_SYSTEM].compositor_id) ||
        !compositor_add_window(
            &compositor,
            &apps[APP_FILES].surface,
            utility_x,
            files_y,
            &apps[APP_FILES].compositor_id) ||
        !compositor_add_window(
            &compositor,
            &apps[APP_APPLICATIONS].surface,
            applications_x,
            applications_y,
            &apps[APP_APPLICATIONS].compositor_id) ||
        !compositor_add_window(
            &compositor,
            &apps[APP_SNAKE].surface,
            user_x,
            user_y,
            &apps[APP_SNAKE].compositor_id) ||
        !compositor_add_window(
            &compositor,
            &apps[APP_CALCULATOR].surface,
            user_x + 24u,
            user_y + 20u,
            &apps[APP_CALCULATOR].compositor_id) ||
        !compositor_add_window(
            &compositor,
            &apps[APP_NOTES].surface,
            user_x > 24u ? user_x - 24u : 0u,
            user_y + 40u,
            &apps[APP_NOTES].compositor_id) ||
        !compositor_set_panel(&compositor, &panel_surface, work_height))
        return 0;
    return compositor_raise_window(
        &compositor,
        apps[APP_CONSOLE].compositor_id
    );
}

static void activate_app(uint32_t app_index) {
    if (app_index >= APP_COUNT) return;
    uint32_t previous = active_app;
    active_app = app_index;
    process_app_set_active(apps[app_index].user_app_id);
    (void)compositor_raise_window(
        &compositor,
        apps[app_index].compositor_id
    );
    render_chrome(previous);
    if (previous != app_index) render_chrome(app_index);
    if (app_index == APP_APPLICATIONS) render_applications();
    render_panel();
}

static uint32_t clamp_pointer_coordinate(int64_t value, uint32_t maximum) {
    if (value < 0) return 0;
    if ((uint64_t)value > maximum) return maximum;
    return (uint32_t)value;
}

int desktop_init(void) {
    if (!console_uses_framebuffer() || !surface_self_test()) return 0;
    const argus_gop_t *g = gop_info();
    if (!g->usable || g->width < 640u || g->height < 400u) return 0;

    desktop_online = 0;
    load_colors();
    scale = g->width >= 800u && g->height >= 600u ? 2u : 1u;
    panel_height = scale == 2u ? 32u : 24u;
    title_height = scale == 2u ? 24u : 16u;
    uint32_t work_height = g->height - panel_height;
    active_app = APP_CONSOLE;
    pointer_initialized = 0;
    pointer_enabled = 0;
    pointer_visible = 0;
    dragging = 0;
    pointer_buttons = 0;
    drag_moves = 0;
    system_tick_bucket = UINT64_MAX;
    launcher_selection = 0;
    for (uint32_t app = 0; app <= ARGUS_APP_ID_NOTES; ++app)
        app_sequences[app] = 0;

    if (!initialize_surfaces(g->width, work_height) ||
        !initialize_compositor(g, work_height)) {
        destroy_surfaces();
        return 0;
    }

    for (uint32_t i = 0; i < APP_COUNT; ++i) {
        surface_clear(&apps[i].surface, colors.terminal);
        render_chrome(i);
    }
    render_system();
    render_files();
    render_applications();
    render_user_app(APP_SNAKE);
    render_user_app(APP_CALCULATOR);
    render_user_app(APP_NOTES);
    render_panel();

    desktop_app_t *console_app = &apps[APP_CONSOLE];
    if (!console_set_surface_region(
            &console_app->surface,
            content_x(),
            content_y(),
            content_width(console_app),
            content_height(console_app),
            0xc9,
            0xc8,
            0xb5,
            0x17,
            0x1a,
            0x17)) {
        destroy_surfaces();
        return 0;
    }

    compositor_damage_all(&compositor);
    if (!compositor_validate(&compositor) || !compositor_present(&compositor)) {
        (void)console_set_region(
            0u,
            0u,
            g->width,
            g->height,
            0xc9,
            0xc8,
            0xb5,
            0x17,
            0x1a,
            0x17
        );
        destroy_surfaces();
        return 0;
    }
    desktop_online = 1;
    pointer_x = g->width / 2u;
    pointer_y = work_height / 2u;
    pointer_initialized = 1;
    return 1;
}

int desktop_redraw(void) {
    if (!desktop_online) return 0;
    for (uint32_t i = 0; i < APP_COUNT; ++i) render_chrome(i);
    render_system();
    render_files();
    render_applications();
    render_user_app(APP_SNAKE);
    render_user_app(APP_CALCULATOR);
    render_user_app(APP_NOTES);
    render_panel();
    compositor_damage_all(&compositor);
    return compositor_validate(&compositor);
}

int desktop_present(void) {
    if (!desktop_online) return 0;
    desktop_pointer_hide();
    int result = compositor_present(&compositor);
    desktop_pointer_show();
    return result;
}

void desktop_refresh_apps(void) {
    if (!desktop_online) return;
    render_system();
    render_files();
    render_applications();
    render_user_app(APP_SNAKE);
    render_user_app(APP_CALCULATOR);
    render_user_app(APP_NOTES);
}

void desktop_tick(uint64_t ticks) {
    if (!desktop_online) return;
    uint64_t bucket = ticks / 100u;
    int app_changed = 0;
    for (uint32_t app_index = APP_SNAKE; app_index <= APP_NOTES; ++app_index) {
        const uint8_t *pixels;
        uint32_t sequence;
        uint32_t app_id = apps[app_index].user_app_id;
        int available = process_app_surface(app_id, &pixels, &sequence);
        if ((available && sequence != app_sequences[app_id]) ||
            (!available && app_sequences[app_id]))
            app_changed = 1;
    }
    int system_changed = bucket != system_tick_bucket;
    if (!app_changed && !system_changed) return;
    desktop_pointer_hide();
    if (system_changed) {
        system_tick_bucket = bucket;
        render_system();
    }
    if (app_changed)
        for (uint32_t app_index = APP_SNAKE; app_index <= APP_NOTES; ++app_index) {
            const uint8_t *pixels;
            uint32_t sequence;
            uint32_t app_id = apps[app_index].user_app_id;
            int available = process_app_surface(app_id, &pixels, &sequence);
            if ((available && sequence != app_sequences[app_id]) ||
                (!available && app_sequences[app_id]))
                render_user_app(app_index);
        }
    (void)compositor_present(&compositor);
    desktop_pointer_show();
}

int desktop_focus_app(const char *name) {
    if (!desktop_online || !name) return 0;
    uint32_t app_index;
    if (strings_equal(name, "console")) app_index = APP_CONSOLE;
    else if (strings_equal(name, "system")) app_index = APP_SYSTEM;
    else if (strings_equal(name, "files")) app_index = APP_FILES;
    else if (strings_equal(name, "apps") ||
             strings_equal(name, "applications")) app_index = APP_APPLICATIONS;
    else if (strings_equal(name, "snake")) app_index = APP_SNAKE;
    else if (strings_equal(name, "calc") ||
             strings_equal(name, "calculator")) app_index = APP_CALCULATOR;
    else if (strings_equal(name, "notes")) app_index = APP_NOTES;
    else return 0;
    activate_app(app_index);
    return 1;
}

int desktop_key_event(uint8_t key) {
    if (!desktop_online) return 0;
    if (active_app == APP_APPLICATIONS) {
        if (key >= '1' && key <= '6') {
            launcher_selection = (uint32_t)(key - '1');
            activate_app(launcher_targets[launcher_selection]);
            (void)compositor_present(&compositor);
            return 1;
        } else if (key == ARGUS_KEY_UP) {
            launcher_selection = launcher_selection
                ? launcher_selection - 1u : LAUNCHER_ENTRY_COUNT - 1u;
        } else if (key == ARGUS_KEY_DOWN) {
            launcher_selection =
                (launcher_selection + 1u) % LAUNCHER_ENTRY_COUNT;
        } else if (key == '\n' || key == '\r') {
            activate_app(launcher_targets[launcher_selection]);
            (void)compositor_present(&compositor);
            return 1;
        } else return 1;
        render_applications();
        (void)compositor_present(&compositor);
        return 1;
    }
    uint32_t app_id = apps[active_app].user_app_id;
    if (!app_id) return 0;
    (void)process_app_input(app_id, key);
    return 1;
}

void desktop_pointer_set_enabled(int enabled) {
    if (!enabled) desktop_pointer_hide();
    pointer_enabled = enabled != 0;
}

void desktop_pointer_show(void) {
    if (!pointer_enabled || pointer_visible || !pointer_initialized ||
        !console_uses_framebuffer())
        return;
    const argus_gop_t *g = gop_info();
    pointer_saved_width = minimum(POINTER_WIDTH, g->width - pointer_x);
    pointer_saved_height = minimum(POINTER_HEIGHT, g->height - pointer_y);

    for (uint32_t y = 0; y < pointer_saved_height; ++y) {
        for (uint32_t x = 0; x < pointer_saved_width; ++x) {
            pointer_saved[y * POINTER_WIDTH + x] =
                gop_getpixel(pointer_x + x, pointer_y + y);
            uint16_t bit = (uint16_t)(1u << (11u - x));
            if (pointer_outer[y] & bit) {
                uint32_t color = pointer_inner[y] & bit
                    ? colors.highlight
                    : colors.ink;
                gop_putpixel(pointer_x + x, pointer_y + y, color);
            }
        }
    }
    pointer_visible = 1;
}

void desktop_pointer_hide(void) {
    if (!pointer_visible) return;
    for (uint32_t y = 0; y < pointer_saved_height; ++y)
        for (uint32_t x = 0; x < pointer_saved_width; ++x)
            gop_putpixel(
                pointer_x + x,
                pointer_y + y,
                pointer_saved[y * POINTER_WIDTH + x]
            );
    pointer_visible = 0;
}

static int panel_app_at(uint32_t x, uint32_t y, uint32_t *app_index) {
    if (y < compositor.panel_y || y >= compositor.height) return 0;
    uint32_t local_y = y - compositor.panel_y;
    if (local_y < 4u || local_y >= panel_height - 4u) return 0;
    for (uint32_t i = 0; i < APP_COUNT; ++i) {
        if (x >= task_x[i] && x < task_x[i] + task_width[i]) {
            *app_index = i;
            return 1;
        }
    }
    return 0;
}

static int launcher_entry_at(
    uint32_t local_x,
    uint32_t local_y,
    uint32_t *entry
) {
    uint32_t start_x = content_x() + 8u;
    uint32_t start_y = content_y() + 22u * scale;
    uint32_t row_height = 11u * scale;
    uint32_t pitch = row_height + 2u * scale;
    if (!entry || local_x < start_x || local_y < start_y ||
        local_x >= apps[APP_APPLICATIONS].surface.width - content_x() - 8u)
        return 0;
    uint32_t candidate = (local_y - start_y) / pitch;
    if (candidate >= LAUNCHER_ENTRY_COUNT ||
        (local_y - start_y) % pitch >= row_height)
        return 0;
    *entry = candidate;
    return 1;
}

void desktop_pointer_event(int16_t dx, int16_t dy, uint8_t buttons) {
    if (!pointer_enabled || !desktop_online) return;
    const argus_gop_t *g = gop_info();
    desktop_pointer_hide();
    pointer_x = clamp_pointer_coordinate((int64_t)pointer_x + dx, g->width - 1u);
    pointer_y = clamp_pointer_coordinate((int64_t)pointer_y + dy, g->height - 1u);

    int left_pressed = (buttons & 1u) && !(pointer_buttons & 1u);
    if (left_pressed) {
        uint32_t app_index;
        if (panel_app_at(pointer_x, pointer_y, &app_index)) {
            activate_app(app_index);
            dragging = 0;
        } else {
            uint32_t window_id;
            uint32_t local_x;
            uint32_t local_y;
            if (compositor_window_at(
                    &compositor,
                    pointer_x,
                    pointer_y,
                    &window_id,
                    &local_x,
                    &local_y)) {
                for (app_index = 0; app_index < APP_COUNT; ++app_index)
                    if (apps[app_index].compositor_id == window_id) break;
                if (app_index < APP_COUNT) {
                    activate_app(app_index);
                    uint32_t launcher_entry;
                    if (app_index == APP_APPLICATIONS &&
                        launcher_entry_at(local_x, local_y, &launcher_entry)) {
                        launcher_selection = launcher_entry;
                        activate_app(launcher_targets[launcher_entry]);
                    } else if (local_x >= 3u &&
                        local_x < apps[app_index].surface.width - 3u &&
                        local_y >= 3u && local_y < 3u + title_height) {
                        dragging = 1;
                        dragged_app = app_index;
                        drag_offset_x = local_x;
                        drag_offset_y = local_y;
                    }
                }
            }
        }
    }

    if (dragging && (buttons & 1u)) {
        uint32_t new_x = pointer_x > drag_offset_x
            ? pointer_x - drag_offset_x
            : 0u;
        uint32_t new_y = pointer_y > drag_offset_y
            ? pointer_y - drag_offset_y
            : 0u;
        uint32_t window_id = apps[dragged_app].compositor_id;
        uint32_t old_x = compositor_window_x(&compositor, window_id);
        uint32_t old_y = compositor_window_y(&compositor, window_id);
        if (compositor_move_window(&compositor, window_id, new_x, new_y) &&
            (old_x != compositor_window_x(&compositor, window_id) ||
             old_y != compositor_window_y(&compositor, window_id)))
            ++drag_moves;
    }
    if (!(buttons & 1u)) dragging = 0;
    pointer_buttons = buttons;
    (void)compositor_present(&compositor);
    desktop_pointer_show();
}

uint64_t desktop_drag_moves(void) { return drag_moves; }
uint32_t desktop_pointer_x(void) { return pointer_x; }
uint32_t desktop_pointer_y(void) { return pointer_y; }
uint32_t desktop_window_x(void) {
    return compositor_window_x(&compositor, apps[APP_CONSOLE].compositor_id);
}
uint32_t desktop_window_y(void) {
    return compositor_window_y(&compositor, apps[APP_CONSOLE].compositor_id);
}
uint32_t desktop_surface_count(void) { return desktop_online ? APP_COUNT : 0u; }
const char *desktop_active_app(void) {
    return desktop_online ? apps[active_app].title : "UNAVAILABLE";
}
uint64_t desktop_compositor_frames(void) { return compositor.frame_count; }
uint64_t desktop_damage_pixels(void) { return compositor.damage_pixels; }
uint32_t desktop_last_damage_count(void) { return compositor.last_damage_count; }
int desktop_compositor_valid(void) {
    return desktop_online && compositor_validate(&compositor);
}
