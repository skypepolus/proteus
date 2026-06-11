#include "platform.h"
#include "primitives.h"
#include "arena.h"
#include "index.h"

pt_redblack_t* pt_core_allocate_superpage_fallback(pt_arena_t* arena, word_t size_words) 
{
	size_t increment = size_words * sizeof(word_t);
	pt_superpage_t* superpage = g_pt.superpage;
	word_t* addr;
	word_t tail_words;
	word_t* tail_hdr;
	if(0 < superpage->hdr[-1]) {
		tail_words = superpage->hdr[-1];
		tail_hdr = superpage->hdr - tail_words;
		addr = (word_t*)(((uintptr_t)(tail_hdr + 1) + increment + PT_INDEX_WATERMARK_MASK) & ~(uintptr_t)PT_INDEX_WATERMARK_MASK);
		if(brk(addr)) __builtin_trap();
		superpage->hdr = --addr;
		superpage->hdr[0] = 0;
		switch((unsigned)tail_words << 1) {
		default:
		case 4: // Tree
			pt_redblack_t* node = pt_idx_hdr_to_tree(tail_hdr, tail_words);
			tail_words = superpage->hdr - tail_hdr;
			pt_idx_tree_migrate_rightward(arena, node, tail_hdr, tail_words);
			break;
		case 3: // List
		case 2: // List
			pt_idx_list_unlink(tail_hdr, tail_words);
		case 1: // Remnant
			tail_words = superpage->hdr - tail_hdr;
			tail_hdr[0] = tail_words;
			pt_idx_tree_insert(arena, arena->root, tail_hdr, tail_words);
			break;
		case 0:
			__builtin_trap();
		}
	} else {
		tail_hdr = superpage->hdr;
		addr = (word_t*)(((uintptr_t)(tail_hdr + 1) + increment + PT_INDEX_WATERMARK_MASK) & ~(uintptr_t)PT_INDEX_WATERMARK_MASK);
		if(brk(addr)) __builtin_trap();
		superpage->hdr = --addr;
		superpage->hdr[0] = 0;
		tail_words = superpage->hdr - tail_hdr;
		tail_hdr[0] = tail_words;
		pt_idx_tree_insert(arena, arena->root, tail_hdr, tail_words);
	}

	return pt_idx_tree_find_first_fit(arena->root, size_words); 
}
