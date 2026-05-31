/* File: include/proteus.h */
#ifndef PROTEUS_H
#define PROTEUS_H

#include <stddef.h>

void* proteus_malloc(size_t size);
void  proteus_free(void* ptr);
void* proteus_realloc(void* ptr, size_t size);
int   proteus_posix_memalign(void** memptr, size_t alignment, size_t size);

#endif // PROTEUS_H
