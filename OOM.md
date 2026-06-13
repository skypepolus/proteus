# Production OOM Analysis and Container Resource Predictability

This document details the operational mechanics of fragmentation-induced Out-Of-Memory (OOM) failures in modern cloud-native infrastructures. It explores why traditional size-class segregated allocators (`mimalloc`, `jemalloc`, `tcmalloc`) run into resource boundaries under chaotic workloads like LLM inference engines, and outlines how the **Proteus Memory Engine** establishes structural immunity against silent memory footprint growth.

---

## 1. The Modern OOM Landscape: The Cgroup Isolation Trap

In legacy bare-metal server environments, an allocator that suffered from external fragmentation typically only degraded performance. If an application bloated due to trapped memory gaps, it could draw from vast pools of unallocated physical RAM. 

In modern cloud-native architectures (Kubernetes, Docker, Serverless platforms), the rules of survival have fundamentally changed:

* **The Hard Ceiling:** Applications do not interact directly with raw host hardware. They are packed tightly into containers governed by Linux **control groups (cgroups)** via explicit resource limits (e.g., `limits.memory: 16Gi`).
* **The Immediate Termination:** When an allocator's Resident Set Size (RSS) breaches the cgroup limit by even a single byte, the Linux kernel does not return `NULL` to a `malloc` call, nor does it allow the application to handle a graceful exception. The kernel's OOM killer instantly broadcasts a `SIGKILL` signal, terminating the process mid-execution.
* **Exit Code 137 (`OOMKilled`):** This manifestation represents a major vector for sudden production failures. Because traditional allocators optimize for median speed by silently caching memory within local threads and size-specific slabs, their real-world RSS footprint grows unpredictably over time until it hits the cgroup barrier.

---

## 2. Modern AI and Streaming Workloads as "Slab Killers"

Traditional web backends (e.g., CRUD REST APIs) handle highly uniform, short-lived workloads. They allocate a few kilobytes for an execution frame, resolve it in milliseconds, and discard it. Size-class segregated allocators excel in this environment because size demands are predictable and local bins rarely overflow.

Modern workloads—specifically **LLM Inference Engines (vLLM, TensorRT-LLM)** and **Distributed Streaming Networks (Kafka pipelines, Vector Databases)**—present a completely different allocation profile:

1. **Continuous Batching Fabrics:** Request lifecycles are non-uniform. An LLM engine continually inserts new tokens and tracking sequences into an active processing batch while older requests complete and drop out asynchronously.
2. **Chaotic Size Distributions:** Allocation payloads are completely unpredictable, ranging from small token IDs (8 bytes) to massive attention block matrices (megabytes) that expand dynamically based on prompt length and context complexity.
3. **Long-Lived State:** Context windows and Key-Value (KV) caches remain pinned in host memory across long multi-turn conversations, preventing simple block recycling.

This combination acts as a "slab killer" for size-class allocators. Because memory regions are locked into rigid size tiers (e.g., a slab partitioned exclusively for 512-byte slots), interleaved allocations and deallocations leave holes that cannot be repurposed for adjacent size demands (e.g., a 520-byte slot request). The allocator is forced to bypass these internal holes, request new physical memory pages from the kernel via `mmap`, and format brand-new slabs—rapidly driving the RSS footprint toward the container ceiling.

---

## 3. The Chronology of "Silent Creep"

External fragmentation is rarely a sudden explosion; it operates as a slow-burn degradation. A microservice container may appear completely stable during a 10-minute automated integration or staging test. However, in production environments, containers are expected to maintain an uninterrupted lifecycle for weeks or months.

Over an extended execution timeline, variations in traffic routing cause different thread-local storage (TLS) caches to expand and capture memory regions. Because traditional engines use lazy, deferred, or batch-driven reclamation to maintain sub-20ns median speeds, they rarely initiate aggressive, global sweeping passes to consolidate these trapped slabs.

After days of continuous processing, the cumulative volume of un-coalesced, size-locked slots builds up. The process footprint eventually collides with the hard cgroup limit, causing the container to crash silently. These failures often leave no application stack trace or internal error log behind because the kernel enforces termination instantly from the host level.

---

## 4. Industry Band-Aids vs. The Proteus Paradigm

To prevent `OOMKilled` disruptions, the software industry currently relies on three highly inefficient operational workarounds:

| Industry Workaround | Operational Penalty | The Proteus Alternative |
| :--- | :--- | :--- |
| **Over-Provisioning <br> (The "Mempool Tax")** | SRE teams routinely assign containers double the RAM required by actual data payloads just to serve as a buffer for allocator fragmentation, wasting significant cloud infrastructure spend. | **Strict Spatial Predictability:** By enforcing immediate, address-ordered neighbor coalescing, physical RAM mirrors active data density, allowing precise container sizing. |
| **Automated Scheduled Restarts** | Enterprise platforms implement cron-driven orchestration jobs to forcefully cycle production containers every 24 to 48 hours, clearing out fragmented slabs before they trigger an OOM. | **Unbounded Operational Lifecycles:** Memory is continuously consolidated back into larger contiguous address regions, enabling pods to run indefinitely without resource drift. |
| **Compulsive Runtime/GC Tuning** | Engineering teams spend weeks adjusting internal garbage collection thresholds and environment variables, sacrificing CPU cycles to trigger more frequent memory sweeps. | **Zero Background Maintenance:** Proteus features no background garbage collection loop or deferred sweeping tasks. All reclamation occurs synchronously and predictably during `free()`. |

### The Proteus Structural Immunity

Proteus achieves a flat spatial profile by eliminating the architectural concept of size-class sharding for blocks greater than 48 bytes. Every single free block is cataloged by its raw virtual memory address and sorted directly into the Tier 3 Augmented Red-Black Tree. 

When an application calls `proteus_free`, the engine immediately performs a bitmask operation to identify the managing 4GB Superpage header, accesses the localized core arena lock, and checks the absolute boundary addresses of the left and right physical neighbors. If a neighbor is free, they are unlinked from the tree and merged into a single continuous free block **instantly**.

Furthermore, because the structural vectors required to manage the Tier 3 tree live directly inside the dead space of the free payloads themselves, the tracking metadata overhead is locked to a perfect $O(1)$ invariant of 16 bytes per block. Proteus trades away microsecond-level local caching speed to guarantee that your physical memory footprint never balloons, replacing unpredictable spatial risks with a highly structured, sub-microsecond time invariant.

## Authors & Attribution

* **Young H. Song** - *Core Architecture & Design* - [@skypepolus](https://github.com/skypepolus/proteus.git )

See also the list of contributors who participated in optimizing this project.

## License

This project is licensed under the Apache License, Version 2.0 - see the LICENSE(LICENSE) file for complete details.
