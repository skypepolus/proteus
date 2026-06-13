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
#ifndef PT_ARENA_MT_H
#define PT_ARENA_MT_H
#include <pthread.h>
#include "hybrid_lock.h"

// Extended core arena layout - Strictly cache-line isolated to prevent False Sharing
typedef struct pt_arena {
    struct hybrid lock;

    pt_list_t segregate[2]; // Two small segregated lists
    pt_redblack_t* root;    // Augmented address-ordered First-Fit tree root

	// Runtime OS Page Invariants
    size_t page_size;   // e.g., 4096, 16384, or 65536
    uintptr_t page_mask; // e.g., 4095, 16383, or 65535

	void* empty_superpage_cache; 

	uint8_t reserved_align[64 - sizeof(pt_redblack_t*) - sizeof(size_t) - sizeof(uintptr_t) - sizeof(void*)];
} pt_arena_t;

typedef struct pt_superpage {
    word_t ftr[1];                                // 1 Word (Low Zero Sentinel)
    word_t block_words[PT_HUGE_THRESHOLD_WORDS];  // PT_SUPER_PAGE_WORDS - 4 Words
    word_t hdr[1];                                // 1 Word (High Zero Sentinel)
    pt_arena_t* arena_ptr;                        // 1 Word
    word_t      reserved_align;                   // 1 Word (Maintained for strict 2-word header structural alignment)
} pt_superpage_t;

typedef struct g_pt {
	// Standard atomic int for core counting
	_Atomic int num_cores;
	_Atomic (pt_arena_t*) arenas;
	uint8_t reserved_align[64 - sizeof(_Atomic int) - sizeof(pt_arena_t*)];
} g_pt_t;

#include "watermark.h"

static inline pt_superpage_t* pt_arena_superpage(void* ptr) {
    return (pt_superpage_t*)((uintptr_t)ptr & ~PT_SUPER_PAGE_MASK);
}

#endif // PT_ARENA_MT_H
