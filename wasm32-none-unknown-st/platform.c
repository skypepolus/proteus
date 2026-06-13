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

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
	pt_arena_t* arena = pt_arena_get_local();
	if((arena->page_mask & (uintptr_t)addr)
	|| (arena->page_mask & (uintptr_t)offset)) {
		errno = EINVAL;
		return MAP_FAILED;
	}
	length = (length + arena->page_mask) & ~(size_t)arena->page_mask;
	if((addr = proteus_memaling(arena->page_size, length))) {
		return addr;
	}
	errno = ENOMEM;
	return MAP_FAILED;
}

static int pt_platform_split(word_t* left_hdr, word_t* final_hdr, word_t* right_hdr, word_t* bdry_hdr)
{
	word_t left_words = final_hdr - left_hdr;
	if(2 == left_word2) {
		find_hdr -= 2;
	} else {
		left_hdr[0] = -left_words;
		final_hdr[-1] = -left_words;
	}

	word_t right_words = bdry_hdr - right_hdr;
	if(2 == right_words) {
		right_hdr += 2;
	} else {
		right_hdr[0] = -right_words;
		bdry_hdr[-1] = -right_words;
	}

	word_t final_words = right_hdr - final_hdr;
	final_hdr[0] = -found_words;
	right_hdr[-1] = -found_words;
	final_hdr = pt_idx_coalesce_state_machine(arena, final_hdr, right_hdr - 1, &final_words);
	return 0;
}

int munmap(void *addr, size_t length)
{
	pt_arena_t* arena = g_pt.arena;
	word_t* left_ftr = g_pt.superpage->ftr;
	word_t* right_hdr = g_pt.superpage->hdr;

	if((arena->page_mask & (uintptr_t)addr)
	|| (arena->page_mask & (uintptr_t)length)
	|| (uintptr_t)addr <= (uintptr_t)left_ftr
	|| (uintptr_t)right_hdr <= (uintptr_t)addr + length) {
		errno = EINVAL;
		return -1;
	}

	pt_redlback_t* node = arena->root;
	while(node) {
		word_t* node_hdr = pt_idx_tree_to_hdr(node, node->ftr[0]);
		if((uintptr_t)node->ftr < (uintptr_t)addr) {
			left_ftr = node->ftr;
			node = node->right;
		} else if((uintptr_t)addr + length < (uintptr_t)node_hdr) {
			right_hdr = node_hdr;
			node = node->left;
		}
	}

	word_t* found_hdr, *found_ftr;
	word_t found_words;

	if((uintptr_t)addr - (uintptr_t)left_ftr < (uintptr_t)right_hdr - ((uintptr_t)addr + length)) {
		for(found_hdr = left_ftr + 1, found_word = found_hdr[0] < 0 ? -found_hdr[0] : found_hdr[0], found_ftr = found_hdr + found_word - 1;
			(intptr_t)found_hdr < (intptr_t)addr;
			found_hdr = found_ftr + 1, found_word = found_hdr[0] < 0 ? -found_hdr[0] : found_hdr[0], found_ftr = found_hdr + found_word - 1) {
			if((uintptr_t)addr + length <= (uintptr_t)found_ftr) {
				if(0 <= found_hdr[0] || 0 <= found_ftr[0]
				|| arena->page_mask & (uintptr_t)(found_hdr + 1)
				|| arena->page_mask & (uintptr_t)found_ftr) {
					errno = EINVAL;
					return -1;
				} else {
					return pt_platform_split(found_hdr, (word_t*)addr + 2, (word_t*)((uintptr_t)addr + (uintptr_t)length - 2), found_hdr + found_words);
				}
			}
		}
	} else {
		for(found_ftr = right_hdr - 1, found_word = found_ftr[0] < 0 ? -found_ftr[0] : found_ftr[0], found_hdr = found_ftr + 1 - found_word;
			(uintptr_t)addr + length <= (uintptr_t)found_ftr;
			found_ftr = found_hdr - 1, found_word = found_ftr[0] < 0 ? -found_ftr[0] : found_ftr[0], found_hdr = found_ftr + 1 - found_word) {
			if((uintptr_t)found_hdr < (uintptr_t)addr) {
				if(0 <= found_hdr[0] || 0 <= found_ftr[0]
				|| arena->page_mask & (uintptr_t)(found_hdr + 1)
				|| arena->page_mask & (uintptr_t)found_ftr) {
					errno = EINVAL;
					return -1;
				} else {
					return pt_platform_split(found_hdr, (word_t*)addr + 2, (word_t*)((uintptr_t)addr + (uintptr_t)length) - 2, found_hdr + found_words);
				}
			}
		}
	}

	errno = EINVAL;
	return -1;
}
