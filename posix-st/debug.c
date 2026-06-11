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

#include "primitives.h"
#include "index.h"
#include "debug.h"
#include "proteus.h"
#include <stdio.h>

static void pt_debug_tree_max(pt_redblack_t* node)
{
	if(node) {
		if(node->left_max == pt_node_total_max(node->left)) {
			pt_debug_tree_max(node->left);
		} else {
			char str[64];
			size_t count;
			count = snprintf(str, sizeof(str), "%p\n", pt_idx_tree_to_hdr(node, 0 > node->ftr[0] ? -node->ftr[0] : node->ftr[0]));
			write(2, str, count);
			__builtin_trap();
		}
		if(node->right_max == pt_node_total_max(node->right)) {
			pt_debug_tree_max(node->right);
		} else {
			char str[64];
			size_t count;
			count = snprintf(str, sizeof(str), "%p\n", pt_idx_tree_to_hdr(node, 0 > node->ftr[0] ? -node->ftr[0] : node->ftr[0]));
			write(2, str, count);
			__builtin_trap();
		}
	}
}

static void pt_debug_tree_print_space(pt_redblack_t* node, int space)
{
	if(node) {
		int i;
		pt_debug_tree_print_space(node->right, space + 1);
		for(i = 0; i < space; i++) {
			write(2, " ", 1);
		}
		if(PT_RB_RED == node->color) {
			char str[64];
			size_t count;
			count = snprintf(str, sizeof(str), "\033[0;30m%p\033[0m\n", pt_idx_tree_to_hdr(node, 0 < node->ftr[0] ? node->ftr[0] : node->ftr[0])); // red
			write(2, str, count);
		} else if(PT_RB_BLACK == node->color) {
			char str[64];
			size_t count;
			count = snprintf(str, sizeof(str), "%p\n", pt_idx_tree_to_hdr(node, 0 < node->ftr[0] ? node->ftr[0] : node->ftr[0])); // black
			write(2, str, count);
		} else {
			__builtin_trap();
		}
		pt_debug_tree_print_space(node->left, space + 1);
	}
}

static void pt_debug_tree_print(pt_redblack_t* node)
{
	pt_debug_tree_print_space(node, 0);
}

static void pt_debug_tag(pt_superpage_t* superpage, void* addr)
{
	word_t* hdr;
	word_t* ftr;
	word_t words;
	if((uintptr_t)addr < (uintptr_t)superpage->ftr) {
		__builtin_trap();
	}
	if((uintptr_t)superpage->hdr < (uintptr_t)addr) {
		__builtin_trap();
	}

	for(hdr = superpage->ftr + 1, words = 0 < hdr[0] ? hdr[0] : -hdr[0], ftr = hdr + words - 1;
		(uintptr_t)hdr < (uintptr_t)addr;
		hdr = ftr + 1, words = 0 < hdr[0] ? hdr[0] : -hdr[0], ftr = hdr + words - 1) {
		if(hdr[0] != ftr[0]) {
			__builtin_trap();
		}
	}
	char str[64];
	size_t count;
	count = snprintf(str, sizeof(str), "%ld\033[0;32m %p\033[0m%ld\n", hdr[0], hdr + 1, ftr[0]); // green
	write(2, str, count);
	for(hdr = ftr + 1, words = 0 < hdr[0] ? hdr[0] : -hdr[0], ftr = hdr + words - 1;
		hdr < superpage->hdr;
		hdr = ftr + 1, words = 0 < hdr[0] ? hdr[0] : -hdr[0], ftr = hdr + words - 1) {
		if(hdr[0] != ftr[0]) {
			__builtin_trap();
		}
	}
}

void *pt_debug_malloc(size_t size)
{
	void* ptr = proteus_malloc(size);
    pt_superpage_t* superpage = pt_arena_superpage(ptr);
    pt_arena_t* arena         = superpage->arena_ptr;
	hybrid_lock(arena->lock, 0);
	pt_debug_tree_max(arena->root);
	pt_debug_tree_print(arena->root);
	pt_debug_tag(superpage, ptr);
	hybrid_unlock(arena->lock);
	return ptr;
}

void pt_debug_free(void *ptr)
{
    pt_superpage_t* superpage = pt_arena_superpage(ptr);
    pt_arena_t* arena         = superpage->arena_ptr;
	proteus_free(ptr);
	hybrid_lock(arena->lock, 0);
	pt_debug_tree_max(arena->root);
	pt_debug_tree_print(arena->root);
	hybrid_unlock(arena->lock);
}

void *pt_debug_realloc(void *ptr, size_t size)
{
	ptr = proteus_realloc(ptr, size);
    pt_superpage_t* superpage = pt_arena_superpage(ptr);
    pt_arena_t* arena         = superpage->arena_ptr;
	hybrid_lock(arena->lock, 0);
	pt_debug_tree_max(arena->root);
	pt_debug_tree_print(arena->root);
	pt_debug_tag(superpage, ptr);
	hybrid_lock(arena->lock, 0);
	return ptr;
}

