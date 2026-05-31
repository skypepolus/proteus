#include "arena.h"
#include "core.h"
#include <sys/mman.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

// Global internal symbols
_Atomic int g_pt_num_cores = 0;
pt_arena_t* g_pt_arenas = NULL;

static pthread_once_t g_bootstrap_once = PTHREAD_ONCE_INIT;

void pt_arena_prepare_fork(void) {
    // Acquire EVERY arena lock sequentially before the fork occurs
    for (long i = 0; i < g_pt_num_cores; i++) {
        // Use a heavy spin-to-sleep lock to ensure absolute ownership
        hybrid_lock(&g_pt_arenas[i].lock, MALLOC_SPIN_COUNTER);
    }
}

void pt_arena_parent_fork(void) {
    // Release all arena locks in the parent process to resume normal operations
    for (long i = 0; i < g_pt_num_cores; i++) {
        hybrid_unlock(&g_pt_arenas[i].lock);
    }
}

void pt_arena_child_fork(void) {
    // Release all arena locks in the child process. 
    // This resets the synchronization state for the new single-threaded environment.
    for (long i = 0; i < g_pt_num_cores; i++) {
        hybrid_unlock(&g_pt_arenas[i].lock);
    }
}

static void pt_arena_init_routine(void) {
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
    
    g_pt_arenas = (pt_arena_t*)raw_mapping;

    /* 4. Individual Core Arena Bootstrapping Loop */
    for (long i = 0; i < detected_cores; i++) {
        pt_arena_t* arena = &g_pt_arenas[i];
        
        // Construct your asymmetric hybrid-lock primitive
        hybrid_initial(&arena->lock);
        
        // Initialize your logical list sentinels to point back to themselves
        arena->segregate[0].sentinel.next = &arena->segregate[0].sentinel;
        arena->segregate[0].sentinel.prev = &arena->segregate[0].sentinel;
        
        arena->segregate[1].sentinel.next = &arena->segregate[1].sentinel;
        arena->segregate[1].sentinel.prev = &arena->segregate[1].sentinel;
        
        // Tree begins completely clear
        arena->root = NULL;
    }

	// Inside your pt_arena_init_routine:
	pthread_atfork(pt_arena_prepare_fork, pt_arena_parent_fork, pt_arena_child_fork);

	// Finally, store the core count using Release semantics. 
    // This forms a memory barrier that guarantees all preceding arena 
    // configurations are fully visible to any thread reading via Acquire.
    atomic_store_explicit(&g_pt_num_cores, detected_cores, memory_order_release);
}

void pt_arena_env_bootstrap(void) {
    pthread_once(&g_bootstrap_once, pt_arena_init_routine);
}
