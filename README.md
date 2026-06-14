# Proteus Elastic Memory Engine

A hyper-concurrent, thread-agnostic, per-core arena memory allocator featuring layout-driven cross-thread deallocation routing, cascading segregated list-splitting, and geometric differential watermark virtual memory eviction.

---

## Abstract

Modern high-performance allocators rely heavily on Thread-Local Storage (TLS) to minimize lock contention. However, this thread-centric model causes severe memory bloat (Resident Set Size inflation) as software threads scale, and introduces high dynamic linking costs via `__tls_get_addr`. 

**Proteus** addresses these inefficiencies by shifting from a thread-centric model to a hardware-centric model. Proteus binds memory sub-arenas strictly to the number of physical CPU cores ($N_{\text{cores}}$) using thread-agnostic modulo routing via `sched_getcpu()` or thread affinity hashes. Cross-thread deallocations are resolved in a single CPU clock cycle without global radix trees or centralized lookup caches. This is achieved by ensuring all sub-arenas allocate out of strict 4GB-aligned hardware superpages (`pt_superpage_t`). Clearing the lower 32 bits of any payload pointer instantly returns its managing structure base address, enabling fast cross-thread lock acquisition.

Internally, Proteus organizes free memory into an optimized 3-Tier Allocation Matrix:
1. **Tier 1 (4-Word/32-Byte Segregated List):** Instant $O(1)$ fast path with support for split fallbacks.
2. **Tier 2 (6-Word/48-Byte Segregated List):** Exact-fit allocation loop.
3. **Tier 3 ($\ge$ 8-Word Augmented Balanced Tree):** An address-ordered, First-Fit Red-Black tree augmented with subtree size maximums (`left_max`, `right_max`), allowing $O(\log n)$ block searches and splits.

To prevent virtual memory thrashing during rapid allocation and deallocation phases, Proteus eliminates heavy temporal decay loops and background worker daemon pools. Instead, it embeds an 8-word `pt_redblack_t` structural footprint directly into the trailing edge of every free block. The node's leading field handles a geometrically anchored, self-healing vector tracker (`node->hdr[0]`), which tracks the distance from the page-aligned purge boundary to the immutable end of the block. This allows the system to execute lazy, synchronous `madvise(MADV_FREE)` purges with zero tracking overhead on the allocation hot path.

---

## Repository Structure

```text
proteus/
├── LICENSE                 # Apache License 2.0
├── Makefile                # Unified build system linking proteus + hybrid-lock
├── .gitmodules             # Tracks the hybrid-lock submodule configuration
├── README.md               # Core architectural documentation & build guides
├── ALGORITHM.md            # Theoretical foundations, proofs, and academic citations
├── deps/
│   └── hybrid-lock/        # Git Submodule: Cache-isolated atomic spin-semaphore
│       └─── include/
│           └── hybrid_lock.h
├── include/
│   └── proteus.h           # Public API drop-in header
├─── src/
│    ├── arena.c             # Environment bootstrapping & CPU core-affinity routing
│    ├── arena.h             # 4GB Superpage structural alignment primitives
│    ├── core.h              # Adaptive spin-lock thresholds 
│    ├── core.c              # Core entry points (malloc, free, realloc, memalign)
│    ├── index.c             # Augmented trees & branchless coalescing switch matrix
│    ├── index.h             # Topological conversion macros & search steps
│    └── primitives.h        # Word-scaling metrics & structural overlay models
├─── posix-mt/               # POSIX multi-threaded porting
├─── posix-st/               # POSIX single-threaded porting
├─── wasm32-none-unknown-st  # WASM32 single-threaded porting
├─── freertos                # FreeRTOS porting
└── tests/
    └── test_stress.c       # Concurrent multi-threaded adversarial stress suite
```

---

## Build & Test Instructions

### Prerequisites
* A standard C toolchain (`clang` or `gcc`) supporting the C11 standard.
* POSIX Threads (`-pthread`) library support.

### 1. Initialize Submodule Dependency
Before compiling, fetch and sync the header-only atomic `hybrid-lock` submodule dependency:
```bash
make init-deps
```

### 2. Compile the Static Library
To build the production-ready library (`libproteus.a`) with aggressive compiler loop unrolling and macro inlining optimizations (`-O3`):
```bash
make -C posix-st
make -C posix-mt
```

### 3. Compiling and Executing the Stress Test
To evaluate the allocator under heavy multi-threaded contention, cross-thread freeing cycles, and size mutations, move the stress verification file into the `tests/` directory and compile with the verification suite:

#### Running with AddressSanitizer & ThreadSanitizer (Detect Work-Stealing Races)
```bash
make -C posix-st/tests run_stress

# For Linux Single-thread
/usr/bin/time -v env LD_PRELOAD=posix-st/libproteus-st.so posix-st/tests/test_stress_bench

make -C posix-mt/tests run_stress

# For Linux Multi-thread
sudo sysctl -w vm.overcommit_memory=1
/usr/bin/time -v env LD_PRELOAD=posix-mt/libproteus-mt.so posix-st/tests/test_stress_bench

# For macOS
DYLD_INSERT_LIBRARIES=posix-mt/libproteus-mt.so posix-st/tests/test_stress_bench

```

### 4. Cleaning Build Artifacts
To wipe out object files and local compiled library archives:
```bash
make clean
```

## Authors & Attribution

* **Young H. Song** - *Core Architecture & Design* - [@skypepolus](https://github.com/skypepolus/proteus.git )

See also the list of contributors who participated in optimizing this project.

## License

This project is licensed under the Apache License, Version 2.0 - see the LICENSE(LICENSE) file for complete details.
