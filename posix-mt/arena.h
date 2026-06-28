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
#ifndef PT_ARENA_H
#define PT_ARENA_H

#include "primitives.h"
#include <hybrid_lock.h>
#include "arena-mt.h"
#include <pthread.h>
#include <sys/mman.h>

void pt_arena_init_routine(void);

#ifdef __APPLE__
    #if defined(__aarch64__)
        // ---------------------------------------------------------
        // Apple Silicon (M-Series) Fast-Path
        // Reads the Thread ID Register. Executes in ~1 cycle.
        // ---------------------------------------------------------
        static inline int get_proteus_arena_id(int max_arenas) {
            uint64_t tpidrro;
            __asm__ volatile ("mrs %0, tpidrro_el0" : "=r" (tpidrro));
            return (int)(tpidrro & 0xFF) % max_arenas;
        }

    #elif defined(__x86_64__)
        // ---------------------------------------------------------
        // Intel Mac (x86_64) Path
        // Uses CPUID to fetch the Local APIC ID.
        // ---------------------------------------------------------
        static inline int get_proteus_arena_id(int max_arenas) {
			uint32_t eax = 0x0B; // Extended Topology Leaf (can also use 0x1F)
			uint32_t ecx = 0;    // Sub-leaf 0
			uint32_t ebx, edx;

			__asm__ volatile (
				"cpuid"
				: "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
				: "a"(eax), "c"(ecx)
			);

			// EDX contains the 32-bit x2APIC ID of the current logical core
            return edx % max_arenas;
        }
    #else
        // Fallback for unknown Apple architectures
        #include <pthread.h>
        static inline int get_proteus_arena_id(int max_arenas) {
            uint64_t tid;
            pthread_threadid_np(NULL, &tid);
            return (int)(tid % max_arenas);
        }
    #endif

#else
    // Standard Linux hardware sharding
    #include <sched.h>
    static inline int get_proteus_arena_id(int max_arenas) {
        return sched_getcpu() % max_arenas;
    }
#endif

static inline pt_arena_t* pt_arena_get_local(void) 
{
    // 1. Read the core count using a lightweight Acquire memory order.
    // 2. Wrap it in a Clang branch hint assuming it is almost always true (> 0).
    int cores = atomic_load_explicit(&g_pt.num_cores, memory_order_acquire);
    
    if (__builtin_expect(cores == 0, 0)) {
        // Slow path: Only hit once in the entire application lifetime
		pthread_once(&pt_once_control, pt_arena_init_routine);
		while(0 == (cores = atomic_load_explicit(&g_pt.num_cores, memory_order_acquire))) {
			platform_spin_pause();
		}
    }

    return &g_pt.arenas[get_proteus_arena_id(cores)];
}

static inline void pt_arena_superpage_new(pt_superpage_t* new_page[2]) 
{
    size_t allocation_canvas = 2 * (size_t)PT_SUPER_PAGE_BYTES;
    
    // Allocate double the required size to guarantee a 4GB boundary exists inside
    void* raw_ptr = mmap(NULL, allocation_canvas, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (__builtin_expect(raw_ptr == MAP_FAILED, 0)) {
		new_page[0] = NULL;
		new_page[1] = NULL;
    }

    uintptr_t raw_addr = (uintptr_t)raw_ptr;
    uintptr_t alignment_mask = (uintptr_t)PT_SUPER_PAGE_BYTES - 1;
    
    // Calculate the first 4GB aligned address boundary inside our canvas
    uintptr_t aligned_addr = (raw_addr + alignment_mask) & ~alignment_mask;
    
    // Calculate the sizes of our excess prefix and suffix fragments
    size_t prefix_trim = aligned_addr - raw_addr;

    // Release the unaligned prefix back to the kernel
    if (prefix_trim > 0) {
        munmap(raw_ptr, prefix_trim);

		// Release the unaligned suffix back to the kernel
		size_t suffix_trim = allocation_canvas - prefix_trim - (size_t)PT_SUPER_PAGE_BYTES;
		if (suffix_trim > 0) {
			munmap((void*)(aligned_addr + (size_t)PT_SUPER_PAGE_BYTES), suffix_trim);
		}
		new_page[0] = (void*)aligned_addr;
		new_page[1] = NULL;
    } else {
		new_page[0] = raw_ptr;
		new_page[1] = (pt_superpage_t*)(raw_addr + PT_SUPER_PAGE_BYTES);
	}
}


#endif // PT_ARENA_H
