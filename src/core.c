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
#include "arena.h"
#include "core.h"
#include "index.h"
#include "proteus.h"
#include <sys/mman.h>
#include <string.h> // For high-performance architecture-optimized memcpy/memset
#include <errno.h>
#include <immintrin.h>

static inline void* pt_core_try_segregated_alloc(pt_arena_t* arena, word_t size_words) {

	pt_link_t* list_4 = &arena->segregate[0].sentinel;
	pt_link_t* list_6 = &arena->segregate[1].sentinel;

	switch(size_words >> 1) {
	case 0:
		 __builtin_trap();
    /* --------------------------------------------------------------------
     * LANE Z: TARGET IS 2 WORDS (16 BYTES)
     * -------------------------------------------------------------------- */
	case 1:
        // Match 1: True Exact Fit (4-word block available)
        if (list_4->prev != list_4) {
            pt_link_t* node = list_4->next;
            word_t* hdr_ptr = pt_idx_link_to_hdr(node, 4);
            pt_idx_list_unlink(hdr_ptr, 4);
            
            hdr_ptr[0] = -2;
            hdr_ptr[1] = -2;
            hdr_ptr[2] = 2;
            hdr_ptr[3] = 2;
            return (void*)(hdr_ptr + 1);
        }
        
        // Match 2: Segregated Split Fallback (Carve from a 6-word block)
        if (list_6->prev != list_6) {
            pt_link_t* node = list_6->next;
            word_t* hdr_ptr = pt_idx_link_to_hdr(node, 6);
            pt_idx_list_unlink(hdr_ptr, 6);
            
            // Format the 4-word allocated chunk at the front
            hdr_ptr[0] = -2;
            hdr_ptr[1] = -2;
            
            // Format the remaining 2 words as a passive Pure Remnant
            word_t* remainder_hdr = hdr_ptr + 2;
			pt_idx_list_insert(arena, remainder_hdr, 4);
            
            return (void*)(hdr_ptr + 1);
        }
		break;
    /* --------------------------------------------------------------------
     * LANE A: TARGET IS 4 WORDS (32 BYTES)
     * -------------------------------------------------------------------- */
	case 2:
        // Match 1: True Exact Fit (4-word block available)
        if (list_4->prev != list_4) {
            pt_link_t* node = list_4->next;
            word_t* hdr_ptr = pt_idx_link_to_hdr(node, 4);
            pt_idx_list_unlink(hdr_ptr, 4);
            
            hdr_ptr[0] = -4;
            hdr_ptr[3] = -4;
            return (void*)(hdr_ptr + 1);
        }
        
        // Match 2: Segregated Split Fallback (Carve from a 6-word block)
        if (list_6->prev != list_6) {
            pt_link_t* node = list_6->next;
            word_t* hdr_ptr = pt_idx_link_to_hdr(node, 6);
            pt_idx_list_unlink(hdr_ptr, 6);
            
            // Format the 4-word allocated chunk at the front
            hdr_ptr[0] = -4;
            hdr_ptr[3] = -4;
            
            // Format the remaining 2 words as a passive Pure Remnant
            word_t* remainder_hdr = hdr_ptr + 4;
            remainder_hdr[0] = 2;
            remainder_hdr[1] = 2;
            
            return (void*)(hdr_ptr + 1);
        }
		break;
    /* --------------------------------------------------------------------
     * LANE B: TARGET IS 6 WORDS (48 BYTES)
     * -------------------------------------------------------------------- */
	case 3:
        // Exact Fit Match Only
        if (list_6->prev != list_6) {
            pt_link_t* node = list_6->next;
            word_t* hdr_ptr = pt_idx_link_to_hdr(node, 6);
            pt_idx_list_unlink(hdr_ptr, 6);
            
            hdr_ptr[0] = -6;
            hdr_ptr[5] = -6;
            return (void*)(hdr_ptr + 1);
        }
		break;
    }

    return NULL; // List caches are fully depleted for these tiers
}

static pt_redblack_t* pt_core_allocate_superpage_fallback(pt_arena_t* home_arena, word_t size_words);

void* proteus_malloc(size_t size_bytes) {

    word_t size_words = PT_TOTAL_BLOCK_WORDS(size_bytes);
    
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
		else
		if((found_node = pt_core_allocate_superpage_fallback(home_arena, size_words))) {
			payload = pt_idx_extract_and_split(home_arena, found_node, size_words);
			hybrid_unlock(&home_arena->lock);
			return payload;
		}
        hybrid_unlock(&home_arena->lock);
    }
    
    // ----------------------------------------------------------------------------
    // PASS B: Work-Stealing Neighbor Core Scan
    // ----------------------------------------------------------------------------
    for (long i = 1, j = home_arena - g_pt_arenas; i < g_pt_num_cores; i++) {
        pt_arena_t* target_arena = &g_pt_arenas[(i + j) % g_pt_num_cores];
        
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
			else
			if((found_node = pt_core_allocate_superpage_fallback(target_arena, size_words))) {
                payload = pt_idx_extract_and_split(target_arena, found_node, size_words);
                hybrid_unlock(&target_arena->lock);
                return payload;
			}
            hybrid_unlock(&target_arena->lock);
        }
    }

    // ----------------------------------------------------------------------------
    // PASS C: Home Arena Slow-Path
    // ----------------------------------------------------------------------------
	hybrid_lock(&home_arena->lock, MALLOC_SPIN_COUNTER); // Reacquire lock
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
	else
	if((found_node = pt_core_allocate_superpage_fallback(home_arena, size_words))) {
		payload = pt_idx_extract_and_split(home_arena, found_node, size_words);
		hybrid_unlock(&home_arena->lock);
		return payload;
	}
	hybrid_unlock(&home_arena->lock);
    
	return NULL;
}

__attribute__((cold, noinline)) 
static pt_redblack_t* pt_core_allocate_superpage_fallback(pt_arena_t* home_arena, word_t size_words) {
        // 3. Fallback to expensive page creation since the arena is completely dry
        pt_superpage_t* new_page = NULL;

        if (home_arena->empty_superpage_cache) {
            new_page = home_arena->empty_superpage_cache;
            home_arena->empty_superpage_cache = NULL;
        } else {  
            /* --- Inside proteus_malloc's Slow-Path Page Allocation Gate --- */
            hybrid_unlock(&home_arena->lock); // Drop lock so others can use the arena

            new_page = pt_arena_superpage_new(); // Expensive OS call

            hybrid_lock(&home_arena->lock, MALLOC_SPIN_COUNTER); // Reacquire lock
        
            if (__builtin_expect(new_page == NULL, 0)) {
                return NULL;
            }
        }
        
        // This structural initialization must ONLY happen for a newly minted page
        new_page->arena_ptr = home_arena;
        new_page->ftr[0]    = 0; // Low Zero Sentinel
        new_page->hdr[0]    = 0; // High Zero Sentinel
        
        word_t* huge_hdr  = &new_page->block_words[0];
        word_t  huge_size = PT_HUGE_THRESHOLD_WORDS;
        
        huge_hdr[0] = PT_HUGE_THRESHOLD_WORDS;
        pt_idx_tree_insert(home_arena, NULL, huge_hdr, huge_size);
        
		return pt_idx_tree_find_first_fit(home_arena->root, size_words); 
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

	/* ============================================================================
     * THE UNIFIED DIFFERENTIAL WATERMARK ADVISORY FILTER
     * ============================================================================ */
	if (__builtin_expect(coalesced_size == PT_HUGE_THRESHOLD_WORDS, 0)) {
        // Only release the superpage if it's not the absolute last one in the tree
        if (arena->root->left || arena->root->right) { 
            // 1. Unlink from the tree first so it becomes invisible to the allocator
            pt_idx_tree_unlink(arena, pt_idx_hdr_to_tree(final_hdr, coalesced_size));
            void* superpage_base = (void*)superpage;
            
            if (arena->empty_superpage_cache) { 
                // Cache is full. Drop lock and DESTROY the page. (No madvise needed)
                hybrid_unlock(&arena->lock); 
                munmap(superpage_base, PT_SUPER_PAGE_BYTES);
            } else {
                // Cache is empty. Drop lock FIRST to perform the heavy system call
                hybrid_unlock(&arena->lock); 

				pt_platform_purge_pages	(superpage_base, PT_SUPER_PAGE_BYTES);

                // Re-acquire lock to safely publish the purged page
                hybrid_lock(&arena->lock, FREE_SPIN_COUNTER);
                if (arena->empty_superpage_cache == NULL) {
                    arena->empty_superpage_cache = superpage_base;
                    hybrid_unlock(&arena->lock);
                } else {
                    // Another thread filled the cache while we were unlocked! Destroy ours.
                    hybrid_unlock(&arena->lock);
                    munmap(superpage_base, PT_SUPER_PAGE_BYTES);
                }
            }
            return;
        }
	} else if (coalesced_size >= PT_INDEX_WATERMARK_WORDS + 8) { 
        pt_redblack_t* node = pt_idx_hdr_to_tree(final_hdr, coalesced_size);
        
        word_t advised_size = node->hdr[0]; 
        
        if (coalesced_size < advised_size) {
            // Malloc splitting consumed our old watermark in the interim; lazily heal the state
            node->hdr[0] = coalesced_size;
            
        } else if (coalesced_size - advised_size >= PT_INDEX_WATERMARK_WORDS) {
            // Unpurged memory on the left has breached our 128KB threshold. Purge it.
            uintptr_t payload_start = (uintptr_t)(final_hdr + 1);
            uintptr_t payload_end   = (uintptr_t)node->hdr;
            
            uintptr_t page_start = (payload_start + arena->page_mask) & ~arena->page_mask;
            uintptr_t page_end   = payload_end & ~arena->page_mask;
            
            if (page_start < page_end) {
			#if 1
				word_t* final_ftr   = final_hdr + coalesced_size - 1;
				// 1. UNLINK: Remove from the tree so it can't be allocated
                pt_idx_tree_unlink(arena, node);

                // 2. INVERT TAGS: Disguise as an allocated block so neighbors don't coalesce and double-unlink
                final_hdr[0] = -coalesced_size;
                final_ftr[0] = -coalesced_size;

                // 3. DROP LOCK: Allow parallel allocations
                hybrid_unlock(&arena->lock);
			#else
				atomic_fetch_add_explicit(&arena->lock.wait, 1, memory_order_relaxed);
			#endif
                // 4. SYSCALL: Safe, unlocked page table purge
				pt_platform_purge_pages((void*)page_start, page_end - page_start);
			#if 0
				atomic_fetch_sub_explicit(&arena->lock.wait, 1, memory_order_relaxed);
			#else
                // 5. RE-ACQUIRE LOCK
                hybrid_lock(&arena->lock, FREE_SPIN_COUNTER);

                // 6. RE-COALESCE & RE-INSERT
                // Because we inverted the tags to negative, we satisfy the state machine's 
                // precondition. It will safely check if neighbors freed themselves while 
                // we were unlocked, format the final positive tags, and insert it.
                final_hdr = pt_idx_coalesce_state_machine(arena, final_hdr, final_ftr, &coalesced_size);

                // 7. Re-anchor the geometric vector tracking
                node = pt_idx_hdr_to_tree(final_hdr, coalesced_size);
                node->hdr[0] = (final_ftr + 1) - (word_t*)page_start;
			#endif
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

	// Compute uniform size metrics adhering strictly to the 16-byte structural alignment
    word_t size_words = PT_TOTAL_BLOCK_WORDS(size_bytes);

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
     * LANE 2: ARENA-BASED ALIGNMENT CARVING (ALIGNMENT > 16 BYTES)
     * ============================================================================ */
    // Request enough padding to guarantee we can shift up to the alignment boundary
    size_t request_bytes = size_bytes + alignment;
    word_t request_words = PT_TOTAL_BLOCK_WORDS(request_bytes);

    if (request_words <= PT_HUGE_THRESHOLD_WORDS) {
        // 1. Allocate a raw block large enough from the arena
        void* raw_ptr = proteus_malloc(request_bytes);
        if (__builtin_expect(raw_ptr == NULL, 0)) {
            return ENOMEM;
        }

        word_t* base_hdr = (word_t*)raw_ptr - 1;
        word_t  base_size = -base_hdr[0]; // Active blocks are negatively stamped

        // 2. Find the aligned payload boundary inside the raw payload
        uintptr_t raw_payload = (uintptr_t)raw_ptr;
        uintptr_t aligned_payload = (raw_payload + alignment - 1) & ~(alignment - 1);

        // 3. Calculate structural boundaries
        word_t left_shift_words = (aligned_payload - raw_payload) / sizeof(word_t);
        word_t right_size = base_size - left_shift_words - size_words;

        word_t* aligned_hdr = base_hdr + left_shift_words;

		// === ENCAPSULATE CRITICAL MUTATION SECTION ===
        pt_superpage_t* superpage = pt_arena_superpage(raw_ptr);
        pt_arena_t* arena = superpage->arena_ptr;

		// Secure exclusive access to boundary tags before any modifications occur
        hybrid_lock(&arena->lock, MALLOC_SPIN_COUNTER);

        // 4. Format the target aligned block
        aligned_hdr[0] = -size_words;
        aligned_hdr[size_words - 1] = -size_words;

        // 5. Setup the left remnant boundaries
        if (left_shift_words > 0) {
            base_hdr[0] = -left_shift_words;
            base_hdr[left_shift_words - 1] = -left_shift_words;
        }

        // 6. Setup the right remnant boundaries
        if (right_size > 0) {
            word_t* right_hdr = aligned_hdr + size_words;
            right_hdr[0] = -right_size;
            right_hdr[right_size - 1] = -right_size;
        }

		// Safely unlock before passing remnants to free(), preventing deadlocks completely
        hybrid_unlock(&arena->lock); 
        // === END OF CRITICAL SECTION ===

        // 7. Release the remnants safely back to the arena
        if (left_shift_words > 0) {
            proteus_free(base_hdr + 1);
        }
        if (right_size > 0) {
            word_t* right_hdr = aligned_hdr + size_words;
            proteus_free(right_hdr + 1);
        }

        *memptr = (void*)aligned_payload;
        return 0;
    }

    /* ============================================================================
     * LANE 3: DIRECT-MMAP ALIGNMENT PROMOTION (HUGE TIER)
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

	// >>> CRITICAL FIX 1: Ensure size_words breaches the huge threshold so free() routes it to munmap <<<
    word_t encoded_size = (size_words > PT_HUGE_THRESHOLD_WORDS) ? size_words : (PT_HUGE_THRESHOLD_WORDS + size_words);
    
    // Stamp your layout invariants perfectly
    hdr_ptr[0]  = -encoded_size;          // Universal uniform size (tells free() it's huge)
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
		// >>> CRITICAL FIX: Return a valid pointer instead of NULL
        return proteus_malloc(0);
    }

    word_t* hdr_ptr = (word_t*)ptr - 1;
    if (__builtin_expect(0 <= *hdr_ptr, 0)) __builtin_trap(); // Guard against corruption
    
    word_t current_size = -*hdr_ptr;
    word_t target_size  = PT_TOTAL_BLOCK_WORDS(size_bytes);

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

		// >>> CRITICAL FIX 2: Safely derive physical payload bounds from the original mmap parameters <<<
        word_t active_payload_words;
        if (current_is_huge) {
            size_t total_mmap_bytes = (size_t)hdr_ptr[-1];
            void* mmap_base         = (void*)hdr_ptr[-2];
            size_t actual_payload_bytes = total_mmap_bytes - ((uintptr_t)ptr - (uintptr_t)mmap_base);
            active_payload_words = actual_payload_bytes / sizeof(word_t);
        } else {
            active_payload_words = current_size - 2;
        }

		// >>> CRITICAL FIX 3: Ensure we do not overflow the TARGET block's payload capacity! <<<
        word_t target_payload_words = target_size - 2;
        word_t words_to_copy = (active_payload_words < target_payload_words) ? active_payload_words : target_payload_words;

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

	// >>> CRITICAL FIX 4: Route realloc to the block's true hardware home arena, NOT the local thread! <<<
    pt_superpage_t* superpage = pt_arena_superpage(ptr);
    pt_arena_t* arena = superpage->arena_ptr;

    hybrid_lock(&arena->lock, MALLOC_SPIN_COUNTER);

    word_t* right_hdr = hdr_ptr + current_size;
    word_t  right_tag = *right_hdr;


    // High-Speed In-Place Rightward Expansion Shortcut
    if (right_tag > 0 && (current_size + right_tag) >= target_size) {
		word_t total_combined_size = current_size + right_tag;
        word_t delta = total_combined_size - target_size;
        unsigned state = (delta >= 2) + (delta >= 4) + (delta >= 6) + (delta >= 8);

        if (state == 4 && right_tag >= 8) {
            // >>> CRITICAL FIX: True Stationary Tree Split. Old block MUST have been in the tree! <<<
            word_t* remainder_hdr = hdr_ptr + target_size;
            remainder_hdr[0] = delta;

            pt_redblack_t* right_node = pt_idx_hdr_to_tree(right_hdr, right_tag);
            right_node->ftr[0] = delta;

            pt_node_propagate_aug(right_node);

            hdr_ptr[0] = -target_size;
            hdr_ptr[target_size - 1] = -target_size;
        } else {
            // Safely unlink the old neighbor block from whatever tracking tier it was in
            if (right_tag == 4 || right_tag == 6) {
                pt_idx_list_unlink(right_hdr, right_tag);
            } else if (right_tag >= 8) {
                pt_idx_tree_unlink(arena, pt_idx_hdr_to_tree(right_hdr, right_tag));
            }

            // Route the remainder to the correct structural tier
            if (state == 4) {
                word_t* remainder_hdr = hdr_ptr + target_size;
                hdr_ptr[0] = -target_size;
                hdr_ptr[target_size - 1] = -target_size;

				// >>> CRITICAL FIX: Stamp the free block header before inserting into the tree <<<
                remainder_hdr[0] = delta;

                pt_idx_tree_insert(arena, NULL, remainder_hdr, delta);
            } else if (state == 2 || state == 3) {
                word_t* remainder_hdr = hdr_ptr + target_size;
                hdr_ptr[0] = -target_size;
                hdr_ptr[target_size - 1] = -target_size;
                pt_idx_list_insert(arena, remainder_hdr, delta);
            } else {
                // Slack remains inside the block
                hdr_ptr[0] = -total_combined_size;
                hdr_ptr[total_combined_size - 1] = -total_combined_size;
            }
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

#if defined(__linux__) || defined(__ELF__)

// 1. Zero-Cost Hardware Aliasing (Linux / ELF)
PT_EXPORT void* malloc(size_t size) __attribute__((alias("proteus_malloc")));
PT_EXPORT void  free(void* ptr) __attribute__((alias("proteus_free")));
PT_EXPORT void* realloc(void* ptr, size_t size) __attribute__((alias("proteus_realloc")));
PT_EXPORT int   posix_memalign(void** memptr, size_t alignment, size_t size) __attribute__((alias("proteus_posix_memalign")));

#else

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

#endif

PT_EXPORT void* calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void* ptr = proteus_malloc(total);
    if (ptr) {
        // Zero-fill allocated tracking space
        memset(ptr, 0, total);
    }
    return ptr;
}

PT_EXPORT void * reallocf(void *ptr, size_t size) {
	// Standard BSD behavior: reallocf(ptr, 0) completely frees the pointer and returns NULL
	if (__builtin_expect(0 == size, 0)) {
        proteus_free(ptr);
        return NULL;
    }

    void* new_ptr = proteus_realloc(ptr, size);
    
    // If reallocation fails, perform a defensive fallback purge on the original tracking block
    if (__builtin_expect(NULL == new_ptr, 0)) {
        proteus_free(ptr);
    }
    
    return new_ptr;
}

PT_EXPORT void* memalign(size_t alignment, size_t size) {
	void* ptr;
    if(__builtin_expect(0 == proteus_posix_memalign(&ptr, alignment, size), 1)) {
		return ptr;
	}
	return NULL;
}

PT_EXPORT void* aligned_alloc(size_t alignment, size_t size) { 
	return (alignment % size) ? memalign(alignment, size) : NULL;
}

PT_EXPORT void* valloc(size_t size) {
	return memalign(PT_DEFAULT_PAGE_BYTES, size);
}

PT_EXPORT void* pvalloc(size_t size) {
	const size_t mask = PT_DEFAULT_PAGE_BYTES - 1;
	return memalign(PT_DEFAULT_PAGE_BYTES , (size + mask) & mask);
}

#endif
