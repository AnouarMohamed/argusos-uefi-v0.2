#ifndef ARGUS_DESKTOP_H
#define ARGUS_DESKTOP_H

#include <stdint.h>

int desktop_init(void);
int desktop_redraw(void);
void desktop_pointer_set_enabled(int enabled);
void desktop_pointer_show(void);
void desktop_pointer_hide(void);
void desktop_pointer_event(int16_t dx, int16_t dy, uint8_t buttons);
uint64_t desktop_drag_moves(void);
uint32_t desktop_pointer_x(void);
uint32_t desktop_pointer_y(void);
uint32_t desktop_window_x(void);
uint32_t desktop_window_y(void);

#endif
