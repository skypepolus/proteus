#include "platform.h"
#include "primitives.h"
#include "arena.h"
#include "index.h"

word_t* pt_core_allocate_superpage_fallback(pt_arena_t* arena, word_t size_words) 
{
	pt_superpage_t* superpage = g_pt.superpage;

	if(0 < superpage->hdr[-1]) {
		size_words -= superpage->hdr[-1];
	}
	if(-1 == brk(superpage->hdr + 1 + size_words)) {
		return NULL;
	}
	word_t* tail_hdr = superpage->hdr;
	tail_hdr[0] = -size_words;
	superpage->hdr += size_words;
	superpage->hdr[-1] = -size_words;
	superpage->hdr[0] = 0;
	return pt_idx_coalesce_state_machine(arena, tail_hdr, superpage->hdr);
}
