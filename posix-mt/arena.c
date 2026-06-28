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
#include "posix.h"
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

#include <stdatomic.h>

__attribute__((aligned(64))) g_pt_t g_pt; 

__attribute__((aligned(64))) pthread_once_t pt_once_control = PTHREAD_ONCE_INIT;

void pt_arena_prepare_fork(void) {
    // Acquire EVERY arena lock sequentially before the fork occurs
    for (long i = 0; i < g_pt.num_cores; i++) {
        // Use a heavy spin-to-sleep lock to ensure absolute ownership
        hybrid_lock(g_pt.arenas[i].lock, MALLOC_SPIN_COUNTER);
    }
}

void pt_arena_parent_fork(void) { 
    for (long i = 0; i < g_pt.num_cores; i++) {
        hybrid_unlock(g_pt.arenas[i].lock);
    }
}

static struct hybrid* child_lock = NULL;
static int child_lock_count = 0;

void pt_arena_child_fork(void) { 
	long i;
    for (i = 0; i < g_pt.num_cores && 0 < child_lock_count; i++, child_lock++, child_lock_count--) {
		pt_arena_t* arena = &g_pt.arenas[i];
		arena->lock = child_lock;
		hybrid_initial(arena->lock);
	}
	if(i < g_pt.num_cores) {
		size_t alloc_bytes = ((size_t)g_pt.num_cores - i) * sizeof(struct hybrid);
		size_t page_mask = g_pt.page_mask;
		size_t aligned_bytes = (alloc_bytes + page_mask) & page_mask;
		child_lock = (struct hybrid*)mmap(NULL, aligned_bytes, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
		if(MAP_FAILED == (void*)child_lock) {
			__builtin_trap();
		}
		child_lock_count = aligned_bytes / sizeof(struct hybrid);
		for (; i < g_pt.num_cores; i++, child_lock++, child_lock_count--) {
			pt_arena_t* arena = &g_pt.arenas[i];
			arena->lock = child_lock;
			hybrid_initial(arena->lock);
		}
	}
}

void pt_arena_init_routine(void) 
{
	// Dynamically query the host kernel for its native virtual memory page size
    uintptr_t os_page_size = sysconf(_SC_PAGESIZE);

	if (__builtin_expect(os_page_size <= 0, 0)) {
		g_pt.page_size = PT_DEFAULT_PAGE_BYTES;
	} else {
		g_pt.page_size = (size_t)os_page_size;
	}
	// Compute the bitwise alignment mask (e.g., 4096 - 1 = 4095)
	g_pt.page_mask = g_pt.page_size - 1;

	// Environmental Variables
	char* malloc_spin = getenv("PROTEUS_MALLOC_SPIN");
	if(malloc_spin) {
		g_pt.malloc_spin = strtoul(malloc_spin, NULL, 10);
	} else {
		g_pt.malloc_spin = DEFAULT_MALLOC_SPIN_COUNTER;
	}

	char* superpage_bytes = getenv("PROTEUS_SUPERPAGE_BYTES");
	if(superpage_bytes ) {
		g_pt.superpage_bytes = next_power_of_2(strtoul(superpage_bytes, NULL, 10));
	} else {
		g_pt.superpage_bytes = PT_DEFAULT_SUPER_PAGE_BYTES;
	}
	g_pt.superpage_bytes = g_pt.superpage_bytes < os_page_size ? os_page_size : g_pt.superpage_bytes;
	g_pt.superpage_mask = g_pt.superpage_bytes - 1;
	g_pt.superpage_words = g_pt.superpage_bytes / sizeof(word_t);

	char* watermark_bytes = getenv("PROTEUS_WATERMARK_BYTES");
	if(watermark_bytes) {
		g_pt.watermark_bytes = ((uintptr_t)strtoul(watermark_bytes, NULL, 10) + g_pt.page_mask) & g_pt.page_mask;
	} else {
		g_pt.watermark_bytes = PT_DEFAULT_INDEX_WATERMARK_BYTES;
	}
	g_pt.watermark_bytes = g_pt.watermark_bytes < os_page_size * 2 ? os_page_size * 2 : g_pt.watermark_bytes;
	g_pt.watermark_mask = g_pt.watermark_bytes - 1;
	g_pt.watermark_words = g_pt.watermark_bytes / sizeof(word_t);

    /* 1. Detect active hardware CPU core boundaries */
#if defined(__APPLE__)
    uint32_t count = 0;
    size_t len = sizeof(count);
	// Get logical core count (Includes Hyper-Threading threads)
    sysctlbyname("hw.logicalcpu", &count, &len, NULL, 0);
    int detected_cores = (long)count;
#else
    int detected_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (detected_cores < 1) detected_cores = 1;
#endif

    /* 2. Calculate allocation requirements */
    size_t alloc_bytes = (size_t)detected_cores * sizeof(pt_arena_t);
    
    /* 3. Calculate allocation requirements */
    void* raw_mapping = mmap(NULL, alloc_bytes, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if(MAP_FAILED == raw_mapping) {
		__builtin_trap();
	}
    
	g_pt.arenas = raw_mapping;	

	/* 4. Individual Core Arena Bootstrapping Loop */
	for (long i = 0; i < detected_cores; i++) {
		pt_arena_t* arena = &g_pt.arenas[i];
	
		// Construct your asymmetric hybrid-lock primitive
		arena->lock = arena->parent_lock;
		hybrid_initial(arena->lock);
	
		// Initialize your logical list sentinels to point back to themselves
		arena->segregate[0].head.next = &arena->segregate[0].tail;
		arena->segregate[0].tail.prev = &arena->segregate[0].head;
		
		arena->segregate[1].head.next = &arena->segregate[1].tail;
		arena->segregate[1].tail.prev = &arena->segregate[1].head;
	
		// Tree begins completely clear
		arena->root = NULL;

		arena->empty_superpage_cache = NULL; 
	}

	size_t page_mask = g_pt.page_mask;
	size_t aligned_bytes = (alloc_bytes + page_mask) & page_mask;
	if(aligned_bytes - alloc_bytes > sizeof(struct hybrid) * child_lock_count) {
		child_lock = (struct hybrid*)&g_pt.arenas[detected_cores];
		child_lock_count = (aligned_bytes - alloc_bytes) / sizeof(struct hybrid);
	}

	// Finally, store the core count using Release semantics. 
	// This forms a memory barrier that guarantees all preceding arena 
	// configurations are fully visible to any thread reading via Acquire.
	atomic_store_explicit(&g_pt.num_cores, detected_cores, memory_order_release);

	// Bind the fork handlers AFTER the core count is published
	pthread_atfork(pt_arena_prepare_fork, pt_arena_parent_fork, pt_arena_child_fork);
}

void* pt_arena_watermark_release(pt_arena_t* arena, pt_superpage_t* superpage, word_t* final_hdr, word_t size_words)
{
	/* ============================================================================
     * THE UNIFIED DIFFERENTIAL WATERMARK ADVISORY FILTER
     * ============================================================================ */
	if (__builtin_expect(final_hdr[0] == PT_HUGE_THRESHOLD_WORDS, 0)) {
        // Only release the superpage if it's not the absolute last one in the tree
        if (arena->root->left || arena->root->right) { 
            // 1. Unlink from the tree first so it becomes invisible to the allocator
            pt_idx_tree_unlink(arena, pt_idx_hdr_to_tree(final_hdr, final_hdr[0]));
            void* superpage_base = (void*)superpage;
            
            if (arena->empty_superpage_cache) { 
                // Cache is full. Drop lock and DESTROY the page. (No madvise needed)
				return superpage_base;
            } else {
                // Cache is empty. Drop lock FIRST to perform the heavy system call
                hybrid_unlock(arena->lock); 

				pt_platform_purge_pages	(superpage_base, PT_SUPER_PAGE_BYTES);

                // Re-acquire lock to safely publish the purged page
                hybrid_lock(arena->lock, FREE_SPIN_COUNTER);

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
		pt_arena_watermark(arena, final_hdr, size_words);
    }
	return NULL;
}
