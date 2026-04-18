#ifndef MALLOC_H
#define MALLOC_H

#include <unistd.h>
#include <stdbool.h>

int  *an_malloc(ssize_t size);
bool  an_free(void *ptr);

#endif