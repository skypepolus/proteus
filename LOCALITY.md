# Hardware Locality, Core Migration, and End-to-End Throughput

This document provides a comprehensive systems-engineering analysis of memory locality, hardware cache behaviors, and operating system thread migration within the **Proteus Memory Engine**. It maps out the architectural trade-offs between raw allocation speed and structural data positioning, explaining why a deterministic $O(\log N)$ core-aligned engine regularly matches or outperforms sub-20ns thread-cached allocators (`mimalloc`, `jemalloc`, `tcmalloc`) in end-to-end production workloads.

---

## 1. The Allocation Vanity Metric vs. Access Realities

In isolation, benchmarks that only measure the time it takes to execute `malloc` and `free` present an incomplete picture of software efficiency. In high-performance backend frameworks—such as LLM inference engines, network proxies, or database engines—the duration a thread spends *inside* the allocator represents a minor fraction of the execution timeline. The overwhelming majority of CPU clock cycles are spent *reading and writing* the data structures themselves.

The total execution time ($T_{\text{total}}$) of an application workload loop can be modeled as:

$$T_{\text{total}} = T_{\text{alloc}} + T_{\text{access}}$$

Where $T_{\text{alloc}}$ is the cumulative time spent navigating allocator paths, and $T_{\text{access}}$ is the time the CPU execution pipeline spends fetching, caching, and modifying the allocated payloads.

### The L3 Cache Miss Penalty (The 10x Trap)
Traditional allocators optimize primarily for $T_{\text{alloc}}$, using thread-local caches to finish requests in an average of 10 ns. However, if their internal layout mechanisms degrade spatial locality, they inflate $T_{\text{access}}$ by forcing the processor to experience recurrent L3 cache misses or main memory (DRAM) accesses.

At current clock cycles, an L1 cache hit takes ~1 ns, an L2 hit takes ~4 ns, and an L3 hit takes ~15 ns, while a raw DRAM fetch forces the CPU to stall for **60 to 100+ ns**. 

* **The Thread-Cached Case:** Suppose an allocator finishes 3 sequential allocations in a lightning-fast 10 ns. However, due to size segregation, it scatters those related objects across distinct memory pages. When the application layer later accesses those objects together, the CPU suffers 3 independent DRAM fetches:
  $$T_{\text{traditional}} = 10\text{ ns (alloc)} + (3 \times 80\text{ ns}) = 250\text{ ns}$$
* **The Proteus Case:** Proteus takes a deliberate, tree-bounded 150 ns to complete an allocation in its Tier 3 layer. However, because it enforces address-ordered positioning, it places those 3 objects end-to-end inside the exact same continuous physical space. When the application processes them, the CPU suffers exactly 1 DRAM fetch, and the hardware prefetcher pulls the remaining structures into the L1 cache automatically:
  $$T_{\text{proteus}} = 150\text{ ns (alloc)} + 80\text{ ns (1 fetch + L1 hits)} = 230\text{ ns}$$

By prioritizing data alignment and cache cooperating layouts, Proteus trades allocation speed to minimize access latency, resulting in a net reduction in end-to-end execution time.

---

## 2. Spatial Locality & The Size-Class Scattering Trap

Modern symmetric multiprocessing (SMP) systems rely heavily on the **CPU Cache Line** (typically 64 or 128 bytes) as the atom of data migration between memory and execution cores. 

### Traditional Allocators: Size-Class Segregation
To completely eliminate lock contention on fast paths, traditional modern allocators route allocations through specialized pools called **Slabs** or **Runs**, which are rigidly dedicated to a single size class (e.g., separating 16-byte slots, 64-byte slots, and 128-byte slots into entirely separate physical pages).

When an application builds a compound object graph sequentially—such as creating an execution frame context header (16 bytes), a token tracking metadata array (64 bytes), and a text string buffer (128 bytes)—the allocator separates them. Even though these structures are created at the exact same microsecond, belong to the exact same request context, and will always be traversed together, they are scattered across different pages of physical RAM. This forces the hardware to burn multiple cache lines to manage a single request context.

### Proteus: Address-Ordered Continuous Clustering
Proteus completely bans size-class segregation for all allocations exceeding 48 bytes. All free holes within an arena's 4GB Superpage are indexed by their absolute physical addresses inside the Tier 3 tree.

When the application issues sequential requests of varying dimensions, Proteus satisfies them by carving contiguous blocks out of the same physical memory segment. They sit back-to-back in memory, packed densely into a minimal number of hardware cache lines. This maximizes **spatial prefetching**: fetching the first member of an object graph implicitly populates the local CPU cache with adjacent structural context elements without incurring extra hardware interconnect penalties.

---

## 3. Thread Migration (Core Changes) & The Interconnect Tax

Operating system kernels utilize load-balancing schedulers (such as Linux's EEVDF or Completely Fair Scheduler) to constantly re-distribute active software threads across physical CPU cores to normalize thermal limits and core utilization. This core-hopping behavior introduces a major hidden bottleneck for allocators optimized purely around software concepts.

### The TLS Alignment Fail: Cache Drag
Traditional allocators bind their internal caching matrices to the individual software thread via **Thread-Local Storage (TLS)**. 

1. When a thread executes on **Core 0**, its TLS allocation bin fills up with tracking markers. The memory blocks held in that local bin naturally populate Core 0’s private L1 and L2 cache lines.
2. If the OS scheduler suddenly migrates that software thread over to **Core 1**, the TLS cache travels with the thread logically, but the actual physical data remains trapped in Core 0’s local silicon.
3. On its next allocation or read loop on Core 1, the thread accesses its TLS cache. This triggers an immediate cache miss. The underlying hardware must initiate a cache-coherency broadcast across the internal CPU interconnect to fetch the cache lines from Core 0 over to Core 1.
4. If the thread now performs a deallocation on Core 1 for memory it originally claimed while on Core 0, it must interact with a remote structure, causing cache line invalidations and "ping-ponging" between the distinct core caches.

### The Proteus Solution: Dynamic Topology Realignment
Proteus cuts ties with software-centric thread ownership and shards its allocation arenas explicitly by the **Physical CPU Core ID** using `sched_getcpu()`.
```text
[ Traditional TLS Allocator ]
Thread Moves Core 0 -> Core 1  ==>  TLS Cache logical migration
Physical memory lines remain on Core 0
Interconnect Stalls / Cross-Core Cache Snooping

[ Proteus Core-Sharded Engine ]
Thread Moves Core 0 -> Core 1  ==>  Next allocation calls sched_getcpu() -> Core 1
Instantly drops into Core 1 Local Arena
Allocates from native, core-local L1/L2 cache lines
```
When a software thread migrates from Core 0 to Core 1, Proteus adapts instantly on its very next allocation call. The entry gate queries `sched_getcpu()`, registers the hardware transition, and drops the thread directly into Core 1's native arena. 

Instead of pulling remote memory blocks across the interconnect from its old home, the thread pulls blocks local to its new hardware home, immediately populating Core 1's private L1/L2 caches. If the thread must free a block it created back on Core 0, it uses a deterministic bitmask operation (`ptr & ~4GB_MASK`) to access the managing 4GB Superpage header and safely execute the free path under Core 0's lock boundary. 

While the cross-core free path requires an atomic lock step, the **hot allocation path remains completely local to the executing hardware domain**, insulating tail latencies from scheduling fluctuations.

---

## 4. NUMA and Socket Scaling Invariants

On multi-socket servers or high-core-count processors featuring distinct Non-Uniform Memory Access (NUMA) domains, crossing a socket boundary introduces an extreme latency penalty (often **150 to 250+ ns** per access) due to traveling over inter-socket links like AMD Infinity Fabric or Intel UPI.

Traditional allocators can suffer from **cross-socket leakage** under high thread migration. If a thread hopped to a new socket, its TLS cache could continue to distribute memory addresses backed by physical RAM nodes anchored to the previous socket, turning every subsequent application read/write into an expensive cross-socket transaction.

Because Proteus binds its 4GB Superpage arenas directly to physical core IDs via `sched_getcpu()`, it is inherently topology-aware:
* Cores residing on Socket 0 allocate strictly from memory pages mapped close to Socket 0.
* Cores residing on Socket 1 allocate strictly from memory pages mapped close to Socket 1.

Even if the operating system forces a thread to jump across physical sockets, Proteus instantly aligns the thread's allocations to the new socket's local physical RAM nodes. This guarantees that your data structures maintain tight NUMA locality, preserving uniform system performance as your parallel execution scales out.

---

## 5. Architectural Locality Matrix

| Performance Vector | Traditional TLS Allocators <br> *(`mimalloc`, `jemalloc`)* | Proteus Memory Engine <br> *(Hardware-Sharded)* |
| :--- | :--- | :--- |
| **Fast-Path Structural Anchor** | The **Software Thread** (Highly fluid, ephemeral). | The **Physical CPU Core** (Fixed, hardware-bounded). |
| **Mixed-Size Allocation Order** | **Scattered.** Divergent size classes isolate sequential operations into separate physical slabs. | **Clustered.** Varying object dimensions are carved end-to-end out of a continuous physical canvas. |
| **Post-Core-Migration Allocation** | Pulls remote memory indices across the internal interconnect; triggers cross-core cache line hits. | Instantly binds to the new destination core's arena; maximizes local L1/L2 hits. |
| **Cache Line Utilization** | Low density for compound graphs. High density only when allocating identical sizes. | Exceptionally high density. Encourages automatic hardware spatial prefetching. |
| **NUMA Isolation Boundary** | Vulnerable to cross-socket leakage if thread-caches remain populated across migrations. | Bound structurally to core topology, ensuring socket-local physical allocations. |
