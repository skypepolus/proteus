#include "platform.h"
#include "arena.h"
#include "core.h"
#include "index.h"

word_t* pt_core_allocate_superpage_fallback(pt_arena_t* arena, word_t size_words) 
{
	pt_superpage_t* new_page[2] = { NULL, NULL };

	if (arena->empty_superpage_cache) {
		*new_page = arena->empty_superpage_cache;
		arena->empty_superpage_cache = NULL;
	} else {  
		/* --- Inside proteus_malloc's Slow-Path Page Allocation Gate --- */
		hybrid_unlock(&arena->lock); // Drop lock so others can use the arena
		pt_arena_superpage_new(new_page); // Expensive OS call
		hybrid_lock(&arena->lock, MALLOC_SPIN_COUNTER); // Reacquire lock
	
		if (__builtin_expect(*new_page == NULL, 0)) {
			return NULL;
		}
	}
	
	// This structural initialization must ONLY happen for a newly minted page
	(*new_page)->arena_ptr = arena;
	(*new_page)->ftr[0]    = 0; // Low Zero Sentinel
	(*new_page)->hdr[0]    = 0; // High Zero Sentinel
	
	word_t* huge_hdr  = &(*new_page)->block_words[0];
	word_t  huge_words = PT_HUGE_THRESHOLD_WORDS;
	
	pt_idx_tree_insert(arena, arena->root, huge_hdr, huge_words);
	huge_hdr[0] = huge_words;
	pt_redblack_t* node = pt_idx_hdr_to_tree(huge_hdr, huge_words);
	node->hdr[0] = huge_words; // initialize watermark

	if(new_page[1]) {
		if(NULL == arena->empty_superpage_cache) {
			arena->empty_superpage_cache = new_page[1];
		} else {
			hybrid_unlock(&arena->lock); // Drop lock so others can use the arena
			munmap(new_page[1], PT_SUPER_PAGE_BYTES); // Expensive OS call
			hybrid_lock(&arena->lock, MALLOC_SPIN_COUNTER); // Reacquire lock
		}
	}
	
	return pt_idx_tree_find_first_fit(arena->root, size_words); 
}
