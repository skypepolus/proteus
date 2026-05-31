#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>

// Assuming your public interfaces are declared here
extern void* proteus_malloc(size_t size);
extern void  proteus_free(void* ptr);
extern void* proteus_realloc(void* ptr, size_t size);
extern int   proteus_posix_memalign(void** memptr, size_t alignment, size_t size);

#define NUM_THREADS 8
#define ALLOC_OPERATIONS 5000
#define CAROUSEL_SIZE 512

// Cross-Thread Freeing Carousel to stress the 4GB alignment rounding logic
void* volatile g_free_carousel[CAROUSEL_SIZE];
pthread_mutex_t     g_carousel_lock = PTHREAD_MUTEX_INITIALIZER;
_Atomic uint64_t    g_carousel_idx  = 0;

// Helper to simulate varying real-world size requests
static inline size_t get_mixed_size(int step) {
    switch (step % 6) {
        case 0: return 24;   // Fits into 4-word segregated list (32 bytes)
        case 1: return 40;   // Fits into 6-word segregated list (48 bytes)
        case 2: return 128;  // Intermediate tree block
        case 3: return 2048; // Large tree block
        case 4: return 200000; // Above 128KB, triggers madvise checks
        default: return 5000000; // Massive block, triggers Direct-MMAP bypass
    }
}

void* stress_worker(void* arg) {
    uintptr_t thread_id = (uintptr_t)arg;
    void* local_allocs[100] = {0};
    int local_count = 0;

    for (int i = 0; i < ALLOC_OPERATIONS; i++) {
        size_t req_size = get_mixed_size(i + thread_id);
        void* ptr = NULL;

        // 1. Stress Test standard allocations, direct mmaps, and aligned paths alternatingly
        if (i % 7 == 0) {
            size_t alignments[] = {32, 64, 4096, 2097152}; // Test up to 2MB huge page alignment
            size_t align = alignments[i % 4];
            int res = proteus_posix_memalign(&ptr, align, req_size);
            if (res == 0 && ptr) {
                // Verify alignment constraints physically
                assert(((uintptr_t)ptr & (align - 1)) == 0);
            }
        } else {
            ptr = proteus_malloc(req_size);
        }

        if (!ptr) continue; // Out of memory fallback testing

        // Touch the memory to ensure physical pages are dirty and backing store is active
        memset(ptr, 0xAA, req_size > 100 ? 100 : req_size);

        // 2. Stress Test Cross-Boundary Realloc conversions
        if (i % 11 == 0) {
            size_t new_size = req_size * 2;
            void* realloc_ptr = proteus_realloc(ptr, new_size);
            if (realloc_ptr) {
                ptr = realloc_ptr;
                memset(ptr, 0xBB, new_size > 100 ? 100 : new_size);
            }
        }

        // 3. Routing Mechanism: Local free vs Cross-thread free
        if (i % 3 == 0) {
            // Drop ptr onto the global carousel for another thread to free
            pthread_mutex_lock(&g_carousel_lock);
            uint64_t idx = g_carousel_idx % CAROUSEL_SIZE;
            void* old_ptr = g_free_carousel[idx];
            g_free_carousel[idx] = ptr;
            g_carousel_idx++;
            pthread_mutex_unlock(&g_carousel_lock);

            // If we kicked an older pointer out of the carousel, we free it here
            if (old_ptr) {
                proteus_free(old_ptr);
            }
        } else {
            // Keep it locally to release sequentially later
            if (local_count < 100) {
                local_allocs[local_count++] = ptr;
            } else {
                proteus_free(ptr);
            }
        }

        // Periodically flush local holdings to simulate burst allocations
        if (local_count == 100) {
            for (int j = 0; j < 100; j++) {
                proteus_free(local_allocs[j]);
                local_allocs[j] = NULL;
            }
            local_count = 0;
        }
    }

    // Clean up any remaining local allocations
    for (int j = 0; j < local_count; j++) {
        if (local_allocs[j]) proteus_free(local_allocs[j]);
    }

    return NULL;
}

int main(void) {
    pthread_t threads[NUM_THREADS];
    printf("[Proteus Stress Engine]: Initializing Execution Fabric...\n");
    printf("[Proteus Stress Engine]: Spawning %d concurrent workers executing %d operations each...\n", NUM_THREADS, ALLOC_OPERATIONS);

    memset((void*)g_free_carousel, 0, sizeof(g_free_carousel));

    // Spawn concurrent workers
    for (uintptr_t i = 0; i < NUM_THREADS; i++) {
        int rc = pthread_create(&threads[i], NULL, stress_worker, (void*)i);
        if (rc) {
            fprintf(stderr, "Failed to spawn worker thread %lu\n", i);
            exit(1);
        }
    }

    // Await execution convergence
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    // Drain the remaining items left over on our cross-thread carousel
    printf("[Proteus Stress Engine]: Workers converged. Draining cross-thread carousel residue...\n");
    for (int i = 0; i < CAROUSEL_SIZE; i++) {
        if (g_free_carousel[i]) {
            proteus_free(g_free_carousel[i]);
        }
    }

    printf("[Proteus Stress Engine]: Core Execution Completed Flawlessly.\n");
    return 0;
}
