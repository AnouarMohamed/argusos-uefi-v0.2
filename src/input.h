#ifndef ARGUS_INPUT_H
#define ARGUS_INPUT_H

void input_init(void);
int input_getc_nonblocking(void);
int input_has_serial(void);
int input_has_keyboard(void);

#endif
