**Proteus** is a highly innovative memory allocator, written in the C11 standard, which is designed to operate independently of software threads ("thread-agnostic"). Its purpose is to solve the critical problems of fragmentation and latency instability that plague modern cloud-native infrastructures and highly demanding systems, such as LLM Inference Engines.

### Key Innovations and Architecture

* **Hardware-Centric Architecture:** Proteus abandons the traditional Thread-Local Storage (TLS) approach used by other allocators (e.g., `mimalloc`, `jemalloc`). Instead of tying memory to software threads, it defines memory arenas based on the physical cores of the processor using the `sched_getcpu()` function. This eliminates the massive delays caused when the operating system migrates threads between cores (core-hopping) and dramatically increases Data Locality.
* **OOM Protection (Zero Fragmentation):** In environments with strict memory limits (e.g., Kubernetes cgroups), the accumulation of "hidden" memory gaps is a frequent cause of system termination. Proteus addresses this problem by completely eliminating size classes for allocations larger than 48 bytes. Instead, it implements a strategy of immediate coalescing: if two adjacent blocks are freed, they are merged instantly. Thus, the actual memory footprint (Resident Set Size - RSS) always reflects the real data, offering absolute "structural immunity" against memory exhaustion.
* **Zero-Overhead Inline Metadata:** The management of free blocks is handled via an Augmented Red-Black Tree that achieves First-Fit searches in $O(\log n)$ time. Instead of allocating extra memory to store the nodes of this tree, the system "writes" the structural data (left child, right child, parent, color) *directly inside the empty space* of the already freed memory blocks (inline metadata). This keeps the spatial overhead at absolute zero, maintaining an $O(1)$ overhead constant.
* **Tail-Latency Stabilization:** Unlike traditional allocators that offer fast median speeds but suffer $O(N)$ worst-case spikes when cleaning up their local caches, Proteus strictly enforces time boundaries through its tree structure. This cuts off delays ("truncates the tail") at the P99.9 level, making it absolutely predictable under stress conditions.
* **Advanced Synchronization (Hybrid-Lock):** The project utilizes the custom `hybrid-lock` library, a cache-isolated spin-semaphore lock that features a "Self-Dampening" algorithm. When traffic increases and a thread goes into a wait state, the remaining threads stop wasting CPU cycles (spinning) and yield gracefully (OS sleep), protecting the system's hardware bus from thrashing.

### Code Quality & Engineering

The engineering level of the project is exceptionally high and designed for industrial use.

* All internal structures are aligned to 64 bytes to prevent the "False Sharing" problem in CPU caches.
* It utilizes modern C11 atomics for thread safety and performs topological pointer swaps in the tree, ensuring that physical memory data is not corrupted during node balancing.
* A complete stress and integrity testing suite is provided, with support for verifying race conditions using tools like `ThreadSanitizer` and `AddressSanitizer`.

### Conclusion

The **Proteus Elastic Memory Engine** is an extremely mature and well-crafted piece of software. It introduces radical solutions to prevent memory footprint leaks and ensures stable performance at P99.9 tail latency. It works flawlessly as a direct, drop-in replacement for traditional allocators in heavy production environments that require absolute resource management.

## Authors & Attribution

* **Young H. Song** - *Core Architecture & Design* - [@skypepolus](https://github.com/skypepolus/proteus.git )

See also the list of contributors who participated in optimizing this project.

## License

This project is licensed under the Apache License, Version 2.0 - see the LICENSE(LICENSE) file for complete details.
