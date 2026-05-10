# 03. Networking: XDP (eXpress Data Path) Zero-copy Firewall

## 📌 Objective
This step analyzes the structural bottlenecks of the traditional Linux network stack (specifically, the overhead of `sk_buff` memory allocation and stack traversal) and verifies the performance of **XDP (eXpress Data Path)** technology, which bypasses the OS to process packets directly at the hardware (NIC) driver level.

Assuming a massive UDP Flood (DDoS) attack targeting a specific port, I implemented an ultra-high-speed firewall that immediately drops packets (`XDP_DROP`) without consuming kernel resources.

## 🛠️ Test Environment & Target Workload
* **Target Workload:** A DDoS simulation script (`udp_flood.py`) using the Python `socket` library to bombard port 9999 on the local loopback (`127.0.0.1`) with hundreds of thousands of UDP packets per second.
* **Defense & Observability Tools:** * An eBPF/XDP program written in C (`xdp_drop_port.c`): Identifies packets bound for port 9999 and executes `XDP_DROP`.
  * `tcpdump`: Observes packet arrival at the kernel network stack (AF_PACKET) level.

## 📊 1. Baseline: Limitations of the Traditional Kernel Network Stack (No Defense)
I executed the massive UDP packet attack and measured the performance without the XDP defense mechanism.

* **Observation (`tcpdump`):** All packets flooded into the kernel stack, outputting a massive amount of logs.
* **Throughput (Attacker):** Reached approximately **200,000 ~ 300,000 pkt/sec**.
* **Bottleneck Analysis:** Because this is a loopback test environment, the attacker (Python script) and the defender (Kernel) share the same CPU. The kernel's CPU resources were exhausted as it performed heavy operations for every incoming packet: allocating massive `sk_buff` structures in memory (Copying) and traversing the IP/UDP stack. This degraded overall system performance and, paradoxically, limited the transmission speed of the attacking script itself.

![img1](before_guard.png)

## 📊 2. XDP Defense: Driver-Level Packet Interception (Zero-copy & OS Bypass)
I injected eBPF bytecode at the NIC driver level (`xdpgeneric` on `lo`) to instantly drop (`XDP_DROP`) UDP packets destined for port 9999 before they could enter the kernel.

* **Observation (`tcpdump`):** Despite hundreds of thousands of packets being fired per second, **`tcpdump` showed absolute silence without a single packet log.** This confirms that the packets were dropped at the lowest level before ever reaching the kernel network stack (OS Bypass).
* **Throughput (Attacker):** Reached approximately **610,000 ~ 620,000 pkt/sec** (**More than a 2x surge in performance**).

![img2](after_guard.png)

## 💡 Conclusion
Through this experiment, I confirmed the tremendous power of **Zero-copy and OS Bypass** in modern high-performance system architectures.

1. **Kernel Protection and Resource Conservation (Defending Resource Exhaustion)**
   
   Upon applying XDP, the packet processing volume (attack speed) more than doubled from 300k to over 610k. This is because CPU resource consumption was drastically reduced by eliminating memory allocation (`sk_buff`) and stack overhead at the very frontline of packet processing. Consequently, the freed-up CPU resources were utilized by the attacking script, resulting in higher speeds.
   
2. **Next-Generation Security/Networking Architecture for Mission-Critical Systems**
   
   By deploying XDP-based filtering at the frontline of defense, finance (HFT), or large-scale cloud infrastructures, servers can handle massive malicious traffic (DDoS) without the backend applications or the kernel experiencing any load (0% CPU utilization). This ensures that only legitimate client requests are fully and safely processed.
