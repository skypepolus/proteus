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
#ifndef __posix_h__
#define __posix_h__ __posix_h__

#include "arena.h"
#ifdef PT_POSIX 
#include <sys/mman.h>
#include <stddef.h>
#endif

void pt_arena_watermark(pt_arena_t* arena, word_t* final_hdr, word_t size_words, word_t coalesced_size);
#ifndef WASM_PAGE_SIZE 
static inline int pt_platform_purge_pages(void* addr, size_t length) {
#if defined(__linux__)
    return madvise(addr, length, MADV_DONTNEED);
#else
    return madvise(addr, length, MADV_FREE);
#endif
}
#endif

#endif/*__posix_h__*/
