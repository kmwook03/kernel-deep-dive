# 05. Memory Subsystem: Page-Fault Policy and Large-Page Evaluation (THP & userfaultfd)

## ❓ Background and Problem Definition

### 1. Large-Memory Workloads and 4 KB Paging

AI inference engines, in-memory databases, and large-scale data-processing systems use virtual address spaces ranging from gigabytes to terabytes. Linux commonly uses a base page size of 4 KB.

A 4 KB page is a general-purpose unit that limits memory waste and internal fragmentation, but it also restricts TLB coverage in large-memory workloads.

### 2. TLB Pressure and MMU Overhead

When translating a virtual address to a physical address, the CPU first checks the translation cached in the TLB (Translation Lookaside Buffer). TLB coverage is limited by page size and entry count. For example, if 1,500 entries each map a 4 KB page, the coverage is $1500 \times 4\text{ KB} \approx 6\text{ MB}$. Actual capacity and hierarchy vary by CPU microarchitecture.

Repeated page-granularity traversal over a wide address range can increase TLB replacement. On a TLB miss, the MMU (Memory Management Unit) performs a page-table walk, increasing memory-access latency and CPU-cycle consumption.

### 3. First-Access Cost of Demand Paging

An operating system using demand paging does not necessarily map physical pages when a process first requests virtual memory. On first access, it handles a synchronous page-fault exception and allocates a page. This policy consumes physical memory only when needed, but adds page-table setup and page-initialization costs to the first-access path. Prefaulting or explicit memory-policy control may therefore be useful for workloads sensitive to tail latency.

## 📌 Objective

This experiment quantifies page-fault processing costs and dTLB misses during large-memory allocation and access. It then measures the effects of Transparent Huge Pages (THP) and implements a lazy-allocation path in which `userfaultfd` delegates page-fault resolution policy to user space.

## 🛠️ Test Environment & Target Workload

* **Hardware:** Raspberry Pi 5 Model B Rev 1.0
* **OS / Kernel:** Ubuntu Server 24.04.4 LTS, Linux `6.8.0-1047-raspi` (`aarch64`)
* **Observability Tool:** Linux `perf` (Performance Counters API)
* **Target Workload:** A C program that allocates a 1 GB region and accesses it at 4 KB intervals to induce page faults and dTLB misses

## 🧪 Experiment Design

1. **Phase 1 (Baseline):** Access a 1 GB region with a 4 KB stride and measure dTLB misses and page faults using `perf`.
2. **Phase 2 (THP):** Apply 2 MB alignment with `posix_memalign()`, request THP with `madvise(MADV_HUGEPAGE)`, and repeat the workload.
3. **Phase 3 (`userfaultfd`):** Register a memory region with `userfaultfd`; a worker detects faults from the main thread and supplies data through `UFFDIO_COPY`.

## 📊 1. Baseline: Inducing and Measuring TLB Pressure

The Baseline workload (`workload.c`) allocates an aligned 1 GB region with `posix_memalign()` and writes one byte every 4 KB. The first-write pattern triggers lazy allocation of base pages, while subsequent traversal over the wide address range pressures the dTLB.

```text
 Performance counter stats for './workload':

        1197721123      dTLB-loads
          23321474      dTLB-load-misses                 #    1.95% of all dTLB cache accesses
            262194      page-faults

       2.496188984 seconds time elapsed

       1.681575000 seconds user
       0.807835000 seconds sys
```

The theoretical number of pages touched by first writes over 1 GB at 4 KB intervals is $1\text{ GB} / 4\text{ KB} = 262,144$, which closely matches the 262,194 measured page faults. The run records 23,321,474 dTLB load misses and 0.808 seconds of `sys` time. These results show that lazy allocation and wide-range traversal impose costs on kernel memory management and the TLB.

## 📊 2. Transparent Huge Pages

The THP variant aligns the region start to 2 MB and requests huge-page backing with `madvise(MADV_HUGEPAGE)`. `MADV_HUGEPAGE` is a hint and does not guarantee that the kernel will back the entire region with 2 MB pages.

```text
 Performance counter stats for './workload_thp':

         275499081      dTLB-loads
           3414722      dTLB-load-misses                 #    1.24% of all dTLB cache accesses
             22536      page-faults

       2.407181747 seconds time elapsed

       1.795903000 seconds user
       0.609288000 seconds sys
```

| Metric | Baseline (4 KB) | THP Requested (2 MB) | Change |
| :--- | ---: | ---: | ---: |
| Page Faults | 262,194 | 22,536 | 91.4% decrease |
| dTLB Misses | 23,321,474 | 3,414,722 | 85.3% decrease |
| `sys` Time | 0.807 s | 0.609 s | 24.5% decrease |

If the entire 1 GB region were backed by 2 MB pages, 512 pages would be required. The measured 22,536 page faults indicate that the whole region was not immediately backed by 2 MB pages. Fragmentation and 4 KB fallback may contribute, but the actual backing ratio must be verified through `AnonHugePages` in `/proc/<pid>/smaps` or kernel THP statistics.

After the THP request, dTLB load misses decrease by 85.3%, from 23,321,474 to 3,414,722, while `sys` time decreases by 24.5%, from 0.808 seconds to 0.609 seconds. Wall-clock time decreases by approximately 3.6%, from 2.496 seconds to 2.407 seconds. Large improvements in TLB and kernel metrics therefore do not translate into equal end-to-end gains.

## 📊 3. Lazy Allocation with `userfaultfd`

The `workload_uffd.c` program lets user space control how kernel-detected page faults are resolved. It opens a `userfaultfd`, creates an unbacked virtual region with `mmap()`, and registers that region with `UFFDIO_REGISTER`.

When the main thread reads an address that is not yet backed, such as `0xffffb13b4000`, the kernel reports the fault through the `userfaultfd` descriptor and blocks the faulting thread.

A worker thread monitoring the descriptor with `poll()` reads the event and copies user-defined data (`'A'`) into the faulting page with `UFFDIO_COPY`. Once the operation succeeds, the blocked main thread resumes. This path does not bypass the page fault itself: the kernel mediates the event, while user space determines the backing content and resolution time.

## 💡 Conclusion

The 4 KB Baseline records 262,194 page faults and 23,321,474 dTLB load misses. Requesting THP decreases these metrics by 91.4% and 85.3%, respectively; `sys` time decreases by 24.5%, while wall-clock time decreases by 3.6%.

`madvise(MADV_HUGEPAGE)` can improve TLB coverage and reduce fault-processing costs. `userfaultfd` enables application-defined page-supply policies for use cases such as data injection, migration, and snapshots. Operational evaluation still requires measurement of the actual THP backing ratio and the per-fault latency and throughput of the `userfaultfd` path.

<details>
<summary><b>Terminal Output</b></summary>
<div markdown="1">

```text
[Main] Reading address 0xffffb13b4000...
[Worker] Ready to handle Page Faults...
[Worker] Page Fault detected at 0xffffb13b4000! Fetching data...
[Worker] Page filled and Main Thread awakened.
[Main] Data read success: 'A'

[Main] Reading address 0xffffb13b5000...
[Worker] Page Fault detected at 0xffffb13b5000! Fetching data...
[Worker] Page filled and Main Thread awakened.
[Main] Data read success: 'A'

[Main] Reading address 0xffffb13b6000...
[Worker] Page Fault detected at 0xffffb13b6000! Fetching data...
[Worker] Page filled and Main Thread awakened.
[Main] Data read success: 'A'

[Main] Reading address 0xffffb13b7000...
[Worker] Page Fault detected at 0xffffb13b7000! Fetching data...
[Worker] Page filled and Main Thread awakened.
[Main] Data read success: 'A'
```

</div>
</details>
