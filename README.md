# 🐧 Linux Kernel Deep Dive
고가용성 및 실시간성이 요구되는 시스템(방산 체계, AI 인프라)을 위한 **리눅스 커널 심층 분석 및 최적화 프로젝트** 입니다.

시스템의 병목을 식별하고 제로 오버헤드로 관측하며, 하드웨어 자원을 극한으로 최적화하는 아키텍처 설계를 학습합니다.

## 🛠️ Tech Stack
* **Language:** C (향후 Rust 기반의 시스템 프로그래밍으로 확장 예정)
* **Kernel & OS:** Linux Kernel, WSL2 (Kernel 5.15+), Raspberry Pi Native Linux
* **Observability & Network:** eBPF (CO-RE, BCC, libbpf), XDP (eXpress Data Path)

## 📌 Architecture & Environment Note
초기 커널 관측 및 스케줄러 분석(Step 1, 2)은 BTF(BPF Type Format)가 활성화된 WSL2 환경에서 CO-RE(Compile Once, Run Everywhere) 메커니즘을 활용하여 진행합니다.

이후 하드웨어 레벨의 네트워크 버퍼 제어(XDP) 및 인터럽트 제어(Step 3, 4)를 위해 네이티브 리눅스(Raspberry Pi 등) 환경으로 타겟을 마이그레이션할 계획입니다.

---

## 🗺️ Deep Dive Roadmap & Status

### ✅ [STEP 1] Observability: ptrace vs. eBPF (Context Switch Overhead Analysis)
* **Status:** Completed
* **Directory:** [`/01_observability_ebpf`](./01_observability_ebpf/)
* **Summary:** 
  기존 시스템 콜 추적 도구(`strace`)가 유발하는 유저-커널 간의 Context Switch 오버헤드를 수치화하고, eBPF의 Zero-overhead 특성을 확인합니다.
  컨테이너 격리(`clone`)를 10,000회 반복하는 부하 환경에서, `strace`는 약 **2.89초**의 커널 CPU 시간(`sys`)을 소모한 반면, 커널 내부에서 JIT 컴파일되어 실행되는 `eBPF`는 단 **1.40초**만을 소모하여 성능 저하 없는 동적 추적(Dynamic Tracing)이 가능함을 보았습니다.

### ✅ [STEP 2] Performance: CFS Scheduler & Page Fault Analysis (Memory Subsystem)
* **Status:** Completed
* **Directory:** `/02_memory_cfs`
* **Summary:** eBPF를 활용해 커널 레벨의 마이크로초(us) 단위 꼬리 지연(Tail Latency) 원인(CFS 스케줄링 경합 및 Page Fault 병목)을 분석하고, 코어 친화도(CPU Affinity)와 우선순위(Nice) 튜닝을 통해 극한의 스케줄링 환경(CPU Starvation)을 성공적으로 확인했습니다.

### ⏳ [STEP 3] Network: Zero-copy Firewall using XDP (sk_buff Allocation Bypass)
* **Status:** Planned
* **Directory:** `/03_network_xdp`
* **Goal:** 대규모 악성 패킷(SYN Flooding) 인입 시, 커널의 거대한 `sk_buff` 메모리 할당을 우회하여 NIC 드라이버 레벨에서 패킷을 드롭(XDP_DROP)하는 방어 아키텍처를 구현합니다.

### ⏳ [STEP 4] Device Driver: Interrupt Handling Mechanism (Top & Bottom Half)
* **Status:** Planned
* **Directory:** `/04_driver_interrupt`
* **Goal:** 하드웨어 제어 시 발생하는 인터럽트 폭주로 인한 커널 패닉을 방지하기 위해, Workqueue를 활용한 비동기식(Deferred Work) 안전한 드라이버 아키텍처를 설계합니다.