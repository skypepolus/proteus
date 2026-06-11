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
#include <sys/mman.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

#include <stdatomic.h>

__attribute__((aligned(64))) g_pt_t g_pt = { .once_control = PTHREAD_ONCE_INIT };

void pt_arena_prepare_fork(void) {
    // Acquire EVERY arena lock sequentially before the fork occurs
    for (long i = 0; i < g_pt.num_cores; i++) {
        // Use a heavy spin-to-sleep lock to ensure absolute ownership
        hybrid_lock(&g_pt.arenas[i].lock, MALLOC_SPIN_COUNTER);
    }
}

void pt_arena_parent_child_fork(void) { 
    for (long i = 0; i < g_pt.num_cores; i++) {
        hybrid_unlock(&g_pt.arenas[i].lock);
    }
}

void pt_arena_init_routine(void) 
{
	// Dynamically query the host kernel for its native virtual memory page size
    long os_page_size = sysconf(_SC_PAGESIZE);

    /* 1. Detect active hardware CPU core boundaries */
#if defined(__APPLE__)
    int nm[2] = {CTL_HW, HW_AVAILCPU};
    uint32_t count = 0;
    size_t len = sizeof(count);
    if (sysctl(nm, 2, &count, &len, NULL, 0) == -1 || count < 1) {
        count = 1; // Fallback bound
    }
    int detected_cores = (long)count;
#else
    int detected_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (detected_cores < 1) detected_cores = 1;
#endif

    /* 2. Calculate allocation requirements */
    size_t alloc_bytes = (size_t)detected_cores * sizeof(pt_arena_t);
    
    /* 3. Replace sbrk with a secure anonymous mmap segment */
    void* raw_mapping = mmap(
        NULL, 
        alloc_bytes, 
        PROT_READ | PROT_WRITE, 
        MAP_PRIVATE | MAP_ANONYMOUS, 
        -1, 
        0
    );
    
    if (raw_mapping == MAP_FAILED) {
        // Critical system failure hook: crash fast before memory corruption occurs
        __builtin_trap(); 
    }
    
    g_pt.arenas = (pt_arena_t*)raw_mapping;

    /* 4. Individual Core Arena Bootstrapping Loop */
    for (long i = 0; i < detected_cores; i++) {
        pt_arena_t* arena = &g_pt.arenas[i];

		if (__builtin_expect(os_page_size <= 0, 0)) {
			arena->page_size = PT_DEFAULT_PAGE_BYTES;
		} else {
			arena->page_size = (size_t)os_page_size;
		}

		// Compute the bitwise alignment mask (e.g., 4096 - 1 = 4095)
		arena->page_mask = arena->page_size - 1;
        
        // Construct your asymmetric hybrid-lock primitive
        hybrid_initial(&arena->lock);
        
        // Initialize your logical list sentinels to point back to themselves
        arena->segregate[0].head.next = &arena->segregate[0].tail;
        arena->segregate[0].tail.prev = &arena->segregate[0].head;
        
        arena->segregate[1].head.next = &arena->segregate[1].tail;
        arena->segregate[1].tail.prev = &arena->segregate[1].head;
        
        // Tree begins completely clear
        arena->root = NULL;

		arena->empty_superpage_cache = NULL; 
    }


	// Finally, store the core count using Release semantics. 
    // This forms a memory barrier that guarantees all preceding arena 
    // configurations are fully visible to any thread reading via Acquire.
    atomic_store_explicit(&g_pt.num_cores, detected_cores, memory_order_release);

	// Bind the fork handlers AFTER the core count is published
	pthread_atfork(pt_arena_prepare_fork, pt_arena_parent_child_fork, pt_arena_parent_child_fork);
}

void* pt_arena_watermark_release(pt_arena_t* arena, pt_superpage_t* superpage, word_t* final_hdr, word_t coalesced_size)
{
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
				return superpage_base;
            } else {
                // Cache is empty. Drop lock FIRST to perform the heavy system call
                hybrid_unlock(&arena->lock); 

				pt_platform_purge_pages	(superpage_base, PT_SUPER_PAGE_BYTES);

                // Re-acquire lock to safely publish the purged page
                hybrid_lock(&arena->lock, FREE_SPIN_COUNTER);
                if (arena->empty_superpage_cache == NULL) {
                    arena->empty_superpage_cache = superpage_base;
					return NULL;
                } else {
                    // Another thread filled the cache while we were unlocked! Destroy ours.
					return superpage_base;
                }
            }
        }
	} else { 
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
				word_t* final_ftr   = final_hdr + coalesced_size - 1;

				// 1. UNLINK: Remove from the tree so it can't be allocated
                pt_idx_tree_unlink(arena, node);

                // 2. INVERT TAGS: Disguise as an allocated block so neighbors don't coalesce and double-unlink
                final_hdr[0] = -coalesced_size;
                final_ftr[0] = -coalesced_size;

                // 3. DROP LOCK: Allow parallel allocations
                hybrid_unlock(&arena->lock);

                // 4. SYSCALL: Safe, unlocked page table purge
				pt_platform_purge_pages((void*)page_start, page_end - page_start);

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
            }
        }
    }
	return NULL;
}
