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

#include <stdio.h>
#include <stdbool.h>

/**
 * Validates RBT sanity using the "Node-Count-Height" convention.
 * Root-only tree = 1, Empty tree = 0.
 */
static bool is_rbt_node_height_sane(int node_height, int num_nodes) {
    // Edge case: Empty tree
    if (num_nodes == 0) {
        return (node_height == 0);
    }
    
    // Out of bounds safety checks
    if (num_nodes < 0 || node_height <= 0 || node_height > 63) {
        return false;
    }

    unsigned long long N_plus_1 = (unsigned long long)num_nodes + 1;

    // 1. Lower bound check: log2(N + 1) <= h
    // Equivalent to: N + 1 <= 2^h
    if (N_plus_1 > (1ULL << node_height)) {
        return false; // Tree has too many nodes for this short height
    }

    // 2. Upper bound check: h <= 2*log2(N + 1) + 1
    // Equivalent to: h - 1 <= 2*log2(N + 1) -> 2^(h - 1) <= (N + 1)^2
    // (Safe from underflow/negative shifts since node_height > 0)
    unsigned long long max_shifted_boundary = 1ULL << (node_height - 1);
    unsigned long long nodes_squared = N_plus_1 * N_plus_1;

    if (max_shifted_boundary > nodes_squared) {
        return false; // Tree is too tall/unbalanced for an RBT
    }

    return true;
}

static int pt_tree_height(pt_redblack_t* node)
{
	if(node) {
		int left = pt_tree_height(node->left);
		int right = pt_tree_height(node->right);
		return left > right ? left + 1 : right + 1;
	}
	return 0;
}

static int pt_tree_nodes(pt_redblack_t* node)
{
	if(node) {
		return pt_tree_nodes(node->left) + 1 + pt_tree_nodes(node->right);
	}
	return 0;
}

static void pt_debug_max(pt_redblack_t* node)
{
	if(node) {
		if(0 <= node->left_max && node->left_max == pt_node_total_max(node->left)) {
			pt_debug_max(node->left);
		} else {
			char str[64];
			size_t count;
			count = snprintf(str, sizeof(str), "%s%ld%s%p%s%ld%s\n", "\033[1;31m", node->left_max, "\033[0m", node, "\033[1;32m", node->right_max, "\033[0m");
			write(2, str, count);
			__builtin_trap();
		}
		if(0 > node->ftr[0]) {
			char str[64];
			size_t count;
			count = snprintf(str, sizeof(str), "%p%s%ld%s\n", node, "\033[1;31m", node->ftr[0], "\033[0m");
			write(2, str, count);
			__builtin_trap();
		}
		if(0 <= node->right_max && node->right_max == pt_node_total_max(node->right)) {
			pt_debug_max(node->right);
		} else {
			char str[64];
			size_t count;
			count = snprintf(str, sizeof(str), "%s%ld%s%p%s%ld%s\n", "", node->left_max, "\033[1;32m", node, "\033[1;31m", node->right_max, "\033[0m");
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
			count = snprintf(str, sizeof(str), "%s%p%s\n", "\033[1;31m", pt_idx_tree_to_hdr(node, 0 < node->ftr[0] ? node->ftr[0] : node->ftr[0]), "\033[0m"); // red
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

static void pt_debug_tree(pt_redblack_t* root)
{
	if(!is_rbt_node_height_sane(pt_tree_height(root), pt_tree_nodes(root))) {
		pt_debug_tree_print(root);
	}
}

static void pt_debug_tag(pt_superpage_t* superpage, void* addr)
{
	char str[128];
	size_t count;
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
		ftr < (word_t*)addr;
		hdr = ftr + 1, words = 0 < hdr[0] ? hdr[0] : -hdr[0], ftr = hdr + words - 1) {
		if(hdr[0] != ftr[0]) {
			count = snprintf(str, sizeof(str), "%s%ld%s%p%s%ld%s", "\033[1;31m", hdr[0], "\033[0m", hdr + 1, "\033[0;30m", ftr[0], "\033[0m"); // red
			write(2, str, count);
			write(2, "\n", 1);
			__builtin_trap();
		}
	}
	count = snprintf(str, sizeof(str), "%ld%s%p%s%ld", hdr[0], "\033[1;32m", hdr + 1, "\033[0m", ftr[0]); // green
	write(2, str, count);
	write(2, "\n", 1);
	for(hdr = ftr + 1, words = 0 < hdr[0] ? hdr[0] : -hdr[0], ftr = hdr + words - 1;
		hdr < superpage->hdr;
		hdr = ftr + 1, words = 0 < hdr[0] ? hdr[0] : -hdr[0], ftr = hdr + words - 1) {
		if(hdr[0] != ftr[0]) {
			count = snprintf(str, sizeof(str), "%s%ld%s%p%s%ld%s", "\033[1;31m", hdr[0], "\033[0m", hdr + 1, "\033[1;31m", ftr[0], "\033[0m"); // red
			write(2, str, count);
			write(2, "\n", 1);
			__builtin_trap();
		}
	}
}

void *pt_debug_malloc(size_t size)
{
	void* ptr = proteus_memalign(1 << (size % 8), size);
    pt_superpage_t* superpage = pt_arena_superpage(ptr);
    pt_arena_t* arena         = superpage->arena_ptr;
	hybrid_lock(arena->lock, 0);
	pt_debug_max(arena->root);
	pt_debug_tree(arena->root);
	pt_debug_tag(superpage, ptr);
	hybrid_unlock(arena->lock);
	return ptr;
}

void pt_debug_free(void *ptr)
{
    pt_superpage_t* superpage = pt_arena_superpage(ptr);
    pt_arena_t* arena         = superpage->arena_ptr;
	pt_debug_tag(superpage, ptr);
	proteus_free(ptr);
	hybrid_lock(arena->lock, 0);
	pt_debug_max(arena->root);
	pt_debug_tree(arena->root);
	hybrid_unlock(arena->lock);
}

void *pt_debug_realloc(void *ptr, size_t size)
{
	ptr = proteus_realloc(ptr, size);
    pt_superpage_t* superpage = pt_arena_superpage(ptr);
    pt_arena_t* arena         = superpage->arena_ptr;
	hybrid_lock(arena->lock, 0);
	pt_debug_max(arena->root);
	pt_debug_tree(arena->root);
	pt_debug_tag(superpage, ptr);
	hybrid_lock(arena->lock, 0);
	return ptr;
}

