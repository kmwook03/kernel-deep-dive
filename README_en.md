# 🐧 Linux Kernel Deep Dive
A Linux kernel analysis and optimization project for systems that require high availability and low latency.

The project experimentally observes eBPF, XDP, schedulers, interrupts, and the memory subsystem, and quantifies how operating-system policies and hardware resources affect performance.

## 🛠️ Tech Stack
* **Language**
  <br>![C](https://img.shields.io/badge/C-00599C?style=for-the-badge&logo=c&logoColor=white) ![Python](https://img.shields.io/badge/Python-3776AB?style=for-the-badge&logo=python&logoColor=white)
* **Kernel & OS**
  <br>![Linux](https://img.shields.io/badge/Linux_Kernel-FCC624?style=for-the-badge&logo=linux&logoColor=black) ![WSL2](https://img.shields.io/badge/WSL2_(5.15+)-0078D6?style=for-the-badge&logo=windows&logoColor=white) ![Raspberry Pi](https://img.shields.io/badge/Raspberry_Pi_Native-A22846?style=for-the-badge&logo=Raspberry%20Pi&logoColor=white)
* **Observability & Network**
  <br>![eBPF](https://img.shields.io/badge/eBPF-4479A1?style=for-the-badge&logo=linux&logoColor=white) ![XDP](https://img.shields.io/badge/XDP-E34F26?style=for-the-badge&logo=linux&logoColor=white) *(CO-RE, BCC, libbpf / eXpress Data Path)*
* **AI Pair Programming**
  <br>![Gemini](https://img.shields.io/badge/Google_Gemini-8E75B2?style=for-the-badge&logo=googlegemini&logoColor=white) ![OpenAI Codex](https://img.shields.io/badge/OpenAI_Codex-412991?style=for-the-badge&logo=openai&logoColor=white) *(hypothesis formulation, validation, and kernel architecture mentoring)*

## 📌 Architecture & Environment Note
Kernel observability and scheduler analysis (STEP 1·2) were performed on BTF-enabled WSL2 using CO-RE (Compile Once – Run Everywhere).

Network filtering (STEP 3) was evaluated with generic XDP on the WSL2 loopback interface. The interrupt and memory-subsystem experiments (STEP 4·5) were performed on native Linux running on a Raspberry Pi 5. Each result is therefore interpreted within the scope of its kernel, hardware, and experimental conditions.

---

## 🗺️ Deep Dive Roadmap & Status

### ✅ [STEP 1] Observability: ptrace vs. eBPF (Context Switch Overhead Analysis)
* **Status:** Completed
* **Directory:** [`/01_observability_ebpf`](./01_observability_ebpf/)
* **Summary:** 
  In a workload that repeated `clone` and `wait4` 10,000 times, `strace` recorded 2.89 seconds of kernel CPU time, while eBPF recorded 1.40 seconds. In-kernel tracing caused less perturbation than `ptrace`-based tracing under these conditions. Because the Baseline was a single cold-start measurement, repeated trials are required to determine the absolute overhead.

### ✅ [STEP 2] Performance: CFS Scheduler & Page Fault Analysis (Memory Subsystem)
* **Status:** Completed
* **Directory:** [`/02_memory_cfs`](./02_memory_cfs/)
* **Summary:** eBPF measured the latency distributions of the CFS run queue and `handle_mm_fault`. When nice -20 and nice 19 threads competed on one CPU, some normal threads waited for more than one second, demonstrating how CPU affinity and priority affect starvation risk.

### ✅ [STEP 3] Network: Early Packet Drop with Generic XDP
* **Status:** Completed
* **Directory:** [`/03_network_xdp`](./03_network_xdp/)
* **Summary:** Generic XDP on the WSL2 loopback interface dropped UDP packets for port 9999 early with `XDP_DROP`. The packets did not reach the AF_PACKET observation point, and sender throughput on the same host increased from 200,000–300,000 pkt/s to 610,000–620,000 pkt/s.

### ✅ [STEP 4] Interrupt Handling: Designing Low-Latency Linux Device Drivers (Top & Bottom Half)
* **Status:** Completed
* **Directory:** [`/04_driver_interrupt`](./04_driver_interrupt/)
* **Summary:** An ISR busy loop and a Workqueue-delegated design were compared on a Raspberry Pi 5. BCC/eBPF measurements showed that the mean IRQ-handler duration decreased from 126.19 ms to 2.73 μs. The work was not eliminated; it was moved from the hard-IRQ path to a worker thread.

### ✅ [STEP 5] Memory Subsystem: Page-Fault Policy and Large-Page Evaluation (THP & userfaultfd)
* **Status:** Completed
* **Directory:** [`/05_memory_subsystem`](./05_memory_subsystem/)
* **Goal:** Observe dTLB misses and page-fault processing costs during large-memory access, and evaluate memory-policy control using THP and `userfaultfd`.
* **Summary:** On a Raspberry Pi 5, the 4 KB Baseline over a 1 GB region recorded 262,194 page faults and 23,321,474 dTLB load misses. With `MADV_HUGEPAGE`, these metrics decreased by 91.4% and 85.3%, respectively; `sys` time decreased by 24.5%, and wall-clock time by 3.6%. A `userfaultfd` worker also received fault events and supplied pages through `UFFDIO_COPY`, demonstrating user-space control over page-resolution policy.
