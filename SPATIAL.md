# Spatial Complexity, Metadata Overhead, and Fragmentation Analysis

This document provides a rigorous analysis of the spatial footprint, memory layout layout design, and fragmentation tolerances of the **Proteus Memory Engine**. It contrasts Proteus's virtual memory shortcuts against traditional size-class segregated allocators (`mimalloc`, `jemalloc`, `tcmalloc`) and outlines how the engine achieves an $O(1)$ metadata overhead invariant even under worst-case "Swiss Cheese" external fragmentation.

---

## 1. Tail Percentiles in Spatial Complexity

While latency percentiles (P99, P99.9) measure transient transaction timings, spatial percentiles track **cumulative system capacity bounds over time**. 

Proteus applies percentile frameworks to spatial boundaries through two lenses:

1. **Temporal Peak Footprint (Capacity Allocation):** A P99.9 spatial complexity boundary of $X$ Gigabytes indicates that across the entire execution timeline of a process (e.g., a multi-day server lifecycle), the Resident Set Size (RSS) remains below $X$ GB for 99.9% of the runtime. The remaining 0.1% represents transient, high-contention memory spikes. Physical machine RAM must be provisioned to match this peak to guarantee that the operating system's Out-Of-Memory (OOM) killer never flags the runtime.
2. **Request Size Distribution Profiles:** Analyzing allocation requests by percentile allows Proteus to orient its layout boundaries. For instance, if an LLM inference framework features a P95 request size of $\le 48$ bytes, Proteus designs its Tier 1 and Tier 2 fast-path slots to cover this exact threshold, ensuring 95% of operations never pay a tree-traversal tax.

### The Stateful Drag of Spatial Tails
Unlike latency penalties—which vanish the moment a heavy loop completes—spatial fragmentation penalties persist. If an allocator's layout traps memory inside dead spaces, that memory remains locked in user-space, driving up the application's base physical footprint until surrounding blocks are explicitly returned. 

---

## 2. Proteus Layout Footprint: VMS vs. RSS

Proteus utilizes a definitive architectural trade-off: it expands **Virtual Memory Space (VMS)** to keep **Resident Set Size (RSS)** and physical metadata tracking exceptionally lean.

### A. Virtual Memory Space (VMS)
The virtual memory complexity of Proteus scales strictly with the number of physical hardware CPU cores ($C$) configured in the execution fabric:

$$\text{VMS Complexity} = O(C \times \text{PT\_SUPER\_PAGE\_BYTES}) = O(C \times 4\text{ GB})$$

On a 16-core processor, Proteus immediately maps $16 \times 4\text{ GB} = 64\text{ GB}$ of virtual address layout. 

On modern 64-bit operating systems, virtual address space is practically infinite and costs zero hardware resources. The kernel's virtual memory manager (VMM) does not back virtual allocations with real physical RAM pages until those specific page addresses are actively written to by user space.

### B. Resident Set Size (RSS)
RSS represents the actual physical RAM committed to the process. Proteus keeps its baseline RSS tight by avoiding size-class sub-allocators. It allocates large chunks from the core arena only when requested, ensuring that physical RAM tracking mirrors the exact active payload density demanded by the application layer.

---

## 3. Zero-Overhead Inline Metadata Architecture

Traditional allocators require external tracking matrices (Page Tables, Segment Descriptors, Slab headers, and Thread Cache lists) to locate free blocks. When memory breaks into thousands of tiny gaps, these tracking arrays bloat linearly with fragmentation, consuming megabytes of physical RAM just to manage free-space records.

Proteus completely bypasses external tracking bloat by embedding its metadata **inline** inside the blocks themselves.

### Allocated Block Layout
When a block is actively held by the application, its permanent structural overhead is exactly **2 words (16 bytes)**, consisting of a boundary-tag header and footer:
```text
+-------------------+-----------------------------------+-------------------+
| Header (1 word)   | Active Application Payload        | Footer (1 word)   |
| [Size | State]    | (Writable memory canvas)          | [Size | State]    |
+-------------------+-----------------------------------+-------------------+
```
### Free Block Layout (The Embedded Index Invariant)
When a block is freed and indexed inside the Tier 3 Augmented Red-Black Tree, it requires multiple tracking variables (`left`, `right`, `parent`, `color`, `left_max`, `right_max`). 

Proteus avoids allocating extra memory to hold these nodes by writing the entire tree node structure **directly inside the dead space of the free block's payload area**:
```text
+-------------------+-----------------------------------+-------------------+
| Header (1 word)   | [left]   [right]    [parent]      | Footer (1 word)   |
| [Size | State]    | [color]  [left_max] [right_max]   | [Size | State]    |
+-------------------+-----------------------------------+-------------------+
```
Because the indexing map uses the exact space abandoned by the application, **the spatial overhead of the structural tree tracking layer is exactly 0 bytes.** The absolute permanent metadata footprint of the engine remains fixed ($O(1)$) relative to the total block count, regardless of whether those blocks are free or allocated:

$$\text{Total Metadata Footprint} = N_{\text{blocks}} \times 16 \text{ bytes}$$

---

## 4. The Worst-Case "Swiss Cheese" Breakdown

A "Swiss Cheese" state (extreme external fragmentation) occurs when memory is punctured by millions of non-contiguous free holes separated by isolated, active allocations. The total volume of free memory is high, but it is split into pieces.o```text
Swiss Cheese Canvas:
[Allocated][ Free Hole ][Allocated][ Free Hole ][Allocated][ Free Hole ]
```
Under this scenario, the behavioral divergence between traditional size-class allocators and Proteus is stark:

### A. Traditional Allocators: The Spatial Bloat Trap
Thread-centric allocators achieve high median speeds by dedicating entire **Slabs** (64KB to 4MB regions) exclusively to a single size class (e.g., a slab entirely carved into 512-byte slots).

* **The Failure Mode:** If an application frees 255 out of 256 items inside a 512-byte slab, that slab remains structurally locked to the 512-byte size class. The empty space represents roughly 130 KB of free memory holes. If the application suddenly issues a request for a single 520-byte object, the allocator *cannot* touch those holes because they are smaller than the target size tier.
* **The Spatial Penalty:** To fulfill the request, the allocator must completely abandon the existing holes, request a brand-new page from the operating system via `mmap`, and format a new slab. In a chaotic environment, megabytes of physical RAM become trapped inside under-utilized slabs, causing the P99.9 spatial footprint to explode linearly ($O(N_{\text{Slabs}})$) relative to size-class variety.

### B. Proteus Engine: The Embedded Tree Time Tax
Proteus prohibits size-class sharding for any block exceeding 48 bytes. Every single hole is cataloged by its raw physical memory address and sorted directly into the Tier 3 tree.

* **Immediate Coalescing Invariant:** The moment a block is freed, Proteus checks its address boundaries. If a neighbor is free, they instantly merge. In a worst-case Swiss Cheese state, every free hole is trapped between two active allocations, preventing any immediate merging.
* **The Spatial Solution:** Because Proteus handles raw addresses rather than locked size categories, it suffers zero slab-trapping bloat. If a request for a 4-word block comes in, and the tree locates a 4-word hole, it slices it out instantly, keeping physical memory utilization near 100%. The metadata tracking structure remains embedded within the holes themselves, preserving the $O(1)$ space invariant.
* **The Time Penalty:** Because the Swiss Cheese state forces $N$ completely isolated holes to exist simultaneously, the Tier 3 Red-Black tree swells to its maximum possible depth. Finding or validating a free block requires navigating the full height of the tree.
* **Pruned Search Traversal:** Proteus minimizes this search tax by checking the root node's augmented `left_max` and `right_max` metrics. If the requested size is larger than both subtree maximums, the search path is pruned immediately, and Proteus falls back to expanding the arena boundary in $O(1)$ steps. If a valid hole does exist, the traversal path to extract it is strictly bounded by tree height:
  $$\text{Search Steps} \le 2 \log_2(N + 1)$$

Proteus trades away short-term transactional speed under high fragmentation to guarantee that your spatial footprint never balloons, fixing your worst-case tail latency to a uniform ceiling of roughly **150–170 ns**.

---

## 5. Spatial Efficiency Comparison Matrix

| Layout Metric | Thread-Cached Allocators <br> *(`mimalloc`, `jemalloc`, `tcmalloc`)* | Proteus Memory Engine <br> *(Hardware-Sharded / Tiered Tree)* |
| :--- | :--- | :--- |
| **Virtual Memory (VMS)** | Bounded closely to active execution scale ($O(\text{Active RAM})$). | Fixed upfront based on core count ($O(C \times 4\text{ GB})$). |
| **Physical Memory (RSS)** | Subject to sudden spikes due to thread-cache retention and slab locking. | Highly stable. Tied directly to active payload usage and immediate coalescing. |
| **Metadata Scaling** | Linear ($O(N)$) expansion as slabs, runs, and thread-local bins multiply. | Perfect Invariant ($O(1)$). Structural tracking vectors are inline or embedded. |
| **Worst-Case Swiss Cheese Response** | **Spatial Collapse (P99.9 Space Peak):** High physical memory waste due to isolated size-class pools. | **Latency Stabilization (P99.9 Time Peak):** 0-byte space waste; operations consistently hit the $O(\log N)$ ceiling. |
| **Memory Reclamation** | Lazy or deferred. Relies on background flushes or synchronous kernel `madvise` sweeps. | Immediate. Address-ordered neighbor merging occurs synchronously on every `free()` call. |
