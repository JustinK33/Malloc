// AI generated tests
#include "malloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdint.h>
#include <stdbool.h>

// ─── test runner ─────────────────────────────────────────────────────────────

void call_test(void (*test_func)(), const char *msg) {
    pid_t pid = fork();
    if (pid == 0) {
        test_func();
        printf("  PASS: %s\n", msg);
        exit(0);
    } else {
        int status;
        waitpid(pid, &status, 0);
        if (WIFSIGNALED(status))
            printf("  FAIL: %s (crashed with signal %d)\n", msg, WTERMSIG(status));
        else if (WEXITSTATUS(status) != 0)
            printf("  FAIL: %s (exit code %d)\n", msg, WEXITSTATUS(status));
    }
}

// ─── test functions ───────────────────────────────────────────────────────────

// basic: allocate an int, write to it, read it back
void test_basic_malloc(void) {
    int *ptr = an_malloc(sizeof(int));
    assert(ptr != NULL);
    *ptr = 42;
    assert(*ptr == 42);
}

// write across a large allocation without segfaulting
void test_large_allocation(void) {
    uint16_t *ptr = (uint16_t *)an_malloc(5000);
    assert(ptr != NULL);
    for (uint16_t i = 0; i < 2500; i++)
        ptr[i] = i;
    for (uint16_t i = 0; i < 2500; i++)
        assert(ptr[i] == i);
}

// free a pointer and confirm its memory is zeroed
void test_free_zeroes_memory(void) {
    int *ptr = an_malloc(sizeof(int));
    *ptr = 99;
    assert(*ptr == 99);
    an_free(ptr);
    // the block header is just behind ptr — peek at the data region directly
    char *raw = (char *)ptr;
    for (int i = 0; i < (int)sizeof(int); i++)
        assert(raw[i] == 0);
}

// free should return false for a pointer it didn't allocate
void test_free_invalid_pointer(void) {
    int x = 5;
    bool result = an_free(&x);   // stack pointer — no BLOCK_MARKER behind it
    assert(result == false);
}

// multiple allocations must not overlap
void test_no_overlap(void) {
    int *a = an_malloc(sizeof(int));
    int *b = an_malloc(sizeof(int));
    int *c = an_malloc(sizeof(int));
    *a = 1; *b = 2; *c = 3;
    assert(*a == 1);
    assert(*b == 2);
    assert(*c == 3);
    an_free(a);
    an_free(b);
    an_free(c);
}

// free then re-malloc should reuse the same block (best-fit recycles it)
void test_reuse_after_free(void) {
    int *first = an_malloc(64);
    void *addr = (void *)first;
    an_free(first);
    int *second = an_malloc(64);
    // after coalescing and best-fit, the same region should come back
    assert((void *)second == addr);
}

// interleaved alloc/free shouldn't corrupt adjacent live allocations
void test_interleaved_alloc_free(void) {
    int *a = an_malloc(sizeof(int));
    int *b = an_malloc(sizeof(int));
    int *c = an_malloc(sizeof(int));
    *a = 10; *b = 20; *c = 30;
    an_free(b);                         // free the middle one
    int *d = an_malloc(sizeof(int));    // should reuse b's slot
    *d = 99;
    assert(*a == 10);                   // a and c must be untouched
    assert(*c == 30);
    assert(*d == 99);
    an_free(a);
    an_free(c);
    an_free(d);
}

// requesting more than one page forces sbrk — should still work
void test_exceeds_one_page(void) {
    char *ptr = (char *)an_malloc(8000);   // bigger than one 4096-byte page
    assert(ptr != NULL);
    for (int i = 0; i < 8000; i++)
        ptr[i] = (char)(i % 127);
    for (int i = 0; i < 8000; i++)
        assert(ptr[i] == (char)(i % 127));
    an_free(ptr);
}

// stress: many small allocations then free all — should not crash or corrupt
void test_stress_many_allocs(void) {
    int *ptrs[100];
    for (int i = 0; i < 100; i++) {
        ptrs[i] = an_malloc(sizeof(int));
        assert(ptrs[i] != NULL);
        *ptrs[i] = i;
    }
    for (int i = 0; i < 100; i++)
        assert(*ptrs[i] == i);
    for (int i = 0; i < 100; i++)
        an_free(ptrs[i]);
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main(void) {
    printf("running malloc tests\n\n");
    call_test(test_basic_malloc,         "basic malloc and write");
    call_test(test_large_allocation,     "large allocation across multiple pages");
    call_test(test_free_zeroes_memory,   "free zeroes the data region");
    call_test(test_free_invalid_pointer, "free rejects non-malloc pointer");
    call_test(test_no_overlap,           "multiple allocations do not overlap");
    call_test(test_reuse_after_free,     "freed block is reused by next malloc");
    call_test(test_interleaved_alloc_free, "interleaved alloc/free keeps neighbors intact");
    call_test(test_exceeds_one_page,     "allocation larger than one page");
    call_test(test_stress_many_allocs,   "100 sequential allocs then free all");
    printf("\ndone.\n");
    return 0;
}