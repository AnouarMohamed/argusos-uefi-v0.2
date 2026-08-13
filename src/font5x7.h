#ifndef ARGUS_FONT5X7_H
#define ARGUS_FONT5X7_H

#include <stdint.h>

/* Returns five useful bits for one row of a 5x7 glyph. */
uint8_t font5x7_row(char c, unsigned row);

#endif
