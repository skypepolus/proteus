#ifndef PT_INDEX_H
#define PT_INDEX_H

#include "arena.h"
#include "index.h"

/* ============================================================================
 * SEGREGATED LIST ROUTING & TRAILING-EDGE TRANSLATION
 * ============================================================================ */

// Maps a given word size to its corresponding segregated list index
static inline int pt_idx_list_select(word_t size_words) {
    // 4 words -> index 0, 6 words -> index 1
    return (size_words == 4) ? 0 : 1;
}

// Steps from a raw free block's header to its trailing-edge link structure
static inline pt_link_t* pt_idx_hdr_to_link(word_t* hdr_ptr, word_t size_words) {
    // Back up exactly 3 words from the end of the block boundary
    return (pt_link_t*)(hdr_ptr + size_words - 3);
}

// Steps backward from a trailing-edge link node to find its true header pointer
static inline word_t* pt_idx_link_to_hdr(pt_link_t* link_ptr, word_t size_words) {
    return ((word_t*)link_ptr) - size_words + 3;
}

/* ============================================================================
 * MUTATOR PROTOTYPES
 * ============================================================================ */

void pt_idx_list_insert(pt_arena_t* arena, word_t* hdr_ptr, word_t size_words);
void pt_idx_list_unlink(pt_arena_t* arena, word_t* hdr_ptr, word_t size_words);


// Defined color invariants for our balanced tree
#define PT_RB_RED   0
#define PT_RB_BLACK 1

/* ============================================================================
 * LOGICAL NAVIGATION PRIMITIVES
 * ============================================================================ */

// Read the absolute size of a tree node block directly from its trailing edge
static inline word_t pt_idx_tree_node_size(pt_redblack_t* node) {
    return node->ftr[0];
}

// Universal Header-to-Tree Mapping
static inline pt_redblack_t* pt_idx_hdr_to_tree(word_t* hdr_ptr, word_t size_words) {
    // Math: If size is 8: hdr_ptr + 8 - 8 = hdr_ptr (Perfect Overlap)
    //       If size > 8: Index points directly to the trailing 8-word descriptor block
    return (pt_redblack_t*)(hdr_ptr + size_words - 8);
}

// Universal Tree-to-Header Mapping
static inline word_t* pt_idx_tree_to_hdr(pt_redblack_t* node_ptr, word_t size_words) {
    return (word_t*)node_ptr - size_words + 8;
}

// Compute the maximum block size contained anywhere within a given subtree
static inline word_t pt_idx_tree_subtree_max(pt_redblack_t* node) {
    if (!node) return 0;
    word_t max_val = node->ftr[0]; // Start with the node's own size
    if (node->left_max > max_val)  max_val = node->left_max;
    if (node->right_max > max_val) max_val = node->right_max;
    return max_val;
}

/* ============================================================================
 * CORE PROTOTYPES
 * ============================================================================ */

void* pt_idx_allocate_from_arena(pt_arena_t* arena, word_t size_words);
pt_redblack_t* pt_idx_tree_find_first_fit(pt_redblack_t* root, word_t size_words);

#define PT_RB_RED   0
#define PT_RB_BLACK 1

// Helper to calculate the maximum block size available within a given subtree footprint
static inline word_t pt_node_total_max(pt_redblack_t* n) {
    if (!n) return 0;
    word_t self_size = n->ftr[0]; // Free blocks store positive size in the footer
    word_t max_child = (n->left_max > n->right_max) ? n->left_max : n->right_max;
    return (self_size > max_child) ? self_size : max_child;
}

// Recalculates the maximum size metrics for a node's immediate children
static inline void pt_node_update_aug(pt_redblack_t* n) {
    if (!n) return;
    n->left_max  = n->left  ? pt_node_total_max(n->left)  : 0;
    n->right_max = n->right ? pt_node_total_max(n->right) : 0;
}

// Bubbles augmentation corrections from a modified node all the way up to the root anchor
static inline void pt_node_propagate_aug(pt_arena_t* arena, pt_redblack_t* n) {
    while (n) {
        pt_node_update_aug(n);
        n = n->parent;
    }
}

static inline void pt_tree_rotate_left(pt_arena_t* arena, pt_redblack_t* x) {
    pt_redblack_t* y = x->right;
    x->right = y->left;
    if (y->left) y->left->parent = x;
    
    y->parent = x->parent;
    if (!x->parent)           arena->root = y;
    else if (x == x->parent->left) x->parent->left = y;
    else                      x->parent->right = y;
    
    y->left = x;
    x->parent = y;
    
    // Recalculate augmentations from the bottom up
    pt_node_update_aug(x);
    pt_node_update_aug(y);
}

static inline void pt_tree_rotate_right(pt_arena_t* arena, pt_redblack_t* y) {
    pt_redblack_t* x = y->left;
    y->left = x->right;
    if (x->right) x->right->parent = y;
    
    x->parent = y->parent;
    if (!y->parent)           arena->root = x;
    else if (y == y->parent->left) y->parent->left = x;
    else                      y->parent->right = x;
    
    x->right = y;
    y->parent = x;
    
    // Recalculate augmentations from the bottom up
    pt_node_update_aug(y);
    pt_node_update_aug(x);
}

#endif // PT_INDEX_H
