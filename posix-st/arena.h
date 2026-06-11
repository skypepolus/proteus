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
#ifndef PT_ARENA_H
#define PT_ARENA_H

#include "primitives.h"
#include "arena-st.h"
#include <unistd.h>

#define pt_arena_watermark_yes(superpage, ftr_ptr) __builtin_expect(ftr_ptr + 1 == superpage->hdr, 0)

static inline pt_arena_t* pt_arena_get_local(void) 
{
    if (__builtin_expect(g_pt.num_cores == 0, 0)) {
		enum { alignment = sizeof(word_t) * 2, mask = alignment - 1 };
		word_t* addr;
        // Slow path: Only hit once in the entire application lifetime
        pt_arena_t* arena = g_pt.arenas;

		arena->page_size = sysconf(_SC_PAGESIZE);
		arena->page_mask = arena->page_size - 1;

        // Initialize your logical list sentinels to point back to themselves
        arena->segregate[0].head.next = &arena->segregate[0].tail;
        arena->segregate[0].tail.prev = &arena->segregate[0].head;
        
        arena->segregate[1].head.next = &arena->segregate[1].tail;
        arena->segregate[1].tail.prev = &arena->segregate[1].head;

		addr = (word_t*)(((uintptr_t)sbrk(0) + alignment + mask) & ~(uintptr_t)mask); 
		if(brk(addr)) __builtin_trap();
		g_pt.superpage->hdr = --addr;
		g_pt.superpage->hdr[0] = 0;
		g_pt.superpage->ftr = --addr;
		g_pt.superpage->ftr[0] = 0;
		g_pt.superpage->arena_ptr = g_pt.arenas;
        g_pt.num_cores = 1;
    }
	return g_pt.arenas;
}

#endif // PT_ARENA_H
