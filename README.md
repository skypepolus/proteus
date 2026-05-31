proteus/
├── .git/
├── .gitmodules             # Tracks the hybrid-lock submodule configuration
├── Makefile                # Unified build system linking proteus + hybrid-lock
├── deps/
│   └── hybrid-lock/        # Git Submodule root directory
│       ├── include/
│       │   └── hybrid_lock.h  # Core atomic lock structure
│       └── Makefile
├── include/
│   └── proteus.h           # Public API header (drop-in replacement for malloc)
├── src/
│   ├── arena.c             # Bootstrapping & Thread ID modulo routing
│   ├── arena.h             # Core-mapped page-tracking boundaries
│   ├── core.c              # Main entry gates (my_malloc, my_free)
│   ├── index.c             # List mutations & Augmented tree algorithms
│   ├── index.h             # Structural query definitions
│   └── primitives.h        # Milestone 1: Word-scaling math & block definitions
└── tests/
    ├── test_correctness.c
    └── test_bench.c
