#ifndef ARGUS_HEAP_H
#define ARGUS_HEAP_H

#include <stdint.h>

int heap_init(uint64_t pages);
void *kmalloc(uint64_t size);
int kfree(void *pointer);
int heap_self_test(void);
uint64_t heap_total_bytes(void);
uint64_t heap_free_bytes(void);
uint64_t heap_used_bytes(void);

#endif
