#ifndef ARGUS_SNAKE_ABI_H
#define ARGUS_SNAKE_ABI_H

#include <stdint.h>

#define ARGUS_SNAKE_ABI_VERSION 1u
#define ARGUS_SNAKE_FRAME_MAGIC 0x534E414Bu
#define ARGUS_SNAKE_GRID_WIDTH 20u
#define ARGUS_SNAKE_GRID_HEIGHT 14u
#define ARGUS_SNAKE_CELL_COUNT \
    (ARGUS_SNAKE_GRID_WIDTH * ARGUS_SNAKE_GRID_HEIGHT)
#define ARGUS_SNAKE_MAX_LENGTH 96u

#define ARGUS_SNAKE_STATE_PLAYING 1u
#define ARGUS_SNAKE_STATE_GAME_OVER 2u

#define ARGUS_SNAKE_CELL_EMPTY 0u
#define ARGUS_SNAKE_CELL_BODY 1u
#define ARGUS_SNAKE_CELL_HEAD 2u
#define ARGUS_SNAKE_CELL_FOOD 3u

typedef struct {
    uint32_t magic;
    uint32_t abi_version;
    uint32_t sequence;
    uint32_t state;
    uint32_t score;
    uint32_t length;
    uint32_t width;
    uint32_t height;
    uint8_t cells[ARGUS_SNAKE_CELL_COUNT];
} argus_snake_frame_v1_t;

#endif
