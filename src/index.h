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
#ifndef PT_INDEX_H
#define PT_INDEX_H

#include "arena.h"
#include "index.h"

/* ============================================================================
 * SEGREGATED LIST ROUTING & TRAILING-EDGE TRANSLATION
 * ============================================================================ */

// Maps a given word size to its corresponding segregated list index
static inline int pt_idx_list_select(word_t size_words) 
{
    // 4 words -> index 0, 6 words -> index 1
    return (size_words == 4) ? 0 : 1;
}

// Steps from a raw free block's header to its trailing-edge link structure
static inline pt_link_t* pt_idx_hdr_to_link(word_t* hdr_ptr, word_t size_words) 
{
    // Back up exactly 3 words from the end of the block boundary
    return (pt_link_t*)(hdr_ptr + size_words - 3);
}

// Steps backward from a trailing-edge link node to find its true header pointer
static inline word_t* pt_idx_link_to_hdr(pt_link_t* link_ptr, word_t size_words) 
{
    return ((word_t*)link_ptr) - size_words + 3;
}

/* ============================================================================
 * SEGREGATED LIST MUTATORS
 * ============================================================================ */

static inline void pt_link_insert(pt_link_t* prev, pt_link_t* node, pt_link_t* next)
{	
	prev->next = node;
	node->prev = prev;
	node->next = next;
	next->prev = node;
}

// LIFO in case of producer
static inline void pt_idx_list_insert(pt_arena_t* arena, word_t* hdr_ptr, word_t size_words) 
{
    // 1. Identify target list queue
    int idx = pt_idx_list_select(size_words);
    
    // 2. Map the structural node onto the trailing edge of the free block
	pt_list_t* list = &arena->segregate[idx];
	pt_link_t* node = pt_idx_hdr_to_link(hdr_ptr, size_words);
    
    // 3. Complete the link transactions (Appending to the circular queue) 
	pt_link_insert(&list->head, node, list->head.next);

    // 4. Stamp the Boundary Tags (Positive values declare the block is FREE)
    hdr_ptr[0] = size_words;
    node->ftr[0] = size_words; 
}

// FIFO in case of consumer
static inline void pt_idx_list_add(pt_arena_t* arena, word_t* hdr_ptr, word_t size_words) 
{
    // 1. Identify target list queue
    int idx = pt_idx_list_select(size_words);
    
    // 2. Map the structural node onto the trailing edge of the free block
	pt_list_t* list = &arena->segregate[idx];
	pt_link_t* node = pt_idx_hdr_to_link(hdr_ptr, size_words);
    
    // 3. Complete the link transactions (Appending to the circular queue) 
	pt_link_insert(list->tail.prev, node, &list->tail);

    // 4. Stamp the Boundary Tags (Positive values declare the block is FREE)
    hdr_ptr[0] = size_words;
    node->ftr[0] = size_words; 
}


static inline void pt_idx_list_unlink(word_t* hdr_ptr, word_t size_words) 
{
    // 1. Map to the trailing edge link node using the known block size
    pt_link_t* node = pt_idx_hdr_to_link(hdr_ptr, size_words);
	pt_link_t* prev = node->prev;
	pt_link_t* next = node->next;
    
    // 2. Extract the node from the circular doubly linked list
    prev->next = node->next;
    next->prev = node->prev;
    
    // 3. Defensive Engineering: Clear out links to kill stray pointers in memory
    node->next = NULL;
    node->prev = NULL;
}

// Defined color invariants for our balanced tree
#define PT_RB_RED   0
#define PT_RB_BLACK 1

/* ============================================================================
 * LOGICAL NAVIGATION PRIMITIVES
 * ============================================================================ */

// Read the absolute size of a tree node block directly from its trailing edge
static inline word_t pt_idx_tree_node_size(pt_redblack_t* node) 
{
    return node->ftr[0];
}

// Universal Header-to-Tree Mapping
static inline pt_redblack_t* pt_idx_hdr_to_tree(word_t* hdr_ptr, word_t size_words) 
{
    // Math: If size is 8: hdr_ptr + 8 - 8 = hdr_ptr (Perfect Overlap)
    //       If size > 8: Index points directly to the trailing 8-word descriptor block
    return (pt_redblack_t*)(hdr_ptr + size_words - 8);
}

// Universal Tree-to-Header Mapping
static inline word_t* pt_idx_tree_to_hdr(pt_redblack_t* node_ptr, word_t size_words) 
{
    return (word_t*)node_ptr - size_words + 8;
}

/* ============================================================================
 * CORE PROTOTYPES
 * ============================================================================ */

// Helper to calculate the maximum block size available within a given subtree footprint
static inline word_t pt_node_total_max(pt_redblack_t* n) 
{
    if (!n) return 0;
    word_t self_size = n->ftr[0]; // Free blocks store positive size in the footer
    word_t max_child = (n->left_max > n->right_max) ? n->left_max : n->right_max;
    return (self_size > max_child) ? self_size : max_child;
}

// Bubbles augmentation corrections from a modified node all the way up to the root anchor
static inline void pt_node_propagate_aug(pt_redblack_t* child) 
{
	if((child)) {
		pt_redblack_t* parent = child->parent;
		while((parent)) {
			word_t* child_max = (parent->left == child) ? &parent->left_max : &parent->right_max;
			word_t max = pt_node_total_max(child);
			if(*child_max == max) {
				return;
			} else {
				child = parent;
				parent = child->parent;
				*child_max = max;
			}
		}
	}
}

static inline void pt_tree_rotate_left(pt_arena_t* arena, pt_redblack_t* x) 
{
    pt_redblack_t* y = x->right;
    x->right = y->left;
	x->right_max = y->left_max;
    if (y->left) y->left->parent = x;
    
    y->parent = x->parent;
    if (!x->parent)           arena->root = y;
    else if (x == x->parent->left) x->parent->left = y;
    else                      x->parent->right = y;
    
    y->left = x;
	y->left_max = pt_node_total_max(x);
    x->parent = y;
}

static inline void pt_tree_rotate_right(pt_arena_t* arena, pt_redblack_t* y) 
{
    pt_redblack_t* x = y->left;
    y->left = x->right;
	y->left_max = x->right_max;
    if (x->right) x->right->parent = y;
    
    x->parent = y->parent;
    if (!y->parent)           arena->root = x;
    else if (y == y->parent->left) y->parent->left = x;
    else                      y->parent->right = x;
    
    x->right = y;
	x->right_max = pt_node_total_max(y);
    y->parent = x;
}

static inline pt_redblack_t* pt_idx_tree_find_first_fit(pt_redblack_t* current, word_t size_words) 
{
    while (current != NULL) {

        // Step 1: Check lowest memory addresses first (Left Subtree).
        // If a fit exists down here, address-ordering guarantees it's our optimal first-fit.
        if (current->left_max >= size_words) {
            current = current->left;
            continue;
        }

        // Step 2: Evaluate the current node.
        // If the left branch didn't have enough room, check if this specific block fits.
        if (current->ftr[0] >= size_words) {
            return current;
        }

        // Step 3: Fallback to higher addresses (Right Subtree).
        // We only step here if both the left branch and the current node failed,
        // but the right subtree max promises a valid block exists deeper.
        if (current->right_max >= size_words) {
            current = current->right;
            continue;
        }

        // Step 4: Safety Sentinel.
        // If our max augmentations are perfectly synchronized, the early exit check
        // ensures we can never logically land here. If we do, the tree is exhausted.
        break;
    }

    return NULL;
}

// Handles updating a right tree node in-place as it absorbs memory from its left side
static inline void pt_idx_tree_absorb_stationary_right(pt_redblack_t* right_node, word_t final_size) {
    right_node->ftr[0] = final_size;
    pt_node_propagate_aug(right_node);
}

// Handles shifting a left tree node rightward within the newly unified block boundaries
static inline pt_redblack_t* pt_idx_tree_migrate(pt_arena_t* arena, pt_redblack_t* old_node, 
                                                 word_t* final_hdr, word_t final_size, pt_redblack_t* new_node) {
    new_node->ftr[0] = final_size; // Inject new unified size tag
    
    // 2. Patch Parent's child reference
    if (new_node->parent == NULL) {
        arena->root = new_node;
    } else if (new_node->parent->left == old_node) {
        new_node->parent->left = new_node;
    } else {
        new_node->parent->right = new_node;
    }
    
    // 3. Patch Children's parent references
    if (new_node->left)  new_node->left->parent  = new_node;
    if (new_node->right) new_node->right->parent = new_node;
    
    // 4. Update memory block headers and ancestral augmentations
    final_hdr[0] = final_size;
    pt_node_propagate_aug(new_node);
	return new_node;
}

// Handles shifting a left tree node rightward within the newly unified block boundaries
static inline pt_redblack_t* pt_idx_tree_migrate_rightward(pt_arena_t* arena, pt_redblack_t* old_node, 
                                                 word_t* final_hdr, word_t final_size) {
    pt_redblack_t* new_node = pt_idx_hdr_to_tree(final_hdr, final_size);
    
    // 1. Shallow Copy structural descriptors instantly
	// >>> CRITICAL FIX: Prevent undefined behavior on overlapping struct memory <<<
	intptr_t* new_hdr = new_node->hdr;
	intptr_t* old_hdr = old_node->hdr;
	new_hdr[7] = old_hdr[7];
	new_hdr[6] = old_hdr[6];
	new_hdr[5] = old_hdr[5];
	new_hdr[4] = old_hdr[4];
	new_hdr[3] = old_hdr[3];
	new_hdr[2] = old_hdr[2];
	new_hdr[1] = old_hdr[1];
	new_hdr[0] = old_hdr[0];

	return pt_idx_tree_migrate(arena, old_node, final_hdr, final_size, new_node);
}

// Handles shifting a right tree node leftward within the newly unified block boundaries
static inline pt_redblack_t* pt_idx_tree_migrate_leftward(pt_arena_t* arena, pt_redblack_t* old_node, 
                                                 word_t* final_hdr, word_t final_size) {
    pt_redblack_t* new_node = pt_idx_hdr_to_tree(final_hdr, final_size);
    
    // 1. Shallow Copy structural descriptors instantly
	// >>> CRITICAL FIX: Prevent undefined behavior on overlapping struct memory <<<
	intptr_t* new_hdr = new_node->hdr;
	intptr_t* old_hdr = old_node->hdr;
	new_hdr[0] = old_hdr[0];
	new_hdr[1] = old_hdr[1];
	new_hdr[2] = old_hdr[2];
	new_hdr[3] = old_hdr[3];
	new_hdr[4] = old_hdr[4];
	new_hdr[5] = old_hdr[5];
	new_hdr[6] = old_hdr[6];
	new_hdr[7] = old_hdr[7];

	return pt_idx_tree_migrate(arena, old_node, final_hdr, final_size, new_node);
}

void pt_idx_tree_unlink(pt_arena_t* arena, pt_redblack_t* z);
void pt_idx_tree_insert(pt_arena_t* arena, pt_redblack_t* x, word_t* final_hdr, word_t final_size);
word_t* pt_idx_coalesce_state_machine(pt_arena_t* arena, word_t* hdr, word_t* ftr, word_t* out_size_words);
void pt_idx_list_split_state_machine(pt_arena_t* arena, word_t* left_hdr, word_t* final_hdr, word_t* right_hdr, word_t* next_hdr);
void pt_idx_tree_split_state_machine(pt_arena_t* arena, word_t* left_hdr, word_t* final_hdr, word_t* right_hdr, word_t* next_hdr);

#if 0
void* pt_idx_extract_and_split(pt_arena_t* arena, pt_redblack_t* node, word_t r_words);
#endif
word_t* pt_idx_coalesce_state_machine(pt_arena_t* arena, word_t* hdr, word_t* ftr, word_t* out_size_words);

#endif // PT_INDEX_H
