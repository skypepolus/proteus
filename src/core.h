#ifndef PT_CORE_H
#define PT_CORE_H

// Tuned synchronization thresholds derived from our concurrency model
#define MALLOC_SPIN_COUNTER	500
#define FREE_SPIN_COUNTER	250

/* ============================================================================
 * CORE PROTOTYPES
 * ============================================================================ */
void* pt_idx_allocate_from_arena(pt_arena_t* arena, word_t size_words);
pt_redblack_t* pt_idx_tree_find_first_fit(pt_redblack_t* root, word_t size_words);

// ---> ADD THESE FIVE MISSING SIGNATURES <---
void* pt_idx_extract_and_split(pt_arena_t* arena, pt_redblack_t* node, word_t r_words);
void pt_idx_tree_insert(pt_arena_t* arena, word_t* hdr_ptr, word_t size_words);
word_t* pt_idx_coalesce_state_machine(pt_arena_t* arena, word_t* hdr, word_t* ftr, word_t* out_size_words);
void pt_idx_tree_unlink(pt_arena_t* arena, pt_redblack_t* z);

#endif // PT_CORE_H
