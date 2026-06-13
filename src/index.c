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
#include "index.h"
#ifdef PT_POSIX 
#include <string.h>
#endif

word_t* pt_idx_coalesce_state_machine(pt_arena_t* arena, word_t* hdr, word_t* ftr, word_t* out_size_words) 
{
    // 1. Double free defense & direct negation
    if (__builtin_expect(0 <= *hdr, 0)) __builtin_trap(); 
    word_t current_size = -*hdr;
    
    word_t left_tag  = hdr[-1]; 
    word_t right_tag = ftr[1];  

    // 2. Branchless status token collection
    unsigned left_state  = (2 <= left_tag) + (4 <= left_tag) + (8 <= left_tag);
    unsigned right_state = (2 <= right_tag) + (4 <= right_tag) + (8 <= right_tag);

    // 3. Pack neighbors into a 4-bit key (16 possible execution values)
    unsigned neighbor_state = (left_state << 2) | right_state;

    word_t* final_hdr = hdr;
    word_t  final_size = current_size;

    /* ============================================================================
     * SWITCH MATRIX 1: UNLINKING AND STRUCTURAL SHORTCUTS
     * ============================================================================ */
    switch (neighbor_state) {
        /* --- BLOCK A: No Tree Shortcuts (Standard List/Remnant Merges) --- */
        case 0x0: // Left Busy, Right Busy
            break;

        case 0x1: // Left Busy, Right Remnant
            final_size += right_tag;
            break;

        case 0x2: // Left Busy, Right List
            pt_idx_list_unlink(ftr + 1, right_tag);
            final_size += right_tag;
            break;

        case 0x4: // Left Remnant, Right Busy
            final_hdr   = hdr - left_tag;
            final_size += left_tag;
            break;

        case 0x5: // Left Remnant, Right Remnant
            final_hdr   = hdr - left_tag;
            final_size += left_tag + right_tag;
            break;

        case 0x6: // Left Remnant, Right List
            pt_idx_list_unlink(ftr + 1, right_tag);
            final_hdr   = hdr - left_tag;
            final_size += left_tag + right_tag;
            break;

        case 0x8: // Left List, Right Busy
            final_hdr   = hdr - left_tag;
            pt_idx_list_unlink(final_hdr, left_tag);
            final_size += left_tag;
            break;

        case 0x9: // Left List, Right Remnant
            final_hdr   = hdr - left_tag;
            pt_idx_list_unlink(final_hdr, left_tag);
            final_size += left_tag + right_tag;
            break;

        case 0xA: // Left List, Right List
            final_hdr   = hdr - left_tag;
            pt_idx_list_unlink(final_hdr, left_tag);
            pt_idx_list_unlink(ftr + 1, right_tag);
            final_size += left_tag + right_tag;
            break;

        /* --- BLOCK B: Stationary Right-Tree Shortcuts (Immediate Returns) --- */
        case 0x3: // Left Busy, Right Tree
			final_size += right_tag;
            pt_idx_tree_absorb_stationary_right(pt_idx_hdr_to_tree(ftr + 1, right_tag), final_size);
			final_hdr[0] = final_size;
            *out_size_words = final_size;
            return final_hdr;

        case 0x7: // Left Remnant, Right Tree
            final_hdr = hdr - left_tag;
			final_size += left_tag + right_tag;
            pt_idx_tree_absorb_stationary_right(pt_idx_hdr_to_tree(ftr + 1, right_tag), final_size);
			final_hdr[0] = final_size;
            *out_size_words = final_size;
            return final_hdr;

        case 0xB: // Left List, Right Tree
            final_hdr = hdr - left_tag;
            pt_idx_list_unlink(final_hdr, left_tag);
			final_size += left_tag + right_tag;
            pt_idx_tree_absorb_stationary_right(pt_idx_hdr_to_tree(ftr + 1, right_tag), final_size);
			final_hdr[0] = final_size;
            *out_size_words = final_size;
            return final_hdr;

        /* --- BLOCK C: Left-Tree Rightward Migrations (Immediate Returns) --- */
        case 0xC: // Left Tree, Right Busy
            final_hdr  = hdr - left_tag;
            final_size += left_tag;
            pt_idx_tree_migrate_rightward(arena, pt_idx_hdr_to_tree(final_hdr, left_tag), final_hdr, final_size);
            *out_size_words = final_size;
            return final_hdr;

        case 0xD: // Left Tree, Right Remnant
            final_hdr  = hdr - left_tag;
            final_size += left_tag + right_tag;
            pt_idx_tree_migrate_rightward(arena, pt_idx_hdr_to_tree(final_hdr, left_tag), final_hdr, final_size);
            *out_size_words = final_size;
            return final_hdr;

        case 0xE: // Left Tree, Right List
            pt_idx_list_unlink(ftr + 1, right_tag);
            final_hdr  = hdr - left_tag;
            final_size += left_tag + right_tag;
            pt_idx_tree_migrate_rightward(arena, pt_idx_hdr_to_tree(final_hdr, left_tag), final_hdr, final_size);
            *out_size_words = final_size;
            return final_hdr;

        /* --- BLOCK D: Double Tree Collision --- */
        case 0xF: // Left Tree, Right Tree
            // Keep right stationary, completely unlink left node to prevent duplicates
            final_hdr = hdr - left_tag;
            pt_idx_tree_unlink(arena, pt_idx_hdr_to_tree(final_hdr, left_tag));
            final_size += left_tag + right_tag;
            pt_idx_tree_absorb_stationary_right(pt_idx_hdr_to_tree(ftr + 1, right_tag), final_size);
			final_hdr[0] = final_size;
            *out_size_words = final_size;
            return final_hdr;

		default:
			__builtin_trap(); 
    }

    *out_size_words = final_size;

	/* ============================================================================
     * SWITCH MATRIX 2: RE-INDEXING ROUTING TABLE
     * ============================================================================ */
    switch ((unsigned)final_size >> 1) {
		case 0:
			__builtin_trap(); 
        case 1:
            // Remnant space (2 words). Completely passive.
			final_hdr[1] = final_size;
            break;
            
        case 2:
        case 3:
            pt_idx_list_add(arena, final_hdr, final_size);
            break;
            
        case 4:
        default:
            // Size 8 and all larger tree tiers flow uniformly into our address-ordered index.
            // Historical vector markers (node->hdr[0]) are preserved automatically in-place!
            pt_idx_tree_insert(arena, arena->root, final_hdr, final_size);
            break;
    }
    final_hdr[0] = final_size;

    return final_hdr;
}

void pt_idx_list_split_state_machine(pt_arena_t* arena, word_t* left_hdr, word_t* final_hdr, word_t* right_hdr, word_t* bdry_hdr) 
{
	word_t current_size = bdry_hdr - left_hdr;

	word_t left_tag = final_hdr - left_hdr;
	word_t right_tag = bdry_hdr - right_hdr;

    // 1. Branchless status token collection
    unsigned left_state  = (2 <= left_tag) + (4 <= left_tag);
    unsigned right_state = (2 <= right_tag) + (4 <= right_tag);

    // 2. Pack neighbors into a 4-bit key (16 possible execution values)
    unsigned neighbor_state = left_state * 3 + right_state;

    /* ============================================================================
     * SWITCH MATRIX 1: UNLINKING AND STRUCTURAL SHORTCUTS
     * ============================================================================ */
	pt_idx_list_unlink(left_hdr, current_size);

	switch (neighbor_state) {
		case 0 * 3 + 0: // Exact Fit
			break;

		case 0 * 3 + 1: // Left Fit, Right Remnant
			bdry_hdr[-1] = 2;
			right_hdr[0] = 2; 
			break;

		case 0 * 3 + 2: // Left Fit, Right List
            pt_idx_list_insert(arena, right_hdr, right_tag);
			break;

		case 1 * 3 + 0: // Left Remnant, Right Fit
			left_hdr[0] = 2;
			final_hdr[-1] = 2;
			break;

		case 1 * 3 + 1: // Left Remanat, Right Remnant
			final_hdr[-1] = 2;
			left_hdr[0] = 2;
			bdry_hdr[-1] = 2;
			right_hdr[0] = 2; 
			break;

		case 2 * 3 + 0: // Left List, Right Fit
            pt_idx_list_insert(arena, left_hdr, left_tag);
			break;

		case 1 * 3 + 2: // Left Remnant, Right List
		case 2 * 3 + 1: // Left List, Right Remnant
		case 2 * 3 + 2: // Left List, Right List
		default: // Impossible
			__builtin_trap(); 
			break;
	}	

	word_t final_size = right_hdr - final_hdr;
	right_hdr[-1] = -final_size;
	final_hdr[0] = -final_size;
}

void pt_idx_tree_split_state_machine(pt_arena_t* arena, word_t* left_hdr, word_t* final_hdr, word_t* right_hdr, word_t* bdry_hdr) 
{
	word_t current_size = bdry_hdr - left_hdr;

	word_t left_tag = final_hdr - left_hdr;
	word_t right_tag = bdry_hdr - right_hdr;

    // 1. Branchless status token collection
    unsigned left_state  = (2 <= left_tag) + (4 <= left_tag) + (8 <= left_tag);
    unsigned right_state = (2 <= right_tag) + (4 <= right_tag) + (8 <= right_tag);

    // 2. Pack neighbors into a 4-bit key (16 possible execution values)
    unsigned neighbor_state = left_state * 4 + right_state;

	pt_redblack_t* x;
    /* ============================================================================
     * SWITCH MATRIX 1: UNLINKING AND STRUCTURAL SHORTCUTS
     * ============================================================================ */
	switch (neighbor_state) {
		case 0 * 4 + 0: // Exact Fit
            pt_idx_tree_unlink(arena, pt_idx_hdr_to_tree(left_hdr, current_size));
			break;

		case 0 * 4 + 1: // Left Fit, Right Remnant
            pt_idx_tree_unlink(arena, pt_idx_hdr_to_tree(left_hdr, current_size));
			bdry_hdr[-1] = 2;
			right_hdr[0] = 2;
			break;

		case 0 * 4 + 2: // Left Fit, Right List;
            pt_idx_tree_unlink(arena, pt_idx_hdr_to_tree(left_hdr, current_size));
            pt_idx_list_insert(arena, right_hdr, right_tag);
			break;

		case 0 * 4 + 3: // Left Fit, Right Tree
            pt_idx_tree_absorb_stationary_right(x = pt_idx_hdr_to_tree(left_hdr, current_size), right_tag);
			right_hdr[0] = right_tag;
			break;

		case 1 * 4 + 0: // Left Remnant, Right Fit
            pt_idx_tree_unlink(arena, pt_idx_hdr_to_tree(left_hdr, current_size));
			final_hdr[-1] = 2;
			left_hdr[0] = 2;
			break;

		case 1 * 4 + 1: // Left Remnant, Right Remnant
            pt_idx_tree_unlink(arena, pt_idx_hdr_to_tree(left_hdr, current_size));
			final_hdr[-1] = 2;
			left_hdr[0] = 2;
			bdry_hdr[-1] = 2;
			right_hdr[0] = 2;
			break;

		case 1 * 4 + 2: // Left Remnant, Right List
            pt_idx_tree_unlink(arena, pt_idx_hdr_to_tree(left_hdr, current_size));
			final_hdr[-1] = 2;
			left_hdr[0] = 2;
            pt_idx_list_insert(arena, right_hdr, right_tag);
			break;

		case 1 * 4 + 3: // Left Remnant, Right Tree
            pt_idx_tree_absorb_stationary_right(x = pt_idx_hdr_to_tree(left_hdr, current_size), right_tag);
			right_hdr[0] = right_tag;
			final_hdr[-1] = 2;
			left_hdr[0] = 2;
			break;

		case 2 * 4 + 0: // Left List, Right Fit
            pt_idx_tree_unlink(arena, pt_idx_hdr_to_tree(left_hdr, current_size));
            pt_idx_list_insert(arena, left_hdr, left_tag);
			break;

		case 2 * 4 + 1: // Left List, Right Remnant
            pt_idx_tree_unlink(arena, pt_idx_hdr_to_tree(left_hdr, current_size));
            pt_idx_list_insert(arena, left_hdr, left_tag);
			bdry_hdr[-1] = 2;
			right_hdr[0] = 2;
			break;

		case 2 * 4 + 2: // Left List, Right List
            pt_idx_tree_unlink(arena, pt_idx_hdr_to_tree(left_hdr, current_size));
            pt_idx_list_insert(arena, left_hdr, left_tag);
            pt_idx_list_insert(arena, right_hdr, right_tag);
			break;

		case 2 * 4 + 3: // Left List, Right Tree
            pt_idx_tree_absorb_stationary_right(x = pt_idx_hdr_to_tree(left_hdr, current_size), right_tag);
			right_hdr[0] = right_tag;
            pt_idx_list_insert(arena, left_hdr, left_tag);
			break;

		case 3 * 4 + 0: // Left Tree, Right Fit
            pt_idx_tree_migrate_leftward(arena, pt_idx_hdr_to_tree(left_hdr, current_size), left_hdr, left_tag);
			break;

		case 3 * 4 + 1: // Left Tree, Right Remnant
            pt_idx_tree_migrate_leftward(arena, pt_idx_hdr_to_tree(left_hdr, current_size), left_hdr, left_tag);
			bdry_hdr[-1] = 2;
			right_hdr[0] = 2;
			break;

		case 3 * 4 + 2: // Left Tree, Right List
            pt_idx_tree_migrate_leftward(arena, pt_idx_hdr_to_tree(left_hdr, current_size), left_hdr, left_tag);
            pt_idx_list_insert(arena, right_hdr, right_tag);
			break;

		case 3 * 4 + 3: // Left Tree, Right Tree
            pt_idx_tree_absorb_stationary_right(x = pt_idx_hdr_to_tree(left_hdr, current_size), right_tag);
			right_hdr[0] = right_tag;
            pt_idx_tree_insert(arena, x, left_hdr, left_tag);
			left_hdr[0] = left_tag;
			break;
		default:
			__builtin_trap(); 
			break;
	}

	word_t final_size = right_hdr - final_hdr;
	right_hdr[-1] = -final_size;
	final_hdr[0] = -final_size;
}

void pt_idx_tree_insert(pt_arena_t* arena, pt_redblack_t* x, word_t* final_hdr, word_t final_size) 
{
    pt_redblack_t* y = NULL;
    
    // Derive our 8-word tree node layout sitting at the trailing edge of the free space
    pt_redblack_t* z = pt_idx_hdr_to_tree(final_hdr, final_size);
    
	z->hdr[0] = 8; // watermark
    z->left  = NULL;
    z->right = NULL;
    z->color = PT_RB_RED; // New entries are always marked red
    z->left_max  = 0;
    z->right_max = 0;
	z->ftr[0] = final_size;

    // Address-Ordered Tree Descent
    while (x != NULL) {
        y = x;
        if ((uintptr_t)z < (uintptr_t)x) x = x->left;
        else                            x = x->right;
    }

    z->parent = y;
    if (y == NULL) {
        arena->root = z;
    } else if ((uintptr_t)z < (uintptr_t)y) {
        y->left = z;
    } else {
        y->right = z;
    }

	// >>> CRITICAL FIX: Propagate augmentations up the tree BEFORE 
    // the 'z' pointer is mutated by the structural balancing loop <<<
    pt_node_propagate_aug(z);

    // Fix any double-red violations up the insertion trail
    while (z != arena->root && z->parent->color == PT_RB_RED) {
        if (z->parent == z->parent->parent->left) {
            pt_redblack_t* uncle = z->parent->parent->right;
            if (uncle && uncle->color == PT_RB_RED) {
                z->parent->color = PT_RB_BLACK;
                uncle->color = PT_RB_BLACK;
                z->parent->parent->color = PT_RB_RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->right) {
                    z = z->parent;
                    pt_tree_rotate_left(arena, z);
                }
                z->parent->color = PT_RB_BLACK;
                z->parent->parent->color = PT_RB_RED;
                pt_tree_rotate_right(arena, z->parent->parent);
            }
        } else {
            pt_redblack_t* uncle = z->parent->parent->left;
            if (uncle && uncle->color == PT_RB_RED) {
                z->parent->color = PT_RB_BLACK;
                uncle->color = PT_RB_BLACK;
                z->parent->parent->color = PT_RB_RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->left) {
                    z = z->parent;
                    pt_tree_rotate_right(arena, z);
                }
                z->parent->color = PT_RB_BLACK;
                z->parent->parent->color = PT_RB_RED;
                pt_tree_rotate_left(arena, z->parent->parent);
            }
        }
    }
    
    if(PT_RB_RED == arena->root->color) arena->root->color = PT_RB_BLACK;
}

// Internal helper to exchange topological tree relationships without altering block data
static inline void pt_tree_node_pointer_swap(pt_arena_t* arena, pt_redblack_t* u, pt_redblack_t* v) 
{
    if (!u->parent)           arena->root = v;
    else if (u == u->parent->left) u->parent->left = v;
    else                      u->parent->right = v;
    
    if (v) v->parent = u->parent;
}

void pt_idx_tree_unlink(pt_arena_t* arena, pt_redblack_t* z) 
{
    pt_redblack_t* y = z;
    pt_redblack_t* x = NULL;
    pt_redblack_t* fixup_parent = NULL;
    unsigned y_original_color = y->color;

    if (z->left == NULL) {
        x = z->right;
        if((fixup_parent = z->parent)) {
			word_t* fixup_max = fixup_parent->left == z ? &fixup_parent->left_max : &fixup_parent->right_max;
			*fixup_max = z->right_max;
		}
        pt_tree_node_pointer_swap(arena, z, z->right); // remove z 
    } else if (z->right == NULL) {
        x = z->left;
        if((fixup_parent = z->parent)) {
			word_t* fixup_max = fixup_parent->left == z ? &fixup_parent->left_max : &fixup_parent->right_max;
			*fixup_max = z->left_max;
		}
        pt_tree_node_pointer_swap(arena, z, z->left); // remove z 
    } else {
        // Node has two children: Locate its absolute successor
        y = z->right;
        while (y->left != NULL) y = y->left;
        
        y_original_color = y->color;
        x = y->right;
        
        if (y->parent == z) {
            if (x) x->parent = y;
            fixup_parent = y;
        } else {
            if((fixup_parent = y->parent)) { 
				word_t* fixup_max = fixup_parent->left == y ? &fixup_parent->left_max : &fixup_parent->right_max;
				*fixup_max = y->right_max;
			}
            pt_tree_node_pointer_swap(arena, y, y->right); // remove y
            y->right = z->right;
			y->right_max = z->right_max;
            y->right->parent = y;
        }
        pt_tree_node_pointer_swap(arena, z, y); // remove z 
        y->left = z->left;
		y->left_max = z->left_max;
        y->left->parent = y;
        y->color = z->color;

		// Force an immediate backward propagation repair from the deletion pivot point
		if(y != fixup_parent) pt_node_propagate_aug(y);
    }

    // Force an immediate backward propagation repair from the deletion pivot point
    pt_node_propagate_aug(fixup_parent);

    // If a black node was unlinked, execute standard balancing fixups to maintain tree properties
    if (y_original_color == PT_RB_BLACK) {
        while (x != arena->root && (!x || x->color == PT_RB_BLACK)) {
            if (x == fixup_parent->left) {
                pt_redblack_t* w = fixup_parent->right;
                if (w && w->color == PT_RB_RED) {
                    w->color = PT_RB_BLACK;
                    fixup_parent->color = PT_RB_RED;
                    pt_tree_rotate_left(arena, fixup_parent);
                    w = fixup_parent->right;
                }
                if (w && (!w->left || w->left->color == PT_RB_BLACK) && (!w->right || w->right->color == PT_RB_BLACK)) {
                    w->color = PT_RB_RED;
                    x = fixup_parent;
                    fixup_parent = x->parent;
                } else if (w) {
                    if (!w->right || w->right->color == PT_RB_BLACK) {
                        if (w->left) w->left->color = PT_RB_BLACK;
                        w->color = PT_RB_RED;
                        pt_tree_rotate_right(arena, w);
                        w = fixup_parent->right;
                    }
                    w->color = fixup_parent->color;
                    fixup_parent->color = PT_RB_BLACK;
                    if (w->right) w->right->color = PT_RB_BLACK;
                    pt_tree_rotate_left(arena, fixup_parent);
                    x = arena->root;
                    break;
                } else { x = fixup_parent; fixup_parent = x->parent; }
            } else {
                pt_redblack_t* w = fixup_parent->left;
                if (w && w->color == PT_RB_RED) {
                    w->color = PT_RB_BLACK;
                    fixup_parent->color = PT_RB_RED;
                    pt_tree_rotate_right(arena, fixup_parent);
                    w = fixup_parent->left;
                }
                if (w && (!w->right || w->right->color == PT_RB_BLACK) && (!w->left || w->left->color == PT_RB_BLACK)) {
                    w->color = PT_RB_RED;
                    x = fixup_parent;
                    fixup_parent = x->parent;
                } else if (w) {
                    if (!w->left || w->left->color == PT_RB_BLACK) {
                        if (w->right) w->right->color = PT_RB_BLACK;
                        w->color = PT_RB_RED;
                        pt_tree_rotate_left(arena, w);
                        w = fixup_parent->left;
                    }
                    w->color = fixup_parent->color;
                    fixup_parent->color = PT_RB_BLACK;
                    if (w->left) w->left->color = PT_RB_BLACK;
                    pt_tree_rotate_right(arena, fixup_parent);
                    x = arena->root;
                    break;
                } else { x = fixup_parent; fixup_parent = x->parent; }
            }
        }
        if (x) x->color = PT_RB_BLACK;
    }
    
    if (arena->root && PT_RB_RED == arena->root->color) arena->root->color = PT_RB_BLACK;
}
