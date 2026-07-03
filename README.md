# 🐧 Linux Kernel Deep Dive
고가용성 및 실시간성이 요구되는 시스템(방산 체계, AI 인프라)을 위한 **리눅스 커널 심층 분석 및 최적화 프로젝트** 입니다.

시스템의 병목을 식별하고 제로 오버헤드로 관측하며, 하드웨어 자원을 극한으로 최적화하는 아키텍처 설계를 학습합니다.

## 🛠️ Tech Stack
* **Language**
  <br>![C](https://img.shields.io/badge/C-00599C?style=for-the-badge&logo=c&logoColor=white) ![Python](https://img.shields.io/badge/Python-3776AB?style=for-the-badge&logo=python&logoColor=white)
* **Kernel & OS**
  <br>![Linux](https://img.shields.io/badge/Linux_Kernel-FCC624?style=for-the-badge&logo=linux&logoColor=black) ![WSL2](https://img.shields.io/badge/WSL2_(5.15+)-0078D6?style=for-the-badge&logo=windows&logoColor=white) ![Raspberry Pi](https://img.shields.io/badge/Raspberry_Pi_Native-A22846?style=for-the-badge&logo=Raspberry%20Pi&logoColor=white)
* **Observability & Network**
  <br>![eBPF](https://img.shields.io/badge/eBPF-4479A1?style=for-the-badge&logo=linux&logoColor=white) ![XDP](https://img.shields.io/badge/XDP-E34F26?style=for-the-badge&logo=linux&logoColor=white) *(CO-RE, BCC, libbpf / eXpress Data Path)*
* **AI Pair Programming**
  <br>![Gemini](https://img.shields.io/badge/Google_Gemini-8E75B2?style=for-the-badge&logo=googlegemini&logoColor=white) ![OpenAI Codex](https://img.shields.io/badge/OpenAI_Codex-412991?style=for-the-badge&logo=openai&logoColor=white) *(가설 설정, 검증 및 커널 아키텍처 멘토링)*

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
* **Directory:** [`/02_memory_cfs`](./02_memory_cfs/)
* **Summary:** eBPF를 활용해 커널 레벨의 마이크로초(us) 단위 꼬리 지연(Tail Latency) 원인(CFS 스케줄링 경합 및 Page Fault 병목)을 분석하고, 코어 친화도(CPU Affinity)와 우선순위(Nice) 튜닝을 통해 극한의 스케줄링 환경(CPU Starvation)을 성공적으로 확인했습니다.

### ✅ [STEP 3] Network: Zero-copy Firewall using XDP (sk_buff Allocation Bypass)
* **Status:** Completed
* **Directory:** [`/03_network_xdp`](./03_network_xdp/)
* **Summary:** 전통적인 리눅스 네트워크 스택(`sk_buff` 할당)이 유발하는 구조적 병목을 분석하고, eBPF/XDP를 통해 NIC 드라이버 레벨에서 악성 UDP 패킷을 즉시 드랍(OS Bypass)함으로써 공격 방어 속도를 2배 이상 끌어올린 고속 Zero-copy 방화벽을 구현했습니다.

### ✅ [STEP 4] Interrupt Handling: Designing Low-Latency Linux Device Drivers (Top & Bottom Half)
* **Status:** Completed
* **Directory:** [`/04_driver_interrupt`](./04_driver_interrupt/)
* **Summary:** Raspberry Pi 5 하드웨어 환경에서 의도적으로 무겁게 설계된 ISR과 Workqueue 기반의 지연(Deferred) 설계를 비교하여, Top Half 및 Bottom Half 인터럽트 처리 분리의 중요성을 검증했습니다.
BCC/eBPF 트레이스포인트를 활용해 IRQ 핸들러 실행 시간을 측정한 결과, 잘못 설계된 드라이버는 평균 126.19 ms 동안 하드 인터럽트 컨텍스트에서 CPU를 점유한 반면, Workqueue 기반 설계는 오래 걸리는 작업을 커널 워커 스레드(Worker thread)로 위임함으로써 관측된 Top Half 소요 시간을 단 2.73 us로 감소시켰습니다.
