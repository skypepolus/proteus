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
#include "index.h"
#include "core.h"
#include "posix.h"
#include <string.h>
#include <errno.h>

void pt_arena_watermark(pt_arena_t* arena, word_t* final_hdr, word_t size_words)
{
	pt_redblack_t* node = pt_idx_hdr_to_tree(final_hdr, final_hdr[0]);
	
	if (size_words < PT_INDEX_WATERMARK_WORDS) {
		// If free memory does not exceed 64 KiB threshold
		word_t advised_size = node->hdr[0]; 
		#if 0
		if (coalesced_size < advised_size) {
			// Malloc splitting consumed our old watermark in the interim; lazily heal the state
			node->hdr[0] = coalesced_size;
			return;
		} else 
		#endif
		if (final_hdr[0] - advised_size < PT_INDEX_WATERMARK_WORDS) {
			return;
		} // Unpurged memory on the left has breached our 64 KiB threshold. Purge it.
	}

	uintptr_t payload_start = (uintptr_t)(final_hdr + 1);
	uintptr_t payload_end   = (uintptr_t)node->hdr;
	
	uintptr_t page_start = (payload_start + arena->page_mask) & ~arena->page_mask;
	uintptr_t page_end   = payload_end & ~arena->page_mask;
	
	if (page_start < page_end) {
		word_t* right_hdr   = final_hdr + final_hdr[0];

		// 1. UNLINK: Remove from the tree so it can't be allocated
		pt_idx_tree_unlink(arena, node);

		// 2. INVERT TAGS: Disguise as an allocated block so neighbors don't coalesce and double-unlink
		final_hdr[0] *= -1;
		right_hdr[-1] *= -1;

		// 3. DROP LOCK: Allow parallel allocations
		hybrid_unlock(arena->lock);

		// 4. SYSCALL: Safe, unlocked page table purge
		pt_platform_purge_pages((void*)page_start, page_end - page_start);

		// 5. RE-ACQUIRE LOCK
		hybrid_lock(arena->lock, FREE_SPIN_COUNTER);

		// 6. RE-COALESCE & RE-INSERT
		// Because we inverted the tags to negative, we satisfy the state machine's 
		// precondition. It will safely check if neighbors freed themselves while 
		// we were unlocked, format the final positive tags, and insert it.
		final_hdr = pt_idx_coalesce_state_machine(arena, final_hdr, right_hdr);

		// 7. Re-anchor the geometric vector tracking
		node = pt_idx_hdr_to_tree(final_hdr, final_hdr[0]);
		word_t advised_size = right_hdr - (word_t*)page_start;
		node->hdr[0] = advised_size;
	}
}
