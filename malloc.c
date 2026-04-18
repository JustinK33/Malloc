#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#define MAGICAL_BYTES 0x55
#define BLOCK_MARKER 0xDD
#define PAGE_SIZE 4096

typedef struct free_area {
    uint8_t marker;
    struct free_area *prev;
    bool in_use;
    uint32_t length;
    struct free_area *next;
} area;

typedef struct stats {
    int magical_bytes;
    bool my_simple_lock;
    uint32_t amount_of_blocks;
    uint16_t amount_of_pages;
} my_stats;

static void *heap_start = NULL;

my_stats *get_malloc_header(void) {
    return (my_stats *)heap_start;
}

area *find_last_block(void) {
    area *block = (area *)((char *)heap_start + sizeof(my_stats));
    while (block->next != NULL)
        block = block->next;
    return block;
}

area *find_previous_used_block(area *from) {
    area *block = from->prev;
    while (block != NULL) {
        if (block->in_use)
            return block;
        block = block->prev;
    }
    return NULL;
}

static void reduce_heap_size_if_possible(void) {
    area *last_block = find_last_block();
    area *prev_used_block = find_previous_used_block(last_block);
    my_stats *malloc_header = get_malloc_header();

    if (prev_used_block == NULL) {
        // only one block left — shrink it to one page minimum, don't delete it
        if (last_block->length > PAGE_SIZE)
            last_block->length = PAGE_SIZE;
        prev_used_block = last_block;
    }

    void *new_end  = (void *)prev_used_block + sizeof(area) + prev_used_block->length;
    void *heap_end = sbrk(0);

    // return whole pages to the OS while there's a full page of free tail space
    while (new_end < heap_end - PAGE_SIZE) {
        sbrk(-PAGE_SIZE);
        heap_end = sbrk(0);
        malloc_header->amount_of_pages -= 1;
    }

    // if there's still a gap between last used block and heap end, inject a free block
    if (heap_end - new_end > (long)(sizeof(area) + 1)) {
        area *leftover = (area *)new_end;
        leftover->marker = BLOCK_MARKER;
        leftover->in_use = false;
        leftover->prev = prev_used_block;
        leftover->next = NULL;
        leftover->length = heap_end - new_end - sizeof(area);
        prev_used_block->next = leftover;
    }
}

static int *add_used_block(ssize_t size) {
    my_stats *malloc_header = get_malloc_header();

    // acquire lock
    while (malloc_header->my_simple_lock)
        sleep(1);
    malloc_header->my_simple_lock = true;

    // best-fit search
    area *block = (area *)((char *)heap_start + sizeof(my_stats));
    area *smallest_block = NULL;
    area *last_block = block;

    while (block != NULL) {
        assert(block->marker == BLOCK_MARKER);
        if (!block->in_use && block->length >= (uint32_t)size) {
            if (smallest_block == NULL || block->length < smallest_block->length)
                smallest_block = block;
        }
        last_block = block;
        block = block->next;
    }

        // no block fits — grow the heap until the last block is large enough
    if (smallest_block == NULL) {
        while (last_block->length < (uint32_t)size) {
            sbrk(PAGE_SIZE);
            last_block->length += PAGE_SIZE;
            malloc_header->amount_of_pages += 1;
        }
        smallest_block = last_block;
    }

    // mark block in use
    smallest_block->in_use = true;

    // ensure room for a new free block after the used portion
    int must_have = (int)smallest_block->length - (int)size - (int)sizeof(area) - 1;
    if (must_have <= 0) {
        sbrk(PAGE_SIZE);
        malloc_header->amount_of_pages += 1;
        last_block->length += PAGE_SIZE;
        must_have = (int)smallest_block->length - (int)size - (int)sizeof(area) - 1;
    }

        // split: carve a new free block out of the remainder
    int remaining_size = must_have + 1;
    area *new_block = (area *)((char *)smallest_block + sizeof(area) + size);
    new_block->marker = BLOCK_MARKER;
    new_block->in_use = false;
    new_block->prev = smallest_block;
    new_block->next = smallest_block->next;
    if (new_block->next != NULL)
        new_block->next->prev = new_block;
    smallest_block->next = new_block;
    new_block->length = remaining_size;
    smallest_block->length = size;
    malloc_header->amount_of_blocks += 1;

    malloc_header->my_simple_lock = false;
    return (int *)((char *)smallest_block + sizeof(area));
}

int *an_malloc(ssize_t size) {
    // first call: grab a page and set up the header + first block
    if (heap_start == NULL) {
        heap_start = sbrk(0);
        sbrk(PAGE_SIZE);
    }

    char *heap_end = sbrk(0);
    long int length = heap_end - (char *)heap_start;
    my_stats *malloc_header = (my_stats *)heap_start;

    if (malloc_header->magical_bytes != MAGICAL_BYTES) {
        malloc_header->magical_bytes = MAGICAL_BYTES;
        malloc_header->my_simple_lock = false;
        malloc_header->amount_of_blocks = 1;
        malloc_header->amount_of_pages = 1;

        area *first_block = (area *)((char *)heap_start + sizeof(my_stats));
        first_block->marker = BLOCK_MARKER;
        first_block->in_use = false;
        first_block->length = length - sizeof(my_stats) - sizeof(area);
        first_block->next = NULL;
        first_block->prev = NULL;
    }

    return add_used_block(size);
}

bool an_free(void *ptr) {
    my_stats *malloc_header = get_malloc_header();

    // acquire lock
    while (malloc_header->my_simple_lock)
        sleep(1);
    malloc_header->my_simple_lock = true;

    // ptr points to the data region — step back to find the block header
    area *block = (area *)((char *)ptr - sizeof(area));

    if (block->marker != BLOCK_MARKER) {
        malloc_header->my_simple_lock = false;
        return false; // not a valid malloc'd pointer
    }

    block->in_use = false;
    memset(ptr, 0, block->length);

    // coalesce with next block if its free
    if (block->next != NULL && !block->next->in_use) {
        area *dead = block->next;
        block->next = dead->next;
        if (dead->next != NULL)
            dead->next->prev = block;
        block->length += sizeof(area) + dead->length;
        memset((void *)dead, 0, sizeof(area) + dead->length);
        malloc_header->amount_of_blocks -= 1;
    }

    // coalesce with previous block if its free
    if (block->prev != NULL && !block->prev->in_use) {
        area *prev = block->prev;
        prev->length += sizeof(area) + block->length;
        prev->next = block->next;
        if (block->next != NULL)
            block->next->prev = prev;
        malloc_header->amount_of_blocks -= 1;
    }

    reduce_heap_size_if_possible();

    malloc_header->my_simple_lock = false;
    return true;
}