#ifndef PT_ARENA_H
#define PT_ARENA_H

#include "primitives.h"
#include <hybrid_lock.h>
#include <pthread.h>

// Extended core arena layout
typedef struct pt_arena {
    struct hybrid lock;
    pt_list_t segregate[2]; // Two small segregated lists
    pt_redblack_t* root;    // Augmented address-ordered First-Fit tree root
} pt_arena_t;

typedef struct pt_superpage {
    pt_arena_t* arena_ptr;                        // 1 Word
	word_t reserved_align; // Maintained for strict 2-word header structural alignment
    word_t ftr[1];                                // 1 Word (Low Zero Sentinel)
    word_t block_words[PT_HUGE_THRESHOLD_WORDS];  // PT_SUPER_PAGE_WORDS - 4 Words
    word_t hdr[1];                                // 1 Word (High Zero Sentinel)
} pt_superpage_t;

// Change g_pt_num_cores to a standard atomic int
extern _Atomic int g_pt_num_cores;
extern pt_arena_t* g_pt_arenas;

void pt_arena_env_bootstrap(void);

static inline pt_arena_t* pt_arena_get_local(void) {
    // 1. Read the core count using a lightweight Acquire memory order.
    // 2. Wrap it in a Clang branch hint assuming it is almost always true (> 0).
    int cores = atomic_load_explicit(&g_pt_num_cores, memory_order_acquire);
    
    if (__builtin_expect(cores == 0, 0)) {
        // Slow path: Only hit once in the entire application lifetime
        pt_arena_env_bootstrap();
        cores = atomic_load_explicit(&g_pt_num_cores, memory_order_acquire);
    }

    uint64_t tid;
#if defined(__APPLE__)
    pthread_threadid_np(pthread_self(), &tid);
#else
    tid = (uint64_t)pthread_self();
#endif

    return &g_pt_arenas[tid % cores];
}

#define PT_SUPER_PAGE_BYTES (1ULL << 32) // Strict 4GB tracking scale

static inline pt_superpage_t* pt_arena_superpage_new(void) {
    size_t allocation_canvas = 2 * (size_t)PT_SUPER_PAGE_BYTES;
    
    // Allocate double the required size to guarantee a 4GB boundary exists inside
    void* raw_ptr = mmap(NULL, allocation_canvas, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (__builtin_expect(raw_ptr == MAP_FAILED, 0)) {
        return NULL;
    }

    uintptr_t raw_addr = (uintptr_t)raw_ptr;
    uintptr_t alignment_mask = (uintptr_t)PT_SUPER_PAGE_BYTES - 1;
    
    // Calculate the first 4GB aligned address boundary inside our canvas
    uintptr_t aligned_addr = (raw_addr + alignment_mask) & ~alignment_mask;
    
    // Calculate the sizes of our excess prefix and suffix fragments
    size_t prefix_trim = aligned_addr - raw_addr;
    size_t suffix_trim = allocation_canvas - prefix_trim - (size_t)PT_SUPER_PAGE_BYTES;

    // Release the unaligned prefix back to the kernel
    if (prefix_trim > 0) {
        munmap(raw_ptr, prefix_trim);
    }
    
    // Release the unaligned suffix back to the kernel
    if (suffix_trim > 0) {
        munmap((void*)(aligned_addr + (size_t)PT_SUPER_PAGE_BYTES), suffix_trim);
    }

    return (pt_superpage_t*)aligned_addr;
}

static inline pt_superpage_t* pt_arena_superpage(void* ptr) {
    uintptr_t mask = ~((uintptr_t)PT_SUPER_PAGE_BYTES - 1);
    return (pt_superpage_t*)((uintptr_t)ptr & mask);
}

#endif // PT_ARENA_H
