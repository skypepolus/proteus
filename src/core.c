#include "arena.h"
#include "core.h"
#include "index.h"
#include "proteus.h"
#include <sys/mman.h>
#include <string.h> // For high-performance architecture-optimized memcpy/memset
#include <errno.h>

static inline void* pt_core_try_segregated_alloc(pt_arena_t* arena, word_t size_words) {
    /* --------------------------------------------------------------------
     * LANE A: TARGET IS 4 WORDS (32 BYTES)
     * -------------------------------------------------------------------- */
    if (size_words == 4) {
        pt_link_t* list_4 = &arena->segregate[0].sentinel;
        
        // Match 1: True Exact Fit (4-word block available)
        if (list_4->next != list_4) {
            pt_link_t* node = list_4->next;
            word_t* hdr_ptr = pt_idx_link_to_hdr(node, 4);
            pt_idx_list_unlink(arena, hdr_ptr, 4);
            
            hdr_ptr[0] = -4;
            hdr_ptr[3] = -4;
            return (void*)(hdr_ptr + 1);
        }
        
        pt_link_t* list_6 = &arena->segregate[1].sentinel;
        
        // Match 2: Segregated Split Fallback (Carve from a 6-word block)
        if (list_6->next != list_6) {
            pt_link_t* node = list_6->next;
            word_t* hdr_ptr = pt_idx_link_to_hdr(node, 6);
            pt_idx_list_unlink(arena, hdr_ptr, 6);
            
            // Format the 4-word allocated chunk at the front
            hdr_ptr[0] = -4;
            hdr_ptr[3] = -4;
            
            // Format the remaining 2 words as a passive Pure Remnant
            word_t* remainder_hdr = hdr_ptr + 4;
            remainder_hdr[0] = 2;
            remainder_hdr[1] = 2;
            
            return (void*)(hdr_ptr + 1);
        }
    }

    /* --------------------------------------------------------------------
     * LANE B: TARGET IS 6 WORDS (48 BYTES)
     * -------------------------------------------------------------------- */
    if (size_words == 6) {
        pt_link_t* list_6 = &arena->segregate[1].sentinel;
        
        // Exact Fit Match Only
        if (list_6->next != list_6) {
            pt_link_t* node = list_6->next;
            word_t* hdr_ptr = pt_idx_link_to_hdr(node, 6);
            pt_idx_list_unlink(arena, hdr_ptr, 6);
            
            hdr_ptr[0] = -6;
            hdr_ptr[5] = -6;
            return (void*)(hdr_ptr + 1);
        }
    }

    return NULL; // List caches are fully depleted for these tiers
}

void* proteus_malloc(size_t size_bytes) {
    if (__builtin_expect(size_bytes == 0, 0)) {
        return NULL;
    }

    word_t size_words = PT_BYTES_TO_WORDS(size_bytes);
    
    /* ============================================================================
     * LANE 1: DIRECT-MMAP BYPASS (HUGE TIER)
     * ============================================================================ */
    if (size_words > PT_HUGE_THRESHOLD_WORDS) {
        size_t metadata_headroom = 3 * sizeof(word_t);
        size_t total_mmap_bytes  = size_bytes + metadata_headroom;

        word_t* mmap_base = (word_t*)mmap(
            NULL, total_mmap_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0
        );
        if (__builtin_expect(mmap_base == MAP_FAILED, 0)) return NULL;

        word_t* hdr_ptr = mmap_base + 2;
        hdr_ptr[0]  = -size_words; 
        hdr_ptr[-1] = (word_t)total_mmap_bytes;
        hdr_ptr[-2] = (word_t)mmap_base;

        return (void*)(hdr_ptr + 1);
    }

    /* ============================================================================
     * LANE 2: ARENA-MANAGED TIER (WITH FAST SEGREGATED LIST MATCHING)
     * ============================================================================ */
    pt_arena_t* home_arena = pt_arena_get_local();
    void* payload = NULL;
    
    // ----------------------------------------------------------------------------
    // PASS A: Home Arena Fast-Path
    // ----------------------------------------------------------------------------
    if (hybrid_try(&home_arena->lock)) {
        // 1. Check exact-fit segregated lists first
        payload = pt_core_try_segregated_alloc(home_arena, size_words);
        if (payload != NULL) {
            hybrid_unlock(&home_arena->lock);
            return payload;
        }
        // 2. Fallback to tree search
        pt_redblack_t* found_node = pt_idx_tree_find_first_fit(home_arena->root, size_words);
        if (found_node != NULL) {
            payload = pt_idx_extract_and_split(home_arena, found_node, size_words);
            hybrid_unlock(&home_arena->lock);
            return payload;
        }
        hybrid_unlock(&home_arena->lock);
    }
    
    // ----------------------------------------------------------------------------
    // PASS B: Work-Stealing Neighbor Core Scan
    // ----------------------------------------------------------------------------
    for (long i = 0; i < g_pt_num_cores; i++) {
        pt_arena_t* target_arena = &g_pt_arenas[i];
        if (target_arena == home_arena) continue;
        
        if (hybrid_try(&target_arena->lock)) {
            // 1. Attempt to steal from neighbor's segregated lists
            payload = pt_core_try_segregated_alloc(target_arena, size_words);
            if (payload != NULL) {
                hybrid_unlock(&target_arena->lock);
                return payload;
            }
            // 2. Attempt to steal and carve from neighbor's tree
            pt_redblack_t* found_node = pt_idx_tree_find_first_fit(target_arena->root, size_words);
            if (found_node != NULL) {
                payload = pt_idx_extract_and_split(target_arena, found_node, size_words);
                hybrid_unlock(&target_arena->lock);
                return payload;
            }
            hybrid_unlock(&target_arena->lock);
        }
    }
    
    // ----------------------------------------------------------------------------
    // PASS C: Slow-Path Fallback (Spin-to-sleep on home anchor)
    // ----------------------------------------------------------------------------
    hybrid_lock(&home_arena->lock, MALLOC_SPIN_COUNTER);
    
    while (1) {
        // 1. Try home segregated list under heavy lock
        payload = pt_core_try_segregated_alloc(home_arena, size_words);
        if (payload != NULL) {
            hybrid_unlock(&home_arena->lock);
            return payload;
        }
        // 2. Try home tree under heavy lock
        pt_redblack_t* found_node = pt_idx_tree_find_first_fit(home_arena->root, size_words);
        if (found_node != NULL) {
            payload = pt_idx_extract_and_split(home_arena, found_node, size_words);
            hybrid_unlock(&home_arena->lock);
            return payload;
        }

		/* --- Inside proteus_malloc's Slow-Path Page Allocation Gate --- */
        atomic_fetch_add_explicit(&home_arena->lock.wait, 1, memory_order_relaxed);
        pt_superpage_t* new_page = pt_arena_superpage_new();
        atomic_fetch_sub_explicit(&home_arena->lock.wait, 1, memory_order_relaxed);
        
        if (__builtin_expect(new_page == NULL, 0)) {
            hybrid_unlock(&home_arena->lock);
            return NULL;
        }
        
        new_page->arena_ptr = home_arena;
        new_page->ftr[0]    = 0; // Low Zero Sentinel
        new_page->hdr[0]    = 0; // High Zero Sentinel
        
        word_t* huge_hdr  = &new_page->block_words[0];
        word_t  huge_size = PT_HUGE_THRESHOLD_WORDS;
        
		huge_hdr[0] = PT_HUGE_THRESHOLD_WORDS;
		pt_idx_tree_insert(home_arena, huge_hdr, huge_size);
    }
}

void proteus_free(void* ptr) {
    if (__builtin_expect(ptr == NULL, 0)) return;

    word_t* hdr_ptr = (word_t*)ptr - 1;
    if (__builtin_expect(0 <= *hdr_ptr, 0)) __builtin_trap(); // Hardware corruption trap
    
    word_t size_words = -*hdr_ptr;

    /* ============================================================================
     * LANE 1: DIRECT-MMAP BYPASS (COMPLETELY LOCK-FREE & ARENA-FREE)
     * ============================================================================ */
    if (size_words > PT_HUGE_THRESHOLD_WORDS) {
        size_t total_mmap_bytes = (size_t)hdr_ptr[-1];
        void* mmap_base         = (void*)hdr_ptr[-2];
        munmap(mmap_base, total_mmap_bytes);
        return;
    }

    /* ============================================================================
     * LANE 2: ARENA-MANAGED COALESCING MATRIX (CROSS-THREAD SAFE)
     * ============================================================================ */
    pt_superpage_t* superpage = pt_arena_superpage(ptr);
    pt_arena_t* arena         = superpage->arena_ptr;

    hybrid_lock(&arena->lock, FREE_SPIN_COUNTER);

    word_t* ftr_ptr = hdr_ptr + size_words - 1;
    word_t coalesced_size;
    word_t* final_hdr = pt_idx_coalesce_state_machine(arena, hdr_ptr, ftr_ptr, &coalesced_size);

    // Super-Page Absolute Eviction Guard
    if (__builtin_expect(coalesced_size == PT_HUGE_THRESHOLD_WORDS, 0)) {
        void* superpage_base = (void*)superpage;
        
        atomic_fetch_add_explicit(&arena->lock.wait, 1, memory_order_relaxed);
        munmap(superpage_base, PT_SUPER_PAGE_BYTES);
        atomic_fetch_sub_explicit(&arena->lock.wait, 1, memory_order_relaxed);
        
        hybrid_unlock(&arena->lock);
        return;
    }

	/* ============================================================================
     * THE UNIFIED DIFFERENTIAL WATERMARK ADVISORY FILTER
     * ============================================================================ */
    if (coalesced_size >= 16384) { 
        pt_redblack_t* node = pt_idx_hdr_to_tree(final_hdr, coalesced_size);
        word_t* final_ftr   = final_hdr + coalesced_size - 1;
        
        word_t advised_size = node->hdr[0]; 
        
        if (coalesced_size < advised_size) {
            // Malloc splitting consumed our old watermark in the interim; lazily heal the state
            node->hdr[0] = coalesced_size;
            
        } else if (coalesced_size - advised_size >= 16384) {
            // Unpurged memory on the left has breached our 128KB threshold. Purge it.
            uintptr_t payload_start = (uintptr_t)(final_hdr + 1);
            uintptr_t payload_end   = (uintptr_t)node->hdr;
            
            uintptr_t page_start = (payload_start + 4095) & ~4095;
            uintptr_t page_end   = payload_end & ~4095;
            
            if (page_start < page_end) {
                atomic_fetch_add_explicit(&arena->lock.wait, 1, memory_order_relaxed);
                madvise((void*)page_start, page_end - page_start, MADV_FREE);
                atomic_fetch_sub_explicit(&arena->lock.wait, 1, memory_order_relaxed);
                
                // Set the new vector: Exact words from the new page_start to the absolute end
                node->hdr[0] = (final_ftr + 1) - (word_t*)page_start;
            }
        }
    }

    hybrid_unlock(&arena->lock);
}

int proteus_posix_memalign(void** memptr, size_t alignment, size_t size_bytes) {
    // 1. POSIX Constraint Validations
    if (__builtin_expect(memptr == NULL, 0)) {
        return EINVAL; 
    }
    // Alignment must be a valid power of two and a multiple of system pointer size
    if (__builtin_expect((alignment & (alignment - 1)) != 0 || alignment % sizeof(void*) != 0, 0)) {
        return EINVAL;
    }
    if (size_bytes == 0) {
        *memptr = NULL;
        return 0;
    }

    // Compute uniform size metrics: payload words + 2 structural words (Header + Ghost Footer)
    word_t payload_words = (size_bytes + sizeof(word_t) - 1) / sizeof(word_t);
    word_t size_words    = payload_words + 2;

    /* ============================================================================
     * LANE 1: STANDARD MALLOC ROUTING (ALIGNMENT <= 16 BYTES)
     * ============================================================================ */
    // Because Proteus natively aligns all blocks to 16-byte boundaries (2 words),
    // standard small alignments are already guaranteed to be met.
    if (alignment <= 16 && size_words <= PT_HUGE_THRESHOLD_WORDS) {
        void* ptr = proteus_malloc(size_bytes);
        if (__builtin_expect(ptr == NULL, 0)) {
            return ENOMEM;
        }
        *memptr = ptr;
        return 0;
    }

    /* ============================================================================
     * LANE 2: DIRECT-MMAP ALIGNMENT PROMOTION (ALIGNMENT > 16 BYTES)
     * ============================================================================ */
    // Oversize the virtual memory canvas to guarantee room for the alignment shift
    // plus our 3-word system metadata block layout.
    size_t metadata_headroom = 3 * sizeof(word_t);
    size_t total_mmap_bytes  = size_bytes + alignment + metadata_headroom;

    void* mmap_base = mmap(NULL, total_mmap_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (__builtin_expect(mmap_base == MAP_FAILED, 0)) {
        return ENOMEM;
    }

    // Compute the first perfectly aligned address that leaves enough headroom for your metadata
    uintptr_t raw_start       = (uintptr_t)mmap_base + metadata_headroom;
    uintptr_t aligned_payload = (raw_start + (alignment - 1)) & ~(alignment - 1);

    // Anchor the 3-word tracking structure immediately behind the aligned payload boundary
    word_t* hdr_ptr = (word_t*)aligned_payload - 1;
    
    // Stamp your layout invariants perfectly
    hdr_ptr[0]  = -size_words;          // Universal uniform size (tells free() it's huge)
    hdr_ptr[-1] = (word_t)total_mmap_bytes; // Raw byte count for munmap
    hdr_ptr[-2] = (word_t)mmap_base;        // Original OS pointer for munmap

    *memptr = (void*)aligned_payload;
    return 0;
}

void* proteus_realloc(void* ptr, size_t size_bytes) {
    // 1. Edge Case Gateways
    if (ptr == NULL) {
        return proteus_malloc(size_bytes);
    }
    if (size_bytes == 0) {
        proteus_free(ptr);
        return NULL;
    }

    word_t* hdr_ptr = (word_t*)ptr - 1;
    if (__builtin_expect(0 <= *hdr_ptr, 0)) __builtin_trap(); // Guard against corruption
    
    word_t current_size = -*hdr_ptr;
    word_t target_size  = PT_BYTES_TO_WORDS(size_bytes);

    bool current_is_huge = (current_size > PT_HUGE_THRESHOLD_WORDS);
    bool target_is_huge  = (target_size > PT_HUGE_THRESHOLD_WORDS);

    /* ============================================================================
     * MATRIX LANE 1: DIRECT-MMAP CONVERSIONS (HUGE TIER)
     * ============================================================================ */
    if (current_is_huge || target_is_huge) {
        // Optimization: Lazy Truncation if shrinking within the huge tier
        if (current_is_huge && target_is_huge && target_size <= current_size) {
            hdr_ptr[0] = -target_size; // Update logical size tracking
            return ptr;
        }

        // Allocate a fresh destination block (could land in arena or a new mmap)
        void* new_payload = proteus_malloc(size_bytes);
        if (__builtin_expect(new_payload == NULL, 0)) {
            return NULL; // Protect original block if allocation fails
        }

        // Polymorphic Copy Size Evaluation:
        // Huge block payload length is exactly 'current_size' words.
        // Managed block payload length is 'current_size - 2' words.
        word_t active_payload_words = current_is_huge ? current_size : (current_size - 2);
        word_t words_to_copy = (active_payload_words < target_size) ? active_payload_words : target_size;

        memcpy(new_payload, ptr, PT_WORDS_TO_BYTES(words_to_copy));

        // Release the old source block
        if (current_is_huge) {
            // Straightforward unmapping using your layout offsets
            size_t total_mmap_bytes = (size_t)hdr_ptr[-1];
            void* mmap_base         = (void*)hdr_ptr[-2];
            munmap(mmap_base, total_mmap_bytes);
        } else {
            // Standard arena release for managed blocks
            proteus_free(ptr);
        }

        return new_payload;
    }

    /* ============================================================================
     * MATRIX LANE 2: ARENA-MANAGED CONVERSIONS (SMALL/TREE TIER)
     * ============================================================================ */
    
    // Lazy Truncation if shrinking within managed boundaries
    if (target_size <= current_size) {
        return ptr; 
    }

    pt_arena_t* arena = pt_arena_get_local();
    hybrid_lock(&arena->lock, MALLOC_SPIN_COUNTER);

    word_t* right_hdr = hdr_ptr + current_size;
    word_t  right_tag = *right_hdr;

    // High-Speed In-Place Rightward Expansion Shortcut
    if (right_tag > 0 && (current_size + right_tag) >= target_size) {
        word_t total_combined_size = current_size + right_tag;
        
        // Unlink the right neighbor from its tracking tier
        if (right_tag == 4 || right_tag == 6) {
            pt_idx_list_unlink(arena, right_hdr, right_tag);
        } else if (right_tag >= 8) {
            pt_idx_tree_unlink(arena, pt_idx_hdr_to_tree(right_hdr, right_tag));
        }

        word_t delta = total_combined_size - target_size;
        unsigned state = (delta >= 2) + (delta >= 4) + (delta >= 6) + (delta >= 8);
        
        if (state == 4) {
            // Stationary Tree Split
            word_t* remainder_hdr = hdr_ptr + target_size;
            remainder_hdr[0] = delta;
            
            pt_redblack_t* right_node = pt_idx_hdr_to_tree(right_hdr, right_tag);
            right_node->ftr[0] = delta;
            
            pt_idx_tree_update_augmentation(right_node);
            
            hdr_ptr[0] = -target_size;
            hdr_ptr[target_size - 1] = -target_size;
        } else {
            // Absorb full neighbor chunk (Slack remains inside the block)
            hdr_ptr[0] = -total_combined_size;
            hdr_ptr[total_combined_size - 1] = -total_combined_size;
        }

        hybrid_unlock(&arena->lock);
        return ptr;
    }

    hybrid_unlock(&arena->lock);

    // Hard Managed Fallback: In-place options exhausted, migrate data
    void* new_payload = proteus_malloc(size_bytes);
    if (__builtin_expect(new_payload == NULL, 0)) {
        return NULL;
    }

    word_t managed_payload_words = current_size - 2;
    memcpy(new_payload, ptr, PT_WORDS_TO_BYTES(managed_payload_words));
    proteus_free(ptr);

    return new_payload;
}
#ifndef DEBUG
/* ============================================================================
 * STANDARD ALLOCATOR INTERPOSITION BRIDGE
 * ============================================================================ */

PT_EXPORT void* malloc(size_t size) {
    return proteus_malloc(size);
}

PT_EXPORT void free(void* ptr) {
    proteus_free(ptr);
}

PT_EXPORT void* realloc(void* ptr, size_t size) {
    return proteus_realloc(ptr, size);
}

PT_EXPORT int posix_memalign(void** memptr, size_t alignment, size_t size) {
    return proteus_posix_memalign(memptr, alignment, size);
}

PT_EXPORT void* calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void* ptr = proteus_malloc(total);
    if (ptr) {
        // Zero-fill allocated tracking space
        memset(ptr, 0, total);
    }
    return ptr;
}
#endif
