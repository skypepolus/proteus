#ifndef PROTEUS_H
#define PROTEUS_H

#include <stddef.h>

// Macro to control dynamic symbol exporting
#if defined(__GNUC__) || defined(__clang__)
    #define PT_EXPORT __attribute__((visibility("default")))
#else
    #define PT_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

void  proteus_free(void* ptr);
void* proteus_realloc(void* ptr, size_t size_bytes);
void* proteus_memalign(size_t alignment, size_t size_bytes);

#ifdef __cplusplus
}
#endif

#endif // PROTEUS_H
