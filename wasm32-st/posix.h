/* wasm32-st/posix.h */
#ifndef __posix_h__
#define __posix_h__

#include "arena.h"
#include <stddef.h>

static inline void pt_arena_watermark(pt_arena_t* arena, word_t* final_hdr, word_t size_words, word_t coalesced_size) {
    // In bare-metal WASM, we don't return pages to the OS via madvise.
    // The memory just remains in the free tree.
    (void)arena; (void)final_hdr; (void)size_words; (void)coalesced_size;
}

#endif
