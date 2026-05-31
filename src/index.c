#include "index.h"

/* ============================================================================
 * SEGREGATED LIST MUTATORS
 * ============================================================================ */

void pt_idx_list_insert(pt_arena_t* arena, word_t* hdr_ptr, word_t size_words) {
    // 1. Identify target list queue
    int idx = pt_idx_list_select(size_words);
    pt_link_t* sentinel = &arena->segregate[idx].sentinel;
    
    // 2. Map the structural node onto the trailing edge of the free block
    pt_link_t* node = pt_idx_hdr_to_link(hdr_ptr, size_words);
    
    // 3. Complete the link transactions (Prepending to the circular queue)
    node->next = sentinel->next;
    node->prev = sentinel;
    
    sentinel->next->prev = node;
    sentinel->next = node;
    
    // 4. Stamp the Boundary Tags (Positive values declare the block is FREE)
    *hdr_ptr = size_words;
    node->ftr[0] = size_words; 
}

void pt_idx_list_unlink(pt_arena_t* arena, word_t* hdr_ptr, word_t size_words) {
    // Silence unused arena variable if compilers complain (retained for tree alignment later)
    (void)arena; 
    
    // 1. Map to the trailing edge link node using the known block size
    pt_link_t* node = pt_idx_hdr_to_link(hdr_ptr, size_words);
    
    // 2. Extract the node from the circular doubly linked list
    node->prev->next = node->next;
    node->next->prev = node->prev;
    
    // 3. Defensive Engineering: Clear out links to kill stray pointers in memory
    node->next = NULL;
    node->prev = NULL;
}

pt_redblack_t* pt_idx_tree_find_first_fit(pt_redblack_t* root, word_t size_words) {
    // Early Exit: If the root's total subtree maximum is smaller than what we need,
    // we know with 100% mathematical certainty that no block in this tree fits.
    if (!root || pt_idx_tree_subtree_max(root) < size_words) {
        return NULL;
    }

    pt_redblack_t* current = root;

    while (current != NULL) {
        // Step 1: Check lowest memory addresses first (Left Subtree).
        // If a fit exists down here, address-ordering guarantees it's our optimal first-fit.
        if (current->left && current->left_max >= size_words) {
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
        if (current->right && current->right_max >= size_words) {
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

void pt_idx_tree_update_augmentation(pt_redblack_t* node) {
    while (node != NULL) {
        node->left_max  = pt_idx_tree_subtree_max(node->left);
        node->right_max = pt_idx_tree_subtree_max(node->right);
        node = node->parent; // Ascend up to the root
    }
}

void* pt_idx_extract_and_split(pt_arena_t* arena, pt_redblack_t* node, word_t r_words) {
    word_t f_words = node->ftr[0];
    word_t* old_hdr = pt_idx_tree_to_hdr(node);
    
    word_t delta = f_words - r_words;
    unsigned state = (delta >= 2) + (delta >= 4) + (delta >= 6) + (delta >= 8);
    
    // Format the allocated payload block at the front boundary
    old_hdr[0] = -r_words;
    old_hdr[r_words - 1] = -r_words;
    void* user_payload = (void*)(old_hdr + 1);
    
    word_t* remainder_hdr = old_hdr + r_words;
    
    // Explicitly optimize for the stationary split fast-path
    if (__builtin_expect(state == 4, 1)) {
        remainder_hdr[0] = delta;
        node->ftr[0] = delta; // Update the size tag inside the trailing edge
        
        // Directly recompute the max_sub_size augmentations up to the root
        pt_idx_tree_update_augmentation(node);
        return user_payload;
    }
    
    // Cold Path: Leftover space is too small to sustain a tree node.
    // The node must be unlinked and degraded to a lower structural tier.
    pt_idx_tree_unlink(arena, node);
    
    switch(state) {
        case 0:
            // Exact fit. Nothing left over.
            break;
            
        case 1:
            // Remnant space (2 words). Completely passive.
            remainder_hdr[0] = 2;
            remainder_hdr[1] = 2;
            break;
            
        case 2:
            // Segregated list block (4 words)
            pt_idx_list_insert(arena, remainder_hdr, 4);
            break;
            
        case 3:
            // Segregated list block (6 words)
            pt_idx_list_insert(arena, remainder_hdr, 6);
            break;
    }
    
    return user_payload;
}

static inline void pt_idx_tree_rotate_left(pt_redblack_t** root, pt_redblack_t* x) {
    pt_redblack_t* y = x->right;
    
    // 1. Turn y's left subtree into x's right subtree
    x->right = y->left;
    if (y->left != NULL) {
        y->left->parent = x;
    }
    
    // 2. Link x's parent to y
    y->parent = x->parent;
    if (x->parent == NULL) {
        *root = y;
    } else if (x == x->parent->left) {
        x->parent->left = y;
    } else {
        x->parent->right = y;
    }
    
    // 3. Put x on y's left
    y->left = x;
    x->parent = y;
    
    // 4. Critical Augmentation Fix: x is now a child of y.
    // We must update the size metrics for x first, then y!
    x->left_max  = pt_idx_tree_subtree_max(x->left);
    x->right_max = pt_idx_tree_subtree_max(x->right);
    
    y->left_max  = pt_idx_tree_subtree_max(y->left);
    y->right_max = pt_idx_tree_subtree_max(y->right);
}

static inline void pt_idx_tree_rotate_right(pt_redblack_t** root, pt_redblack_t* y) {
    pt_redblack_t* x = y->left;
    
    // 1. Turn x's right subtree into y's left subtree
    y->left = x->right;
    if (x->right != NULL) {
        x->right->parent = y;
    }
    
    // 2. Link y's parent to x
    x->parent = y->parent;
    if (y->parent == NULL) {
        *root = x;
    } else if (y == y->parent->left) {
        y->parent->left = x;
    } else {
        y->parent->right = x;
    }
    
    // 3. Put y on x's right
    x->right = y;
    y->parent = x;
    
    // 4. Critical Augmentation Fix: y is now a child of x.
    // We must update the size metrics for y first, then x!
    y->left_max  = pt_idx_tree_subtree_max(y->left);
    y->right_max = pt_idx_tree_subtree_max(y->right);
    
    x->left_max  = pt_idx_tree_subtree_max(x->left);
    x->right_max = pt_idx_tree_subtree_max(x->right);
}

void pt_idx_tree_insert(pt_arena_t* arena, word_t* hdr_ptr, word_t size_words) {
    pt_redblack_t* node = pt_idx_hdr_to_tree(hdr_ptr, size_words);
    
    // Initialize the fresh tree node metadata
    node->left   = NULL;
    node->right  = NULL;
    node->parent = NULL;
    node->color  = PT_RB_RED; // Standard Red-Black insertion property
    node->left_max  = 0;
    node->right_max = 0;
    node->ftr[0]    = size_words; // Anchor size at trailing edge

    pt_redblack_t* y = NULL;
    pt_redblack_t* x = arena->root;

    // 1. Standard Binary Search Tree routing by absolute memory address
    while (x != NULL) {
        y = x;
        if ((uintptr_t)node < (uintptr_t)x) {
            x = x->left;
        } else {
            x = x->right;
        }
    }

    node->parent = y;
    if (y == NULL) {
        arena->root = node; // Tree was empty
    } else if ((uintptr_t)node < (uintptr_t)y) {
        y->left = node;
    } else {
        y->right = node;
    }

    // 2. Propagate initial size metrics up to the root before rebalancing
    pt_idx_tree_update_augmentation(node);

    // 3. Trigger Red-Black Tree Balancing Fixups
    pt_idx_tree_insert_fixup(&arena->root, node);
}

// Handles shifting a left tree node rightward within the newly unified block boundaries
static inline void pt_idx_tree_migrate_rightward(pt_arena_t* arena, pt_redblack_t* old_node, 
                                                 word_t* final_hdr, word_t final_size) {
    pt_redblack_t* new_node = pt_idx_hdr_to_tree(final_hdr, final_size);
    
    // 1. Shallow Copy structural descriptors instantly
    *new_node = *old_node;
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
    pt_idx_tree_update_augmentation(new_node);
}

// Handles updating a right tree node in-place as it absorbs memory from its left side
static inline void pt_idx_tree_absorb_stationary_right(pt_redblack_t* right_node, 
                                                       word_t* final_hdr, word_t final_size) {
    final_hdr[0] = final_size;
    right_node->ftr[0] = final_size;
    pt_idx_tree_update_augmentation(right_node);
}

word_t* pt_idx_coalesce_state_machine(pt_arena_t* arena, word_t* hdr, word_t* ftr, word_t* out_size_words) {
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
            pt_idx_list_unlink(arena, ftr + 1, right_tag);
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
            pt_idx_list_unlink(arena, ftr + 1, right_tag);
            final_hdr   = hdr - left_tag;
            final_size += left_tag + right_tag;
            break;

        case 0x8: // Left List, Right Busy
            final_hdr   = hdr - left_tag;
            pt_idx_list_unlink(arena, final_hdr, left_tag);
            final_size += left_tag;
            break;

        case 0x9: // Left List, Right Remnant
            final_hdr   = hdr - left_tag;
            pt_idx_list_unlink(arena, final_hdr, left_tag);
            final_size += left_tag + right_tag;
            break;

        case 0xA: // Left List, Right List
            final_hdr   = hdr - left_tag;
            pt_idx_list_unlink(arena, final_hdr, left_tag);
            pt_idx_list_unlink(arena, ftr + 1, right_tag);
            final_size += left_tag + right_tag;
            break;

        /* --- BLOCK B: Stationary Right-Tree Shortcuts (Immediate Returns) --- */
        case 0x3: // Left Busy, Right Tree
            pt_idx_tree_absorb_stationary_right(pt_idx_hdr_to_tree(ftr + 1, right_tag), final_hdr, current_size + right_tag);
            *out_size_words = current_size + right_tag;
            return final_hdr;

        case 0x7: // Left Remnant, Right Tree
            final_hdr = hdr - left_tag;
            pt_idx_tree_absorb_stationary_right(pt_idx_hdr_to_tree(ftr + 1, right_tag), final_hdr, current_size + left_tag + right_tag);
            *out_size_words = current_size + left_tag + right_tag;
            return final_hdr;

        case 0xB: // Left List, Right Tree
            final_hdr = hdr - left_tag;
            pt_idx_list_unlink(arena, final_hdr, left_tag);
            pt_idx_tree_absorb_stationary_right(pt_idx_hdr_to_tree(ftr + 1, right_tag), final_hdr, current_size + left_tag + right_tag);
            *out_size_words = current_size + left_tag + right_tag;
            return final_hdr;

        /* --- BLOCK C: Left-Tree Rightward Migrations (Immediate Returns) --- */
        case 0xC: // Left Tree, Right Busy
            final_hdr  = hdr - left_tag;
            final_size = current_size + left_tag;
            pt_idx_tree_migrate_rightward(arena, pt_idx_hdr_to_tree(final_hdr, left_tag), final_hdr, final_size);
            *out_size_words = final_size;
            return final_hdr;

        case 0xD: // Left Tree, Right Remnant
            final_hdr  = hdr - left_tag;
            final_size = current_size + left_tag + right_tag;
            pt_idx_tree_migrate_rightward(arena, pt_idx_hdr_to_tree(final_hdr, left_tag), final_hdr, final_size);
            *out_size_words = final_size;
            return final_hdr;

        case 0xE: // Left Tree, Right List
            pt_idx_list_unlink(arena, ftr + 1, right_tag);
            final_hdr  = hdr - left_tag;
            final_size = current_size + left_tag + right_tag;
            pt_idx_tree_migrate_rightward(arena, pt_idx_hdr_to_tree(final_hdr, left_tag), final_hdr, final_size);
            *out_size_words = final_size;
            return final_hdr;

        /* --- BLOCK D: Double Tree Collision --- */
        case 0xF: // Left Tree, Right Tree
            // Keep right stationary, completely unlink left node to prevent duplicates
            final_hdr = hdr - left_tag;
            pt_idx_tree_unlink(arena, pt_idx_hdr_to_tree(final_hdr, left_tag));
            final_size = current_size + left_tag + right_tag;
            pt_idx_tree_absorb_stationary_right(pt_idx_hdr_to_tree(ftr + 1, right_tag), final_hdr, final_size);
            *out_size_words = final_size;
            return final_hdr;
    }

    *out_size_words = final_size;
    final_hdr[0] = final_size;

	/* ============================================================================
     * SWITCH MATRIX 2: RE-INDEXING ROUTING TABLE
     * ============================================================================ */
    switch (final_size) {
        case 2:
            // CRITICAL CRASH: Absolute heap boundary destruction detected!
            __builtin_trap(); 
            break;
            
        case 4:
        case 6:
            pt_idx_list_insert(arena, final_hdr, final_size);
            break;
            
        case 8:
        default:
            // Size 8 and all larger tree tiers flow uniformly into our address-ordered index.
            // Historical vector markers (node->hdr[0]) are preserved automatically in-place!
            pt_idx_tree_insert(arena, final_hdr, final_size);
            break;
    }

    return final_hdr;
}

void pt_idx_tree_insert(pt_arena_t* arena, word_t* final_hdr, word_t final_size) {
    pt_redblack_t* x = arena->root;
    pt_redblack_t* y = NULL;
    
    // Derive our 8-word tree node layout sitting at the trailing edge of the free space
    pt_redblack_t* z = pt_idx_hdr_to_tree(final_hdr, final_size);
    
    z->left  = NULL;
    z->right = NULL;
    z->color = PT_RB_RED; // New entries are always marked red
    z->left_max  = 0;
    z->right_max = 0;

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
    
    arena->root->color = PT_RB_BLACK;
    
    // Bubble up and correct all structural size augmentations
    pt_node_propagate_aug(arena, z);
}

// Internal helper to exchange topological tree relationships without altering block data
static inline void pt_tree_node_pointer_swap(pt_arena_t* arena, pt_redblack_t* u, pt_redblack_t* v) {
    if (!u->parent)           arena->root = v;
    else if (u == u->parent->left) u->parent->left = v;
    else                      u->parent->right = v;
    
    if (v) v->parent = u->parent;
}

void pt_idx_tree_unlink(pt_arena_t* arena, pt_redblack_t* z) {
    pt_redblack_t* y = z;
    pt_redblack_t* x = NULL;
    pt_redblack_t* fixup_parent = NULL;
    unsigned y_original_color = y->color;

    if (z->left == NULL) {
        x = z->right;
        fixup_parent = z->parent;
        pt_tree_node_pointer_swap(arena, z, z->right);
    } else if (z->right == NULL) {
        x = z->left;
        fixup_parent = z->parent;
        pt_tree_node_pointer_swap(arena, z, z->left);
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
            fixup_parent = y->parent;
            pt_tree_node_pointer_swap(arena, y, y->right);
            y->right = z->right;
            y->right->parent = y;
        }
        
        pt_tree_node_pointer_swap(arena, z, y);
        y->left = z->left;
        y->left->parent = y;
        y->color = z->color;
    }

    // Force an immediate backward propagation repair from the deletion pivot point
    pt_node_propagate_aug(arena, fixup_parent);

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
    
    if (arena->root) arena->root->color = PT_RB_BLACK;
}

