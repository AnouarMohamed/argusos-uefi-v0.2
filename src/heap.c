#include "heap.h"
#include "pmm.h"

#define HEAP_BLOCK_MAGIC 0xA86B10C5u
#define HEAP_ALIGNMENT 16u

typedef struct heap_block {
    uint64_t size;
    struct heap_block *previous;
    struct heap_block *next;
    uint32_t magic;
    uint32_t free;
} heap_block_t;

_Static_assert(sizeof(heap_block_t) % HEAP_ALIGNMENT == 0,
               "heap headers must preserve payload alignment");

static uint64_t heap_base;
static uint64_t heap_size;
static heap_block_t *first_block;

static uint64_t align_size(uint64_t size) {
    if (size > UINT64_MAX - (HEAP_ALIGNMENT - 1u)) return 0;
    return (size + HEAP_ALIGNMENT - 1u) & ~(uint64_t)(HEAP_ALIGNMENT - 1u);
}

static int block_in_heap(const heap_block_t *block) {
    uint64_t address = (uint64_t)(uintptr_t)block;
    return heap_base && address >= heap_base &&
           address <= heap_base + heap_size - sizeof(heap_block_t);
}

static void merge_with_next(heap_block_t *block) {
    heap_block_t *next = block->next;
    if (!next || !next->free || next->magic != HEAP_BLOCK_MAGIC) return;
    block->size += sizeof(heap_block_t) + next->size;
    block->next = next->next;
    if (block->next) block->next->previous = block;
    next->magic = 0;
}

int heap_init(uint64_t pages) {
    if (!pages || pages > UINT64_MAX / ARGUS_PAGE_SIZE) return 0;
    uint64_t address = pmm_alloc_pages(pages);
    if (!address) return 0;

    heap_base = address;
    heap_size = pages * ARGUS_PAGE_SIZE;
    first_block = (heap_block_t *)(uintptr_t)address;
    first_block->size = heap_size - sizeof(heap_block_t);
    first_block->previous = 0;
    first_block->next = 0;
    first_block->magic = HEAP_BLOCK_MAGIC;
    first_block->free = 1;
    return 1;
}

void *kmalloc(uint64_t requested_size) {
    uint64_t size = align_size(requested_size);
    if (!size || !first_block) return 0;

    for (heap_block_t *block = first_block; block; block = block->next) {
        if (!block->free || block->magic != HEAP_BLOCK_MAGIC || block->size < size)
            continue;

        if (block->size >= size + sizeof(heap_block_t) + HEAP_ALIGNMENT) {
            heap_block_t *split = (heap_block_t *)(void *)(
                (uint8_t *)(block + 1) + size);
            split->size = block->size - size - sizeof(heap_block_t);
            split->previous = block;
            split->next = block->next;
            split->magic = HEAP_BLOCK_MAGIC;
            split->free = 1;
            if (split->next) split->next->previous = split;
            block->next = split;
            block->size = size;
        }

        block->free = 0;
        return block + 1;
    }
    return 0;
}

int kfree(void *pointer) {
    if (!pointer) return 0;
    uint64_t address = (uint64_t)(uintptr_t)pointer;
    if (!first_block || address < heap_base + sizeof(heap_block_t) ||
        address >= heap_base + heap_size || (address & (HEAP_ALIGNMENT - 1u)))
        return 0;
    heap_block_t *block = (heap_block_t *)(uintptr_t)(
        address - sizeof(heap_block_t));
    if (!block_in_heap(block) || block->magic != HEAP_BLOCK_MAGIC || block->free)
        return 0;

    block->free = 1;
    merge_with_next(block);
    if (block->previous && block->previous->free) {
        block = block->previous;
        merge_with_next(block);
    }
    return 1;
}

uint64_t heap_total_bytes(void) {
    return heap_size > sizeof(heap_block_t) ? heap_size - sizeof(heap_block_t) : 0;
}

uint64_t heap_free_bytes(void) {
    uint64_t total = 0;
    for (heap_block_t *block = first_block; block; block = block->next)
        if (block->magic == HEAP_BLOCK_MAGIC && block->free) total += block->size;
    return total;
}

uint64_t heap_used_bytes(void) {
    uint64_t total = 0;
    for (heap_block_t *block = first_block; block; block = block->next)
        if (block->magic == HEAP_BLOCK_MAGIC && !block->free) total += block->size;
    return total;
}

int heap_validate(void) {
    if (!first_block || (uint64_t)(uintptr_t)first_block != heap_base) return 0;
    heap_block_t *previous = 0;
    uint64_t cursor = heap_base;
    uint64_t end = heap_base + heap_size;
    uint64_t maximum_blocks = heap_size / sizeof(heap_block_t);
    uint64_t blocks = 0;

    for (heap_block_t *block = first_block; block; block = block->next) {
        uint64_t address = (uint64_t)(uintptr_t)block;
        if (++blocks > maximum_blocks || !block_in_heap(block) ||
            address != cursor || block->magic != HEAP_BLOCK_MAGIC ||
            block->previous != previous || !block->size ||
            (block->size & (HEAP_ALIGNMENT - 1u)) ||
            block->size > end - address - sizeof(heap_block_t) ||
            (previous && previous->free && block->free))
            return 0;
        cursor = address + sizeof(heap_block_t) + block->size;
        if (block->next && (uint64_t)(uintptr_t)block->next != cursor) return 0;
        previous = block;
    }
    return cursor == end;
}

int heap_self_test(void) {
    uint64_t free_before = heap_free_bytes();
    uint64_t used_before = heap_used_bytes();
    if (!heap_validate()) return 0;
    uint8_t *a = (uint8_t *)kmalloc(1);
    uint8_t *b = (uint8_t *)kmalloc(33);
    uint8_t *c = (uint8_t *)kmalloc(4096);
    if (!a || !b || !c || ((uintptr_t)a & 15u) ||
        ((uintptr_t)b & 15u) || ((uintptr_t)c & 15u))
        return 0;

    a[0] = 0xA1u;
    b[0] = 0xB2u;
    b[32] = 0xB3u;
    c[0] = 0xC4u;
    c[4095] = 0xC5u;
    int valid = a[0] == 0xA1u && b[0] == 0xB2u && b[32] == 0xB3u &&
                c[0] == 0xC4u && c[4095] == 0xC5u;
    valid = !kfree(a + 1) && !kfree(0) && valid;
    valid = kfree(b) && valid;
    valid = kfree(a) && valid;
    valid = kfree(c) && valid;
    valid = !kfree(c) && heap_validate() && valid;

    uint8_t *random_slots[64] = {0};
    uint64_t random_sizes[64] = {0};
    uint32_t random = 0xA86B10C5u;
    for (unsigned operation = 0; operation < 512; ++operation) {
        random = random * 1664525u + 1013904223u;
        unsigned slot = (random >> 8) & 63u;
        if (random_slots[slot]) {
            uint8_t final_value = (uint8_t)(slot ^ 0xA5u);
            if (random_sizes[slot] == 1u)
                valid = random_slots[slot][0] == final_value && valid;
            else
                valid = random_slots[slot][0] == (uint8_t)(slot + 1u) &&
                        random_slots[slot][random_sizes[slot] - 1u] == final_value &&
                        valid;
            valid = kfree(random_slots[slot]) && valid;
            random_slots[slot] = 0;
        } else {
            uint64_t size = ((uint64_t)random & 2047u) + 1u;
            uint8_t *allocation = (uint8_t *)kmalloc(size);
            if (allocation) {
                allocation[0] = (uint8_t)(slot + 1u);
                allocation[size - 1u] = (uint8_t)(slot ^ 0xA5u);
                random_slots[slot] = allocation;
                random_sizes[slot] = size;
            }
        }
        valid = heap_validate() && valid;
    }
    for (unsigned slot = 0; slot < 64; ++slot)
        if (random_slots[slot]) valid = kfree(random_slots[slot]) && valid;

    void *exhaustion[256] = {0};
    unsigned allocated = 0;
    while (allocated < 256u) {
        exhaustion[allocated] = kmalloc(2048u);
        if (!exhaustion[allocated]) break;
        ++allocated;
    }
    valid = allocated != 0 && allocated < 256u && !kmalloc(2048u) && valid;
    for (unsigned i = 0; i < allocated; i += 2) {
        valid = kfree(exhaustion[i]) && valid;
        exhaustion[i] = kmalloc(512u);
        valid = exhaustion[i] != 0 && valid;
    }
    for (unsigned i = 0; i < allocated; ++i)
        if (exhaustion[i]) valid = kfree(exhaustion[i]) && valid;

    return valid && heap_validate() && heap_free_bytes() == free_before &&
           heap_used_bytes() == used_before;
}
