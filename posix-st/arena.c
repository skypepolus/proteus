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
#include "arena.h"
#include "core.h"
#include "index.h"
#include <sys/mman.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

__attribute__((aligned(64))) g_pt_t g_pt;

void* pt_arena_watermark_release(pt_arena_t* arena, pt_superpage_t* superpage, word_t* final_hdr, word_t coalesced_size)
{
	pt_redblack_t* node = pt_idx_hdr_to_tree(final_hdr, coalesced_size);

	word_t* addr = (word_t*)(((uintptr_t)(final_hdr + 8 + 1) + PT_INDEX_WATERMARK_MASK) & ~(uintptr_t)PT_INDEX_WATERMARK_MASK);

	superpage->hdr = addr - 1;
	pt_idx_tree_migrate_leftward(arena, node, final_hdr, superpage->hdr - final_hdr);
	if(brk(addr)) __builtin_trap();
	superpage->hdr[0] = 0;
	return NULL;
}
