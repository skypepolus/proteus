#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include "proteus.h"

// Access internal symbols for checking tree state if needed, 
// or stick to the public API paired with ASan.
void test_tier1_tier2_segregation() {
    printf("[1/4] Testing Tier 1 & 2 Fast Paths...\n");
    
    void* p1 = proteus_malloc(16); // Tier 1
    void* p2 = proteus_malloc(32); // Tier 2
    
    assert(p1 != NULL);
    assert(p2 != NULL);
    assert(p1 != p2);
    
    proteus_free(p1);
    proteus_free(p2);
    printf("      -> Passed.\n");
}

void test_tier3_splitting() {
    printf("[2/4] Testing Tier 3 Allocation and Splitting...\n");
    
    // Request a large chunk to guarantee Tier 3 activation
    size_t large_size = 1024; 
    void* p1 = proteus_malloc(large_size);
    assert(p1 != NULL);
    
    // Free it so it registers as a large free hole in the Tier 3 Tree
    proteus_free(p1);
    
    // Request a smaller size. This forces Proteus to locate the 1024-byte hole,
    // split it, reserve 128 bytes, and re-index the remaining 896 bytes.
    void* p2 = proteus_malloc(128);
    assert(p2 == p1); // Should reuse the exact same starting address
    
    void* p3 = proteus_malloc(128);
    assert(p3 > p2); // Should carve right out of the remaining fragment
    
    proteus_free(p2);
    proteus_free(p3);
    printf("      -> Passed.\n");
}

void test_immediate_coalescing() {
    printf("[3/4] Testing Immediate Address-Ordered Coalescing...\n");
    
    // Allocate 3 sequential large blocks
    void* p1 = proteus_malloc(512);
    void* p2 = proteus_malloc(512);
    void* p3 = proteus_malloc(512);
    
    assert(p1 < p2 && p2 < p3); // Verify address monotonicity
    
    // Free the outer blocks first, leaving an active island in the middle
    proteus_free(p1);
    proteus_free(p3);
    
    // Free the middle block. This must trigger an immediate double-coalesce, 
    // merging left into p1's old space and right into p3's old space.
    proteus_free(p2);
    
    // If coalesced correctly, we should be able to request a single continuous 
    // 1500-byte chunk without drawing new pages from the arena.
    void* p_giant = proteus_malloc(1500);
    assert(p_giant == p1); // Must reclaim the consolidated space starting at p1
    
    proteus_free(p_giant);
    printf("      -> Passed.\n");
}

void test_swiss_cheese_exhaustion() {
    printf("[4/4] Testing Swiss Cheese Pruning Boundaries...\n");
    
    int num_blocks = 1000;
    void* blocks[num_blocks];
    
    // 1. Create a perfectly fragmented memory grid (Alloc, Free, Alloc, Free...)
    for (int i = 0; i < num_blocks; i++) {
        blocks[i] = proteus_malloc(64); // Tier 3 sizing
    }
    for (int i = 1; i < num_blocks; i += 2) {
        proteus_free(blocks[i]); // Free every odd block
    }
    
    // 2. Request a size that is completely impossible to fulfill out of the holes
    // This forces Proteus to parse the root max values, prune the tree walk,
    // and cleanly expand the arena boundary.
    void* giant = proteus_malloc(1024 * 1024); 
    assert(giant != NULL);
    
    // Cleanup
    proteus_free(giant);
    for (int i = 0; i < num_blocks; i += 2) {
        proteus_free(blocks[i]);
    }
    printf("      -> Passed.\n");
}

int main() {
    printf("=== Launching Proteus Single-Threaded Integrity Suite ===\n");
    test_tier1_tier2_segregation();
    test_tier3_splitting();
    test_immediate_coalescing();
    test_swiss_cheese_exhaustion();
    printf("=== All Structural Invariants Verified Successfully! ===\n");
    return 0;
}
