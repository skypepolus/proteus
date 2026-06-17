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
#ifdef PT_POSIX
#include <sys/mman.h>
#include <string.h> // For high-performance architecture-optimized memcpy/memset
#include <errno.h>
#include <immintrin.h>
#else
void *memcpy(void *dest, const void *src, size_t n);
#endif

static inline word_t* pt_core_try_segregated_alloc(pt_arena_t* arena, word_t size_words) {

	pt_link_t* head, *tail;
	switch((unsigned)size_words >> 1) {
	case 0:
		 __builtin_trap();
	case 1:
    /* --------------------------------------------------------------------
     * LANE Z: TARGET IS 2 WORDS (16 BYTES)
     * -------------------------------------------------------------------- */
	case 2:
    /* --------------------------------------------------------------------
     * LANE A: TARGET IS 4 WORDS (32 BYTES)
     * -------------------------------------------------------------------- */
		head = &arena->segregate[0].head;
		tail = &arena->segregate[0].tail;
        if (head->next != tail) {
            return pt_idx_link_to_hdr(head->next, 4);
        }
	case 3:
    /* --------------------------------------------------------------------
     * LANE B: TARGET IS 6 WORDS (48 BYTES)
     * -------------------------------------------------------------------- */
		head = &arena->segregate[1].head;
		tail = &arena->segregate[1].tail;
        if (head->next != tail) {
            return pt_idx_link_to_hdr(head->next, 6);
        }
	case 4:
    /* --------------------------------------------------------------------
     * LANE B: TARGET IS 8 WORDS (64 BYTES)
     * -------------------------------------------------------------------- */
	default:
        return pt_idx_tree_find_first_fit(arena->root, size_words);
    }

    return NULL; // List caches are fully depleted for these tiers
}

void proteus_free(void* ptr) 
{
    if (__builtin_expect(ptr == NULL, 0)) return;

    word_t* hdr_ptr = (word_t*)ptr - 1;
    if (__builtin_expect(0 <= *hdr_ptr, 0)) __builtin_trap(); // Hardware corruption trap
    
    word_t size_words = -*hdr_ptr;
	#ifndef PT_SINGLE_THREAD
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
	#endif/*PT_SINGLE_THREAD*/
    pt_superpage_t* superpage = pt_arena_superpage(ptr);
    pt_arena_t* arena         = superpage->arena_ptr;
    hybrid_lock(&arena->lock, FREE_SPIN_COUNTER);

    word_t* final_hdr = pt_idx_coalesce_state_machine(arena, hdr_ptr, hdr_ptr + size_words);
	#ifdef PT_POSIX
	if (pt_arena_watermark_no(final_hdr[0])) { 
		hybrid_unlock(&arena->lock);
		return;
	} else { 
		void* superpage_base = pt_arena_watermark_release(arena, superpage, final_hdr, size_words);
		#ifdef PT_SINGLE_THREAD
		(void)superpage_base;
		#else
		if(superpage_base) {
			hybrid_unlock(&arena->lock);
			munmap(superpage_base, PT_SUPER_PAGE_BYTES);
			return;
		}
		#endif/*PT_SINGLE_THREAD*/
	}
	#else
	(void)final_hdr;
	#endif
	hybrid_unlock(&arena->lock);
}

void* proteus_memalign(size_t alignment, size_t size_bytes) 
{
	uintptr_t aligned_payload;
    /* ============================================================================
     * LANE 1: STANDARD MALLOC ROUTING (ALIGNMENT <= 16 BYTES)
     * ============================================================================ */
    // Because Proteus natively aligns all blocks to 16-byte boundaries (2 words),
    // standard small alignments are already guaranteed to be met.

	alignment = alignment < sizeof(word_t) * 2 ? 0 : alignment;

	/* ============================================================================
     * LANE 2: ARENA-BASED ALIGNMENT CARVING (ALIGNMENT > 16 BYTES)
     * ============================================================================ */
	// Request enough padding to guarantee we can shift up to the alignment boundary
    size_t request_bytes = size_bytes + alignment;
    word_t request_words = PT_TOTAL_BLOCK_WORDS(request_bytes);

    pt_arena_t* arena = pt_arena_get_local();
	#ifndef PT_SINGLE_THREAD
    if (__builtin_expect(request_words <= PT_HUGE_THRESHOLD_WORDS, 1)) {
	#endif/*PT_SINGLE_THREAD*/
		// === ENCAPSULATE CRITICAL MUTATION SECTION ===
		// ----------------------------------------------------------------------------
		// PASS A: Home Arena Fast-Path
		// ----------------------------------------------------------------------------
		if(__builtin_expect(!hybrid_try(&arena->lock), 0)) {
			// ----------------------------------------------------------------------------
			// PASS B: Work-Stealing Neighbor Core Scan
			// ----------------------------------------------------------------------------
			int i, j, num_cores = g_pt.num_cores;
			pt_arena_t* arenas = g_pt.arenas;
			for (i = 1, j = arena - arenas; i < num_cores; i++) {
				arena = &arenas[(i + j) % num_cores];
				if (hybrid_try(&arena->lock)) {
					break;
				}
			}
			if(__builtin_expect(num_cores <= i, 0)) {
				arena = &arenas[(i + j) % num_cores];
				hybrid_lock(&arena->lock, MALLOC_SPIN_COUNTER); // Reacquire lock
			}
		}

        // 1. Allocate a raw block large enough from the arena
		word_t* left_hdr;

		if(__builtin_expect(NULL == (left_hdr = pt_core_try_segregated_alloc(arena, request_words)), 0)) {
			if(__builtin_expect(NULL == (left_hdr = pt_core_allocate_superpage_fallback(arena, request_words)), 0)) {
				hybrid_unlock(&arena->lock); 
				return NULL;
			}
		}

		// 2. Find the aligned payload boundary inside the raw payload
		aligned_payload = 0 == alignment ? (uintptr_t)(left_hdr + 1) : ( (uintptr_t)(left_hdr + 1) + alignment - 1) & ~(alignment - 1);

        // 3. Calculate structural boundaries
		switch((unsigned)left_hdr[0] >> 1) {
		case 0:
			__builtin_trap();
		case 1:
			left_hdr[0] = -2;
			left_hdr[1] = -2;
			break;
		case 2:
		case 3:
			pt_idx_list_split_state_machine(arena, 
				left_hdr, 
				(word_t*)aligned_payload - 1, 
				(word_t*)aligned_payload - 1 + PT_TOTAL_BLOCK_WORDS(size_bytes),
				left_hdr + left_hdr[0]);
			break;
		case 4:
		default:
			pt_idx_tree_split_state_machine(arena,
				left_hdr, 
				(word_t*)aligned_payload - 1, 
				(word_t*)aligned_payload - 1 + PT_TOTAL_BLOCK_WORDS(size_bytes),
				left_hdr + left_hdr[0]);
			break;
		}

		// Safely unlock before passing remnants to free(), preventing deadlocks completely
        hybrid_unlock(&arena->lock); 
        // === END OF CRITICAL SECTION ===

		return (void*)aligned_payload;
	#ifndef PT_SINGLE_THREAD
	}

    /* ============================================================================
     * LANE 3: DIRECT-MMAP ALIGNMENT PROMOTION (HUGE TIER)
     * ============================================================================ */
    // Oversize the virtual memory canvas to guarantee room for the alignment shift
    // plus our 3-word system metadata block layout.
    size_t metadata_headroom = 3 * sizeof(word_t);
    size_t total_mmap_bytes  = size_bytes + alignment + metadata_headroom;

    uintptr_t mmap_base = (uintptr_t)mmap(NULL, total_mmap_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (__builtin_expect(mmap_base == (uintptr_t)MAP_FAILED, 0)) {
        return NULL;
    }

    // Compute the first perfectly aligned address that leaves enough headroom for your metadata
    aligned_payload = (uintptr_t)mmap_base + metadata_headroom;
	aligned_payload = 0 == alignment ? aligned_payload : (aligned_payload + alignment - 1) & ~(alignment - 1);

    // Anchor the 3-word tracking structure immediately behind the aligned payload boundary
    word_t* hdr_ptr = (word_t*)aligned_payload - 1;

    // Calculate the sizes of our excess prefix and suffix fragments
    size_t prefix_trim = ((uintptr_t)(hdr_ptr - 2) & ~arena->page_mask) - (uintptr_t)mmap_base;
    size_t suffix_trim = (uintptr_t)mmap_base + total_mmap_bytes - (((uintptr_t)aligned_payload + size_bytes + arena->page_mask) & ~arena->page_mask);

    // Release the unaligned prefix back to the kernel
    if (prefix_trim > 0) {
        munmap((void*)mmap_base, prefix_trim);
		mmap_base += prefix_trim;
		total_mmap_bytes -= prefix_trim;
    }
    
    // Release the unaligned suffix back to the kernel
    if (suffix_trim > 0) {
        munmap((void*)(mmap_base + total_mmap_bytes - suffix_trim), suffix_trim);
		total_mmap_bytes -= suffix_trim;
    }

	// >>> CRITICAL FIX 1: Ensure size_words breaches the huge threshold so free() routes it to munmap <<<
    word_t encoded_size = PT_SUPER_PAGE_WORDS;
    
    // Stamp your layout invariants perfectly
    hdr_ptr[0]  = -encoded_size;          // Universal uniform size (tells free() it's huge)
    hdr_ptr[-1] = (word_t)total_mmap_bytes; // Raw byte count for munmap
    hdr_ptr[-2] = (word_t)mmap_base;        // Original OS pointer for munmap

    return (void*)aligned_payload;
	#endif/*PT_SINGLE_THREAD*/
}

static inline uintptr_t next_power_of_2(uintptr_t n) {
	switch(n) {
	case 0:
	case 1:
		return n;
	default:
		if (sizeof(uintptr_t) == 8) {
			return (uintptr_t)1 << (sizeof(uintptr_t) * 8 - __builtin_clzll(n - 1));// Uses 64-bit built-in 
		} else {
			return (uintptr_t)1 << (sizeof(uintptr_t) * 8 - __builtin_clz(n - 1));	// Uses 32-bit built-in 
		}
	}
	return 0;
}

static inline size_t PT_HUGE_PAYLOAD_BYTES(word_t* hdr_ptr) {
#ifdef PT_SINGLE_THREAD
	(void)hdr_ptr; __builtin_trap(); return 0;
#else
	return (uintptr_t)(hdr_ptr[-2]) + (uintptr_t)(hdr_ptr[-1]) - (uintptr_t)(hdr_ptr + 1);
#endif
}

static inline word_t PT_HUGE_BLOCK_WORDS(word_t* hdr_ptr) {
#ifdef PT_SINGLE_THREAD
	(void)hdr_ptr; __builtin_trap(); return 0;
#else
	return 2 + PT_HUGE_PAYLOAD_BYTES(hdr_ptr) / sizeof(word_t);
#endif
}

void* proteus_realloc(void* ptr, size_t size_bytes) 
{
	size_t next_bytes = next_power_of_2(size_bytes);
    // 1. Edge Case Gateways
    if (ptr == NULL) {
        return proteus_memalign(0, next_bytes);
    }

    word_t* hdr_ptr = (word_t*)ptr - 1;
    if (__builtin_expect(0 <= *hdr_ptr, 0)) __builtin_trap(); // Guard against corruption
    
	size_t current_bytes;
	(void)current_bytes;

    word_t current_words = -*hdr_ptr < -(word_t)PT_HUGE_THRESHOLD_WORDS ? PT_HUGE_BLOCK_WORDS(hdr_ptr) : -*hdr_ptr;
    word_t target_words  = PT_TOTAL_BLOCK_WORDS(next_bytes);

	if(current_words == target_words) {
		return ptr;
	}

	// >>> CRITICAL FIX 4: Route realloc to the block's true hardware home arena, NOT the local thread! <<<
    pt_superpage_t* superpage;
    pt_arena_t* arena;
	word_t* remainder_hdr;
    
	unsigned target_is_bigger = (target_words > current_words);
    unsigned target_is_huge  = (target_words > (word_t)PT_HUGE_THRESHOLD_WORDS);
    unsigned current_is_huge = (-*hdr_ptr > (word_t)PT_HUGE_THRESHOLD_WORDS);
	unsigned state = target_is_bigger * 4 + target_is_huge * 2 + current_is_huge;

	switch(state) {
	case 0 * 4 + 0 * 2 + 0: // target is smaller & target is not huge & current is not huge
	/* ============================================================================
	 * MATRIX LANE 1: ARENA-MANAGED CONVERSIONS (SMALL/TREE TIER)
	 * ============================================================================ */
		superpage = pt_arena_superpage(ptr);
		arena = superpage->arena_ptr;
		hybrid_lock(&arena->lock, MALLOC_SPIN_COUNTER);

		hdr_ptr[0] = -target_words;
		hdr_ptr[target_words - 1] = -target_words;

		remainder_hdr = hdr_ptr + target_words;
		word_t delta = current_words - target_words;
		remainder_hdr[0] = -delta; 
		remainder_hdr[delta - 1] = -delta; 
		(void)pt_idx_coalesce_state_machine(arena, remainder_hdr, remainder_hdr + delta);

		hybrid_unlock(&arena->lock);

		return ptr;

	case 0 * 4 + 0 * 2 + 1: // targer is smaller & target is not huge & current is huge
		current_bytes = PT_HUGE_PAYLOAD_BYTES(hdr_ptr);
		break;

	case 0 * 4 + 1 * 2 + 1: // target is smaller & target is huge     & current is huge
	#ifdef PT_SINGLE_THREAD
		__builtin_trap();
	#else
    /* ============================================================================
     * MATRIX LANE 2: DIRECT-MMAP CONVERSIONS (HUGE TIER)
     * ============================================================================ */
		current_bytes = PT_HUGE_PAYLOAD_BYTES(hdr_ptr);
		arena = pt_arena_get_local();
		size_t suffix_trim = (uintptr_t)ptr + current_bytes - (((uintptr_t)ptr + next_bytes + arena->page_size - 1) & arena->page_mask);
		
		// Release the unaligned suffix back to the kernel
		if (suffix_trim > 0) {
			munmap((void*)((uintptr_t)ptr + current_bytes - suffix_trim), suffix_trim);
			hdr_ptr[-1] -= suffix_trim;
		}
		return ptr;
	#endif
	case 1 * 4 + 0 * 2 + 0: // target is bigger  & target is not huge & current is not huge
	/* ============================================================================
	 * MATRIX LANE 3: ARENA-MANAGED CONVERSIONS (SMALL/TREE TIER)
	 * ============================================================================ */
		current_bytes = (-*hdr_ptr - 2) * sizeof(word_t);
		superpage = pt_arena_superpage(ptr);
		arena = superpage->arena_ptr;
		hybrid_lock(&arena->lock, MALLOC_SPIN_COUNTER);

		remainder_hdr = hdr_ptr + target_words;

		word_t* right_hdr = hdr_ptr + current_words;
		word_t right_tag = *right_hdr;

		// High-Speed In-Place Rightward Expansion Shortcut
		if (target_words <= (current_words + right_tag)) {

			word_t total_combined_size = current_words + right_tag;
			word_t delta = total_combined_size - target_words;
			unsigned delta_state = (2 <= delta) + (4 <= delta) + (8 <= delta);
			unsigned right_state = (2 <= right_tag) + (4 <= right_tag) + (8 <= right_tag);
			state = delta_state * 4 + right_state;

			switch(state) {
			case 0 * 4 + 1: // Remnant to Zero
				break;
			case 0 * 4 + 2: // List to Zero
				pt_idx_list_unlink(right_hdr, right_tag);
				break;
			case 0 * 4 + 3: // Tree to Zero
				pt_idx_tree_unlink(arena, pt_idx_hdr_to_tree(right_hdr, right_tag));
				break;
			case 1 * 4 + 2: // List to Remanant
				pt_idx_list_unlink(right_hdr, right_tag);
				remainder_hdr[delta - 1] = delta; 
				remainder_hdr[0] = delta; 
				break;
			case 1 * 4 + 3: // Tree to Remanant
				pt_idx_tree_unlink(arena, pt_idx_hdr_to_tree(right_hdr, right_tag));
				remainder_hdr[delta - 1] = delta; 
				remainder_hdr[0] = delta; 
				break;
			case 2 * 4 + 2: // List to List
				pt_idx_list_unlink(right_hdr, right_tag);
				// >>> CRITICAL FIX: Stamp the free block header before inserting into the tree <<<
				pt_idx_list_insert(arena, remainder_hdr, delta);
				break;
			case 2 * 4 + 3: // Tree to List
				pt_idx_tree_unlink(arena, pt_idx_hdr_to_tree(right_hdr, right_tag));
				// >>> CRITICAL FIX: Stamp the free block header before inserting into the tree <<<
				pt_idx_list_insert(arena, remainder_hdr, delta);
				break;
			case 3 * 4 + 3: // Tree to Tree
				pt_idx_tree_absorb_stationary_right(pt_idx_hdr_to_tree(right_hdr, right_tag), delta);
				remainder_hdr[0] = delta;
				break;
			case 0 * 4 + 0: // Zero to Zero
			case 1 * 4 + 0: // Zero to Remnant
			case 1 * 4 + 1: // Remnant to Remnant
			case 2 * 4 + 0: // Zero to List
			case 2 * 4 + 1: // Remnant to List
			case 3 * 4 + 0: // Zero to Tree
			case 3 * 4 + 1: // Remnant to Tree
			case 3 * 4 + 2: // List to Tree
			default: // Impossible
				 __builtin_trap();
			}
			hdr_ptr[target_words - 1] = -target_words;
			hdr_ptr[0] = -target_words;

			hybrid_unlock(&arena->lock);
			return ptr;
		}
		hybrid_unlock(&arena->lock);
		break;

	case 1 * 4 + 1 * 2 + 0: // target is bigger  & target is huge     & current is not huge
		current_bytes = (-*hdr_ptr - 2) * sizeof(word_t);
		break;

	case 1 * 4 + 1 * 2 + 1: // target is bugger  & target is huge     & current it huge
		current_bytes = PT_HUGE_PAYLOAD_BYTES(hdr_ptr);
		break;

	case 0 * 4 + 1 * 2 + 0: // target is smaller & target is huge     & current is not huge
	case 1 * 4 + 0 * 2 + 1: // target is bigger  & target is not huge & current is huge
	default: // impossible
		__builtin_trap();
	}
    // Hard Managed Fallback: In-place options exhausted, migrate data
    void* new_payload = proteus_memalign(0, next_bytes);
    if (__builtin_expect(new_payload == NULL, 0)) {
        return NULL;
    }
    memcpy(new_payload, ptr, size_bytes < current_bytes ? size_bytes : current_bytes);
    proteus_free(ptr);

    return new_payload;
}
