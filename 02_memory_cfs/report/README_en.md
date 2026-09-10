# 02. Performance: CFS Scheduler & Page Fault Analysis

## 📌 Objective
This experiment traces the kernel-level causes of tail latency that occurs even when aggregate CPU and memory utilization remain below 100%.

It uses eBPF to observe microsecond-scale run-queue latency and page-fault processing latency that aggregate tools such as `top` and `htop` may obscure. It also evaluates how CPU affinity and nice values affect scheduling.

## 🛠️ Test Environment & Target Workload
* **Target Workload:** A C program (`stress_test.c`) with eight threads, each allocating 512 MB and performing randomized accesses to induce page faults and CPU contention.
* **Observability Tools:** `bpftrace` probes on the `sched_switch` tracepoint and the `handle_mm_fault` kprobe.

## 📊 1. CFS Scheduler Analysis (Runqueue Latency)
The measurement spans from entry into the runnable queue until CPU dispatch.
* **Fast Path:** Most observations fall in the 0–32 μs range.
* **Tail Latency:** Under contention, some threads wait for 16–32 ms. The tail is interpreted as the combined effect of runnable-task contention and the CFS fairness policy.

## 📊 2. Memory Subsystem Analysis (Page Fault Latency)
The execution time of `handle_mm_fault` exhibits a bimodal distribution concentrated below 1 μs and between 0.5 and 8 ms.
* **Short Path (under 1 μs):** Most fast fault handling falls in this range.
* **Long Path (0.5–8 ms):** Under memory pressure, some fault-processing times rise to the millisecond scale. Because the current probe measures only `handle_mm_fault` duration, it cannot independently attribute this range to major faults, swap, or compaction.

## 💡 Conclusion
The results show that scheduler queue state and page-fault handling contribute to response-time tails in addition to application code.

Latency-sensitive workloads should separately evaluate memory pooling, prefaulting, and CPU affinity to control page faults and scheduling contention.

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
The 16–32 ms tail is hypothesized to arise when runnable threads compete under the CFS fairness policy.

Unlike general-purpose fairness, some real-time control tasks require deadlines for specific tasks. A priority-differentiation experiment models this requirement.

In `stress_test2.c`, only Thread 0 is configured with `SCHED_FIFO` or `SCHED_RR`, after which the workload is remeasured.

### 💥 Encountered Issues
```bash
kmwook@kmwookgram:~/kernel-deep-dive/02_memory_cfs$ sudo ./workload/stress_test2
=== CFS vs SCHED_FIFO Scheduling Test ===
Thread 0 (RT) failed to create - sudo permission required: Success
```

#### Problem 1. Simultaneous output of `failed` and `Success`
`perror()` formats the global `errno` value. POSIX thread functions, however, generally return an error number directly instead of setting `errno`.

Because `errno` remained zero, the failure message was followed by `Success`. The returned error number should instead be passed to `strerror()`.

#### Problem 2. Permission Denied (EPERM) despite using `sudo`
WSL2 and some container environments may reject `SCHED_FIFO` or `SCHED_RR` because of RT-bandwidth settings or capability restrictions. In this environment, the call returned `EPERM` even with administrator privileges.

Such restrictions reduce the risk that an RT busy loop in a guest or container will starve the host CPU.

### 💡 Workaround Strategy: Extreme Priority (Nice) Manipulation within CFS
Because the real-time policies could not be applied, the experiment was narrowed to nice-value differentiation within CFS. This is not a substitute for real-time scheduling; it evaluates the effect of CFS weights.

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


Thread 0 was expected to finish first, but the observed completion order differed. The result is analyzed as follows.

#### The Multi-core Trap
Scheduler priority has its clearest effect when multiple runnable threads compete for the same CPU.

The test host is a 2022 LG gram 360 with an Intel Core i5-1135G7 and eight logical CPUs visible to the operating system.

With eight threads on eight logical CPUs, threads can execute concurrently on separate CPUs, reducing contention in any single run queue. This likely obscured the effect of nice values on completion order.

### 📊 4. Scheduling Control Tuning Result 2 (Single-core)

#### CPU Affinity (`taskset -c 0`)
All threads are restricted to one CPU to expose competition within a single run queue.

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

The eBPF histogram extends to the `[1M, 2M)` bucket. When nice -20 and nice 19 threads compete on one CPU, some lower-weight threads can wait for more than one second.

## ❓Additional Experiment: Observing CFS Behavior on a Single Core
To remove the reduction in contention caused by multiple CPUs, all threads are pinned to one CPU with `taskset -c 0` and remeasured under the same CFS policy.

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

In the earlier extreme nice-value experiment, normal-thread wait times reached the 1–2 s range. With equal nice values on one CPU, the largest observed bucket is 32–64 ms.

Fairness reduces skew in waiting time, whereas real-time scheduling targets deadline guarantees for selected tasks. These are distinct objectives.

For general-purpose workloads, CFS balances throughput and interactivity through fairness. Hard real-time systems instead require worst-case response guarantees, which CFS fairness alone cannot provide. Such systems must combine appropriate real-time policies with CPU isolation, priority-inversion handling, and worst-case execution-time analysis.

## 💡 Final Conclusion

The eBPF observations of CFS and page-fault paths, together with CPU-affinity and nice-value changes, support the following conclusions:

1. **The Importance of Microsecond-Level Observability**

   Aggregate utilization from `top` or `htop` cannot identify microsecond-to-millisecond tail latency. eBPF event tracing separates the distributions of scheduler waits and fault-handling times.

2. **The Duality of OS Resource Management Across Domains**

   CFS fairness and deadline guarantees in real-time scheduling serve different design goals. Scheduling policy should therefore be selected according to domain requirements such as general-purpose throughput, interactivity, and worst-case response time.

3. **Organic Understanding of Hardware Architecture and the Kernel**

   When the number of logical CPUs equals the number of runnable threads, reduced contention can obscure the effect of nice values. Pinning the workload with `taskset` exposed the priority effect but also produced waits longer than one second for lower-weight threads. CPU affinity and priority therefore cannot be interpreted independently.

**Next Step:** The next phase evaluates XDP filtering before packets traverse the `sk_buff`-based upper network stack.
