# 02. Performance: CFS Scheduler & Page Fault Analysis

## 📌 Objective
This step aims to trace the root causes of **'Tail Latency'** at the kernel level—those unexplained system delays that occur even when overall CPU or memory utilization is not at 100%. 

By observing microsecond (us) scale scheduler wait times (Runqueue Latency) and memory allocation bottlenecks (Page Faults) using eBPF—metrics that are invisible to standard monitoring tools like `top` or `htop`—I seek to learn the critical performance tuning points required for real-time and high-availability environments.

## 🛠️ Test Environment & Target Workload
* **Target Workload:** A multi-threaded C program (`stress_test.c`) that spawns 8 threads, each dynamically allocating 512MB of memory. It intentionally triggers intensive Page Faults and CPU contention via randomized memory access.
* **Observability Tools:** `bpftrace` utilizing kernel tracepoints (`sched_switch`) and kprobes (`handle_mm_fault`).

## 📊 1. CFS Scheduler Analysis (Runqueue Latency)
Measured the wait time from when a thread enters the execution queue (Runqueue) to when it actually gets scheduled onto the CPU.
* **Fast Path:** Over 95% of the scheduling events were executed at lightning speed, within 0~32us.
* **Tail Latency:** As thread contention intensified, the Completely Fair Scheduler (CFS) forced context switches to maintain fairness. During this, I observed a critical tail latency phase where threads waited up to **32ms (32,000us)** without receiving CPU time.

## 📊 2. Memory Subsystem Analysis (Page Fault Latency)
Measured the execution time of the `handle_mm_fault` function (which maps physical memory to virtual memory), revealing a clear **Bimodal Distribution**.
* **Minor Page Fault (Under 1us):** When free memory was readily available, the mapping was handled incredibly fast, taking less than 1us.
* **Major Page Fault / Compaction (0.5ms ~ 8ms):** When system memory pressure increased and the kernel had to secure physical memory (e.g., via Swapping or Memory Compaction), a massive bottleneck formed. In this zone, latency spiked by up to **4,000 times (over 4ms)** compared to the fast path.

## 💡 Engineering Conclusion
This experiment verified that system response times are not solely dictated by application-level code optimization; the **OS kernel's scheduling queues and memory fragmentation states** play a decisive role. 

Moving forward, this hands-on experience deeply reinforced the necessity of minimizing kernel intervention (e.g., avoiding Hard Page Faults and excessive Context Switches) by utilizing techniques like Memory Pooling and adjusting CPU Affinity when designing high-performance system architectures.

<details>
<summary><b>Terminal Output</b></summary>
<div markdown="1">

```text
Attaching 4 probes...
Tracing CPU Runqueue Latency ... Hit Ctrl-C to end.
^C

@qtime[104754]: 5995281526996
@qtime[104757]: 5997420670948
@qtime[104785]: 6004336033052
@qtime[104791]: 6006603588411
@runqlat_us:
[0]                 2440 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@      |
[1]                 1827 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@                  |
[2, 4)              1907 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@                |
[4, 8)              2747 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[8, 16)              941 |@@@@@@@@@@@@@@@@@                                   |
[16, 32)             573 |@@@@@@@@@@                                          |
[32, 64)              87 |@                                                   |
[64, 128)             55 |@                                                   |
[128, 256)            18 |                                                    |
[256, 512)            16 |                                                    |
[512, 1K)             13 |                                                    |
[1K, 2K)               6 |                                                    |
[2K, 4K)               7 |                                                    |
[4K, 8K)               4 |                                                    |
[8K, 16K)             47 |                                                    |
[16K, 32K)            15 |                                                    |
```

```text
Attaching 3 probes...
Tracing Page Fault Latency for 'stress_test'... Hit Ctrl-C to end.
^C

@pf_lat_us:
[0]                 1301 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[1]                  153 |@@@@@@                                              |
[2, 4)                42 |@                                                   |
[4, 8)                20 |                                                    |
[8, 16)                4 |                                                    |
[16, 32)              43 |@                                                   |
[32, 64)              45 |@                                                   |
[64, 128)             22 |                                                    |
[128, 256)             0 |                                                    |
[256, 512)             0 |                                                    |
[512, 1K)            825 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@                    |
[1K, 2K)             494 |@@@@@@@@@@@@@@@@@@@                                 |
[2K, 4K)             573 |@@@@@@@@@@@@@@@@@@@@@@                              |
[4K, 8K)             111 |@@@@                                                |
[8K, 16K)             42 |@                                                   |
[16K, 32K)             2 |                                                    |
```
</details>

## 🔧 Performance Tuning Attempts
The previously observed 32ms tail latency is believed to be caused by the Completely Fair Scheduler (CFS) attempting to distribute CPU time equally among all threads.

However, critical tasks like missile interception systems or autonomous driving braking controls do not require fairness; they require absolute, unconditional priority execution.

To investigate this, I modified the `stress_test.c` code (`stress_test2.c`) to elevate the priority of a specific thread (Thread 0) using Linux's real-time scheduling policies (`SCHED_FIFO` or `SCHED_RR`) and re-measured the load.

### 💥 Encountered Issues
```bash
kmwook@kmwookgram:~/kernel-deep-dive/02_memory_cfs$ sudo ./workload/stress_test2
=== CFS vs SCHED_FIFO Scheduling Test ===
Thread 0 (RT) failed to create - sudo permission required: Success
```

#### Problem 1. Simultaneous output of `failed` and `Success`
The `perror()` function used for error output reads the global variable `errno` and converts it to a string. However, `pthread` library functions do not set `errno` upon failure; instead, they return the error code directly as the function's return value.

Therefore, `errno` was still `0` (Success), and `perror()`, which only reads `errno`, mistakenly printed `Success`.

#### Problem 2. Permission Denied (EPERM) despite using `sudo`
To maintain host stability, WSL2 and many container environments fundamentally block the use of real-time scheduling (like `SCHED_FIFO`/`SCHED_RR`) by limiting or zeroing out the cgroup's RT bandwidth.

This is a defensive mechanism designed to prevent an RT busy loop inside the guest/container from causing CPU starvation on the host machine.

### 💡 Workaround Strategy: Extreme Priority (Nice) Manipulation within CFS
Instead of rebuilding the WSL2 kernel to bypass the cgroup restrictions, I shifted the experiment's focus to see if the VIP thread could be protected by **manipulating process priorities (Nice values) to their extremes within the allowed CFS environment.**

1. **Strategy:** Assign the highest kernel-allowed priority (Nice -20) to a specific thread (Thread 0), and the lowest priority (Nice 19) to the rest using the setpriority system call.

2. **Execution & Observation:** Run the stress test again and observe the scheduling behavior via eBPF.

### 📊 3. Scheduling Control Tuning Result 1 (Multi-core)
<details>
<summary><b>Terminal Output</b></summary>
<div markdown="1">

```text
=== CFS Extreme Priority (Nice) Test ===
[Thread 1] 🐢 Normal Thread (Nice: 19)
[Thread 0] 🚀 VIP Thread (Nice: -20)
[Thread 2] 🐢 Normal Thread (Nice: 19)
[Thread 3] 🐢 Normal Thread (Nice: 19)
[Thread 4] 🐢 Normal Thread (Nice: 19)
[Thread 5] 🐢 Normal Thread (Nice: 19)
[Thread 6] 🐢 Normal Thread (Nice: 19)
[Thread 7] 🐢 Normal Thread (Nice: 19)
[Thread 5] Job Completed
[Thread 2] Job Completed
[Thread 6] Job Completed
[Thread 7] Job Completed
[Thread 0] Job Completed
[Thread 3] Job Completed
[Thread 4] Job Completed
[Thread 1] Job Completed
=== All Tests Completed ===
```
```text
Attaching 4 probes...
Tracing CPU Runqueue Latency ... Hit Ctrl-C to end.
^C

@qtime[28521]: 5350993415703
@qtime[28554]: 5361357805086
@runqlat_us:
[0]                 6932 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[1]                 6122 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@       |
[2, 4)              3373 |@@@@@@@@@@@@@@@@@@@@@@@@@                           |
[4, 8)              2181 |@@@@@@@@@@@@@@@@                                    |
[8, 16)             1704 |@@@@@@@@@@@@                                        |
[16, 32)            1545 |@@@@@@@@@@@                                         |
[32, 64)             353 |@@                                                  |
[64, 128)            205 |@                                                   |
[128, 256)            96 |                                                    |
[256, 512)            59 |                                                    |
[512, 1K)             46 |                                                    |
[1K, 2K)              32 |                                                    |
[2K, 4K)              34 |                                                    |
[4K, 8K)              60 |                                                    |
[8K, 16K)             90 |                                                    |
[16K, 32K)             2 |                                                    |
```
</details>


I expected Thread 0 to execute and finish first, but the results did not align with my hypothesis. The cause was analyzed as follows:

#### The Multi-core Trap
Scheduler priority (nice value) is only meaningful when multiple threads compete for a single CPU core **(Contention)**.

The laptop I used for this research (LG gram 360 2022) is equipped with an `11th Gen Intel(R) Core(TM) i5-1135G7 @ 2.40GHz(2.42 GHz)` CPU.

Thanks to Intel's Hyper-Threading technology, the OS recognizes 8 logical cores. Therefore, the 8 threads were distributed almost 1-to-1 across the logical cores. Because no actual queue or contention formed, the priority scenario failed to execute as intended.

### 📊 4. Scheduling Control Tuning Result 2 (Single-core)

#### The Solution (`taskset -c 0`)
I restricted the execution to a single core using CPU Affinity.

<details>
<summary><b>Terminal Output</b></summary>
<div markdown="1">

```text
=== CFS Extreme Priority (Nice) Test ===
[Thread 6] 🐢 Normal Thread (Nice: 19)
[Thread 7] 🐢 Normal Thread (Nice: 19)
[Thread 5] 🐢 Normal Thread (Nice: 19)
[Thread 4] 🐢 Normal Thread (Nice: 19)
[Thread 3] 🐢 Normal Thread (Nice: 19)
[Thread 2] 🐢 Normal Thread (Nice: 19)
[Thread 1] 🐢 Normal Thread (Nice: 19)
[Thread 0] 🚀 VIP Thread (Nice: -20)
[Thread 0] Job Completed
[Thread 6] Job Completed
[Thread 3] Job Completed
[Thread 4] Job Completed
[Thread 5] Job Completed
[Thread 7] Job Completed
[Thread 1] Job Completed
[Thread 2] Job Completed
=== All Tests Completed ===
```
```text
Attaching 4 probes...
Tracing CPU Runqueue Latency ... Hit Ctrl-C to end.
^C

@qtime[33893]: 6421676103225
@qtime[34067]: 6440226045667
@qtime[34073]: 6442279940315
@qtime[34141]: 6454590725568
@qtime[34180]: 6466951967341
@qtime[34192]: 6471047458124
@qtime[34204]: 6475143324012
@qtime[34213]: 6479202324099
@qtime[34225]: 6483301232788
@qtime[34231]: 6485347939830
@qtime[34264]: 6495617181112
@qtime[34276]: 6499714999738
@qtime[34282]: 6501761580708
@qtime[34288]: 6503810121088
@qtime[34425]: 6514043778312
@qtime[34428]: 6514079436115
@runqlat_us:
[0]                18662 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[1]                17135 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@     |
[2, 4)              8242 |@@@@@@@@@@@@@@@@@@@@@@                              |
[4, 8)              6085 |@@@@@@@@@@@@@@@@                                    |
[8, 16)             2640 |@@@@@@@                                             |
[16, 32)            1590 |@@@@                                                |
[32, 64)             341 |                                                    |
[64, 128)            156 |                                                    |
[128, 256)           117 |                                                    |
[256, 512)            27 |                                                    |
[512, 1K)             18 |                                                    |
[1K, 2K)              14 |                                                    |
[2K, 4K)              23 |                                                    |
[4K, 8K)              20 |                                                    |
[8K, 16K)             35 |                                                    |
[16K, 32K)             3 |                                                    |
[32K, 64K)             9 |                                                    |
[64K, 128K)            2 |                                                    |
[128K, 256K)           1 |                                                    |
[256K, 512K)           0 |                                                    |
[512K, 1M)             0 |                                                    |
[1M, 2M)               1 |                                                    |
```
</details>

Looking at the eBPF histogram, unlike the previous results, the tail extended dramatically to the `[1M, 2M)` bucket. Because I tested this in a single-core environment (triggering severe contention), the lower-priority threads were pushed down the queue, starving for over 1 second without receiving any CPU allocation.

## ❓Additional Experiment: Observing CFS Behavior on a Single Core
Since the very first experiment was conducted in a multi-core environment, I ran it again under a single-core constraint to observe pure CFS behavior.

<details>
<summary><b>Terminal Output</b></summary>
<div markdown="1">

```text
Attaching 4 probes...
Tracing CPU Runqueue Latency ... Hit Ctrl-C to end.
^C

@qtime[37740]: 7172596442409
@qtime[37746]: 7174662967171
@qtime[37767]: 7180872498024
@qtime[37779]: 7184964467229
@qtime[37782]: 7186975147022
@qtime[37907]: 7193113484587
@qtime[37925]: 7199263212310
@qtime[38005]: 7213671079482
@qtime[38011]: 7215729235452
@qtime[38023]: 7219879335607
@qtime[38029]: 7221961713227
@qtime[38038]: 7226065567611
@qtime[38041]: 7226134145475
@qtime[38056]: 7232357087876
@qtime[38068]: 7236463617813
@qtime[38083]: 7240623776626
@qtime[38119]: 7252941563558
@qtime[38309]: 7269390443513
@runqlat_us:
[0]                14507 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@              |
[1]                19383 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[2, 4)              8967 |@@@@@@@@@@@@@@@@@@@@@@@@                            |
[4, 8)              6727 |@@@@@@@@@@@@@@@@@@                                  |
[8, 16)             3232 |@@@@@@@@                                            |
[16, 32)            1192 |@@@                                                 |
[32, 64)             410 |@                                                   |
[64, 128)            159 |                                                    |
[128, 256)            73 |                                                    |
[256, 512)            22 |                                                    |
[512, 1K)             12 |                                                    |
[1K, 2K)              13 |                                                    |
[2K, 4K)               7 |                                                    |
[4K, 8K)               1 |                                                    |
[8K, 16K)             16 |                                                    |
[16K, 32K)             2 |                                                    |
[32K, 64K)             2 |                                                    |
```
</details>

In the previous VIP test, normal threads suffered from CPU Starvation, waiting 1~2 seconds without execution. However, this pure CFS test histogram shows that the maximum wait time was much shorter, capped at 32ms~64ms.

While an algorithm with a shorter tail latency might generally seem "better," it strictly depends on the domain.

For general-purpose systems (web servers, desktops), CFS is overwhelmingly superior. However, in hard real-time systems (defense radars, autonomous driving, pacemakers), CFS is a dangerous choice that could threaten lives. If fairness dictates that a critical missile interception system is queued behind a file download thread, the consequences would be fatal.

## 💡 Final Conclusion & Engineering Insight

By hooking into and tuning the Linux kernel's scheduler (CFS) and memory subsystem (Page Fault) directly with eBPF, I gained the following profound insights:

1. **The Importance of Microsecond-Level Observability**

   User-space tools like `top` or `htop` can easily mask fatal internal bottlenecks, even when CPU usage appears low. To capture millisecond to microsecond-level tail latencies and CPU Starvation, kernel-level dynamic tracing technologies like eBPF are indispensable.

2. **The Duality of OS Resource Management Across Domains**

   I proved that there is no 'absolutely perfect algorithm' for scheduling. While the 'fairness' of CFS works brilliantly to prevent starvation in general environments, this very fairness induces fatal delays in domains requiring Hard Real-time capabilities, such as missile interception radar systems or real-time AI inference infrastructure. I realized the importance of having the skill to manipulate and control OS policies according to the specific purpose.

3. **Organic Understanding of Hardware Architecture and the Kernel**

   I encountered firsthand how the effects of software scheduling priority (Nice) are diluted in a multi-core environment based on Hyper-Threading. I empirically confirmed that software priority tuning must be accompanied by the physical isolation of hardware resources—using **CPU Affinity** controls like `taskset`—to unleash its true power.

🚀 **Next Step:** Having deeply understood the overhead caused by internal kernel scheduling and memory copying (Page Faults), my next phase will expand into **XDP (eXpress Data Path)-based Zero-copy Firewall Architecture**. This will involve intercepting and dropping massive incoming network traffic directly at the NIC driver level, long before it ever reaches the kernel's network stack (`sk_buff`).
