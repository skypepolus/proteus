# Theoretical and Algorithmic Foundations of Proteus

This document establishes the mathematical models, algorithmic steps, and academic literature supporting the design of the Proteus Elastic Memory Engine.

---

## 1. Address-Ordered First-Fit Strategy

Proteus manages allocations exceeding 6 words (48 bytes) through an address-ordered, First-Fit indexing tree. 

### Theoretical Advantages
As analyzed extensively by Donald Knuth [1], the First-Fit storage allocation strategy possesses a natural mathematical tendency to preserve memory compaction. By always satisfying an allocation request from the lowest available virtual memory address that meets the size criteria, the allocator aggressively packs fragments together at the leading edge of the heap canvas. This leaves higher memory blocks untouched and contiguous, allowing them to fulfill large allocation requests later.

### Algorithmic Mechanics
When a block is released via `proteus_free`, it immediately queries its immediate physical neighbors via its boundary tags. If a neighbor is free, the blocks are merged via branchless coalescing keys. Because Proteus sorts its index strictly by the physical memory address (`(uintptr_t)node`), newly coalesced blocks are re-inserted into their exact spatial location in the hierarchy, maintaining the address-ordered compaction invariant.

---

## 2. Augmented Subtree Maximum Searches ($O(\log n)$ First-Fit)

While a naive First-Fit strategy requires a linear $O(n)$ traversal across a linked list of free chunks, Proteus implements an efficient tree-based search model based on the work of Richard P. Brent [2].

### The Structural Augmentation Invariant
Every tree node (`pt_redblack_t`) embedded at the trailing edge of a free block maintains two size-tracking properties:
* `left_max`: The absolute largest block size available anywhere within its left child subtree.
* `right_max`: The absolute largest block size available anywhere within its right child subtree.

This allows the search routine to inspect whole branches of the tree in $O(1)$ time, turning the traditional linear search into a precise, deterministic path down the tree.

### Search Path Specification
```text
Function pt_idx_tree_find_first_fit(root, size_words):
    If root is NULL or Maximum(root->left_max, root->right_max, root->size) < size_words:
        Return NULL (Mathematical proof that no block in this tree fits)

    current = root
    While current is not NULL:
        // Step 1: Prioritize lower addresses to maintain strict First-Fit compaction
        If current->left is not NULL and current->left_max >= size_words:
            current = current->left
            Continue
            
        // Step 2: Evaluate the current node's physical capacity
        If current->size >= size_words:
            Return current
            
        // Step 3: Fall back to higher addresses if left paths and current options fail
        If current->right is not NULL and current->right_max >= size_words:
            current = current->right
            Continue
            
        Break
    Return NULL
```

During splits and extractions, these augmentations are updated back up to the root node via `pt_idx_tree_update_augmentation`, maintaining tree accuracy across allocation operations.

---

## 3. Red-Black Tree Topology and Pointer-Swap Invariance

To guarantee that the tree search height remains strictly bounded to $O(\log n)$, Proteus uses the self-balancing properties defined by Leonidas J. Guibas and Robert Sedgewick [3].

### Topological Pointer-Swap Invariance
A classic challenge when embedding balanced tree node descriptors straight into free memory payloads is handling node evictions when a block has two active children. Standard textbook implementations copy or swap the values of the nodes during a deletion fixup. 

In Proteus, doing this would overwrite the block's physical size markers and corrupt the geometric vector history (`node->hdr[0]`). 

To prevent this data corruption, Proteus implements a **Topological Pointer Swap** inside `pt_idx_tree_unlink`. When unlinking a node with two children, the allocator leaves the physical memory blocks undisturbed. Instead, it re-wires the structural linkage pointers (`left`, `right`, `parent`, and `color`) of the nodes themselves. This ensures that the virtual memory history tracking stays safely anchored to its exact physical location.

---

## 4. Fragmentation Limits and the Robson Bound

The structural efficiency of dynamic storage engines is fundamentally bounded by the **Robson Bound** [4]. 

### The Mathematical Threat
J. M. Robson proved that for any allocation strategy, the worst-case memory requirement $V$ to guarantee satisfaction of a maximum active allocation volume $M$ is bounded by:

$$V \propto M \cdot \log_2\left(\frac{N_{\text{max}}}{N_{\text{min}}}\right)$$

where $N_{\text{max}}$ is the maximum requested block size and $N_{\text{min}}$ is the minimum requested block size. If an allocator allows random fragmentation across high and low addresses, it can trigger worst-case fragmentation, bloating memory usage beyond acceptable bounds.

### Proteus Defense Mechanisms
Proteus defends against the Robson Bound through two architectural choices:
1. **Aggressive Low-Tier Coalescing:** Small blocks (4 words and 6 words) are tracked via fast segregated lists. If an allocation request hits a tree block, any leftover space is aggressively split and downgraded to these segregated lists. This minimizes internal fragmentation and keeps the ratio of $N_{\text{max}} / N_{\text{min}}$ strictly controlled.
2. **Strict Address-Ordered Allocation:** By packing allocations into the lowest possible addresses, Proteus forces the virtual memory layout to remain tightly packed. This helps prevent isolated free blocks from being trapped at high memory addresses, keeping real-world fragmentation well within optimal theoretical limits.

---

## References

* **[1] Donald E. Knuth.** *The Art of Computer Programming, Volume 1: Fundamental Algorithms*. Section 2.5: "Dynamic Storage Allocation". Addison-Wesley, Third Edition.
* **[2] Richard P. Brent.** "Efficient implementation of the first-fit strategy for dynamic storage allocation." *ACM Transactions on Programming Languages and Systems (TOPLAS)*, Vol. 11, No. 3, 1989, pp. 388–403.
* **[3] Leonidas J. Guibas and Robert Sedgewick.** "A Dichromatic Framework for Balanced Trees." *Proceedings of the 19th Annual Symposium on Foundations of Computer Science (FOCS)*, IEEE Computer Society, 1978, pp. 8–21.
* **[4] J. M. Robson.** "Bounds on Storage Allocation Algorithms." *Journal of the ACM (JACM)*, Vol. 21, No. 3, 1974, pp. 491–499; and "An Estimate of the Store Required for First-Fit Allocation of Storage", *The Computer Journal*, Vol. 20, No. 3, 1977, pp. 242–244.

---

## Acknowledgements

Special acknowledgement is given to the open-source and academic operating systems research communities. The development of Proteus draws directly on the foundational concurrency models found in multiprocessor runtimes, combining classic data structure engineering with modern cache-line isolation strategies to achieve high performance under intense, multi-threaded workloads.

This architecture, along with its geometric vector virtual memory tracking invariants and zero-TLS layout synchronization model, was co-designed and rigorously optimized in collaborative engineering sessions with **Gemini**, a large language model built by Google.
