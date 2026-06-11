/*
 * Copyright 2026 Young H. Song
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "platform.h"
#include "arena.h"
#include "proteus.h"
#include <stddef.h>
#include <string.h>
#include <errno.h>

/* ============================================================================
 * STANDARD ALLOCATOR INTERPOSITION BRIDGE
 * ============================================================================ */

PT_EXPORT void* malloc(size_t size) {
    return proteus_malloc(size);
}

PT_EXPORT void free(void* ptr) {
    proteus_free(ptr);
}

PT_EXPORT void* realloc(void* ptr, size_t size) {
    return proteus_realloc(ptr, size);
}

PT_EXPORT void* calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void* ptr = proteus_malloc(total);
    if (ptr) {
        // Zero-fill allocated tracking space
        return memset(ptr, 0, total);
    }
    return ptr;
}

PT_EXPORT void * reallocf(void *ptr, size_t size) {
	// Standard BSD behavior: reallocf(ptr, 0) completely frees the pointer and returns NULL
	if (__builtin_expect(0 == size, 0)) {
        proteus_free(ptr);
        return NULL;
    }

    void* new_ptr = proteus_realloc(ptr, size);
    
    // If reallocation fails, perform a defensive fallback purge on the original tracking block
    if (__builtin_expect(NULL == new_ptr, 0)) {
        proteus_free(ptr);
    }
    
    return new_ptr;
}

PT_EXPORT int posix_memalign(void** memptr, size_t alignment, size_t size) {
	if((memptr) && sizeof(void*) <= alignment && 0 == (alignment & (alignment - 1))  && 0 == alignment % sizeof(void*)) {
		*memptr = proteus_memalign(alignment, size);
		return (*memptr) ? 0 : ENOMEM;
	}
    return EINVAL;
}

PT_EXPORT void* memalign(size_t alignment, size_t size) {
	if(sizeof(void*) <= alignment && 0 == (alignment & (alignment - 1))  && 0 == alignment % sizeof(void*)) {
		return proteus_memalign(alignment, size);
	}
	errno = EINVAL;
	return NULL;
}

PT_EXPORT void* aligned_alloc(size_t alignment, size_t size) { 
	if(0 == alignment % size) {
		return memalign(alignment, size);
	}
	errno = EINVAL;
	return NULL;
}

PT_EXPORT void* valloc(size_t size) {
	pt_arena_t* arena = pt_arena_get_local();
	return memalign(arena->page_size, size);
}

PT_EXPORT void* pvalloc(size_t size) {
	pt_arena_t* arena = pt_arena_get_local();
	return memalign(arena->page_size , (size + arena->page_mask) & ~arena->page_mask);
}
