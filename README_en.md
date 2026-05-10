# 🐧 Linux Kernel Deep Dive
A personal portfolio documenting my journey to understand and optimize the Linux kernel, with a focus on the stability and real-time requirements of mission-critical environments (e.g., defense systems, AI infrastructure).

Through this ongoing project, I aim to study how to identify system bottlenecks in practice, apply low-overhead observability tools, and write more resource-efficient system software.

## 🛠️ Tech Stack
* **Language**
  <br>![C](https://img.shields.io/badge/C-00599C?style=for-the-badge&logo=c&logoColor=white) ![Rust](https://img.shields.io/badge/Rust-000000?style=for-the-badge&logo=rust&logoColor=white) *(Planned expansion to Rust-based system programming)*
* **Kernel & OS**
  <br>![Linux](https://img.shields.io/badge/Linux_Kernel-FCC624?style=for-the-badge&logo=linux&logoColor=black) ![WSL2](https://img.shields.io/badge/WSL2_(5.15+)-0078D6?style=for-the-badge&logo=windows&logoColor=white) ![Raspberry Pi](https://img.shields.io/badge/Raspberry_Pi_Native-A22846?style=for-the-badge&logo=Raspberry%20Pi&logoColor=white)
* **Observability & Network**
  <br>![eBPF](https://img.shields.io/badge/eBPF-4479A1?style=for-the-badge&logo=linux&logoColor=white) ![XDP](https://img.shields.io/badge/XDP-E34F26?style=for-the-badge&logo=linux&logoColor=white) *(CO-RE, BCC, libbpf / eXpress Data Path)*
* **AI Pair Programming**
  <br>![Gemini](https://img.shields.io/badge/Google_Gemini-8E75B2?style=for-the-badge&logo=googlegemini&logoColor=white) *(Hypothesis setting, validation, and kernel architecture mentorship)*

## 📌 Architecture & Environment Note
Initial kernel observability and scheduler analysis (Steps 1 & 2) are conducted in a WSL2 environment with BTF (BPF Type Format) enabled, utilizing the CO-RE (Compile Once – Run Everywhere) mechanism.

For subsequent hardware-level network buffer control (XDP) and interrupt handling (Steps 3 & 4), the target environment will be migrated to Native Linux (e.g., Raspberry Pi).

---

### ✅ [STEP 1] Observability: ptrace vs. eBPF (Context Switch Overhead Analysis)
* **Status:** Completed
* **Directory:** [`/01_observability_ebpf`](./01_observability_ebpf/)
* **Summary:** 
  Measured the user-kernel space Context Switch overhead caused by traditional tracing tools (`strace`) and verified the low-overhead characteristics of `eBPF` in practice. 
  Under a stress workload repeating container isolation (`clone`) 10,000 times, `strace` consumed approximately **2.89 seconds** of kernel CPU time (`sys`). In contrast, `eBPF`, which is JIT-compiled and executed directly within the kernel, consumed only **1.40 seconds**. This hands-on experiment deepened my understanding of how to monitor systems without degrading application performance.

### ✅ [STEP 2] Performance: CFS Scheduler & Page Fault Analysis (Memory Subsystem)
* **Status:** Completed
* **Directory:** [`/02_memory_cfs`](./02_memory_cfs/)
* **Summary:** Analyzed the root causes of microsecond-level tail latency (CFS scheduling contention and Page Fault bottlenecks) at the kernel level using eBPF, and successfully demonstrated extreme scheduling conditions (CPU Starvation) through CPU Affinity and priority (Nice) tuning.

### ✅ [STEP 3] Network: Zero-copy Firewall using XDP (sk_buff Allocation Bypass)
* **Status:** Completed
* **Directory:** [`/03_network_xdp`](./03_network_xdp/)
* **Summary:** Analyzed the structural bottlenecks caused by the traditional Linux network stack (`sk_buff` allocation) and implemented a high-speed Zero-copy firewall. By leveraging eBPF/XDP to instantly drop malicious UDP packets at the NIC driver level (OS Bypass), the defense throughput was boosted by more than 2x.

### ⏳ [STEP 4] Device Driver: Interrupt Handling Mechanism (Top & Bottom Half)
* **Status:** Planned
* **Directory:** `/04_driver_interrupt`
* **Goal:** Design a fail-safe device driver architecture using Workqueues for deferred (Bottom Half) asynchronous processing, preventing kernel panics caused by interrupt storms during hardware control.
