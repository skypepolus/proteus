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

PT_EXPORT void* proteus_malloc(size_t size_bytes);
PT_EXPORT void  proteus_free(void* ptr);
PT_EXPORT void* proteus_realloc(void* ptr, size_t size_bytes);
PT_EXPORT int   proteus_posix_memalign(void** memptr, size_t alignment, size_t size_bytes);

#ifdef __cplusplus
}
#endif

#endif // PROTEUS_H
