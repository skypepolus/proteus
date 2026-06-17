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
#include "posix.h"

#ifndef WASM_PAGE_SIZE
#include <sys/mman.h>
#include <unistd.h>
#endif

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

__attribute__((aligned(64))) g_pt_t g_pt;

void* pt_arena_watermark_release(pt_arena_t* arena, pt_superpage_t* superpage, word_t* final_hdr, word_t size_words)
{
	if(__builtin_expect(final_hdr + final_hdr[0] == superpage->hdr, 0)) {
		pt_idx_tree_unlink(arena, pt_idx_hdr_to_tree(final_hdr, final_hdr[0]));
		superpage->hdr = final_hdr;
		superpage->hdr[0] = 0;
		if(-1 == brk((void*)(superpage->hdr + 1))) {
			__builtin_trap();
		}
	} else {
		pt_arena_watermark(arena, final_hdr, size_words);
	}
	return NULL;
}
