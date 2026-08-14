#ifndef ARGUS_PS2_H
#define ARGUS_PS2_H

int ps2_keyboard_init(void);
int ps2_keyboard_available(void);
int ps2_getc_nonblocking(void);

#endif
