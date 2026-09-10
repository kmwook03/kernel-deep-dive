# 🐧 Linux Kernel Deep Dive
고가용성과 낮은 지연이 요구되는 시스템을 대상으로 하는 **리눅스 커널 심층 분석 및 최적화 프로젝트**이다.

eBPF, XDP, 스케줄러, 인터럽트, 메모리 서브시스템을 실험적으로 관측하고, 운영체제 정책과 하드웨어 자원이 성능에 미치는 영향을 정량화한다.

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
커널 관측과 스케줄러 분석(STEP 1·2)은 BTF(BPF Type Format)가 활성화된 WSL2에서 CO-RE(Compile Once, Run Everywhere) 기반으로 수행했다.

네트워크 필터링(STEP 3)은 WSL2 루프백의 generic XDP에서, 인터럽트와 메모리 서브시스템 실험(STEP 4·5)은 Raspberry Pi 5 기반 네이티브 Linux 환경에서 수행했다. 따라서 각 결과는 해당 커널, 하드웨어, 실험 조건의 범위에서 해석한다.

---

## 🗺️ Deep Dive Roadmap & Status

### ✅ [STEP 1] Observability: ptrace vs. eBPF (Context Switch Overhead Analysis)
* **Status:** Completed
* **Directory:** [`/01_observability_ebpf`](./01_observability_ebpf/)
* **Summary:** 
  `clone`과 `wait4`를 10,000회 반복한 워크로드에서 `strace`는 커널 CPU 시간 2.89초, eBPF는 1.40초를 기록했다. 본 조건에서 커널 내 추적이 `ptrace` 기반 추적보다 작은 교란을 보였다. 단, Baseline은 Cold Start 1회 측정이므로 절대 오버헤드는 반복 실험으로 추가 검증해야 한다.

### ✅ [STEP 2] Performance: CFS Scheduler & Page Fault Analysis (Memory Subsystem)
* **Status:** Completed
* **Directory:** [`/02_memory_cfs`](./02_memory_cfs/)
* **Summary:** eBPF로 CFS 실행 대기열과 `handle_mm_fault` 지연 분포를 측정했다. 단일 CPU에서 nice -20과 nice 19를 경쟁시킨 결과 일부 일반 스레드의 대기 시간이 1초 이상으로 증가하여, CPU 친화도와 우선순위가 기아 가능성에 미치는 영향을 확인했다.

### ✅ [STEP 3] Network: Early Packet Drop with Generic XDP
* **Status:** Completed
* **Directory:** [`/03_network_xdp`](./03_network_xdp/)
* **Summary:** WSL2 루프백의 generic XDP에서 9999번 포트 UDP 패킷을 `XDP_DROP`으로 조기 폐기했다. 패킷이 AF_PACKET 관측 지점에 도달하지 않았고, 동일 호스트의 송신 처리량은 200,000~300,000 pkt/s에서 610,000~620,000 pkt/s로 증가했다.

### ✅ [STEP 4] Interrupt Handling: Designing Low-Latency Linux Device Drivers (Top & Bottom Half)
* **Status:** Completed
* **Directory:** [`/04_driver_interrupt`](./04_driver_interrupt/)
* **Summary:** Raspberry Pi 5에서 ISR 내 busy loop와 Workqueue 위임 구조를 비교했다. BCC/eBPF로 측정한 IRQ 핸들러 평균 실행 시간은 126.19 ms에서 2.73 μs로 감소했다. 이 결과는 전체 작업이 제거된 것이 아니라 하드 IRQ 경로에서 워커 스레드로 이동했음을 의미한다.

### ✅ [STEP 5] Memory Subsystem: Page-Fault Policy and Large-Page Evaluation (THP & userfaultfd)
* **Status:** Completed
* **Directory:** [`/05_memory_subsystem`](./05_memory_subsystem/)
* **Goal:** 대규모 메모리 접근의 dTLB miss와 페이지 폴트 처리 비용을 관측하고, THP와 `userfaultfd`를 이용한 메모리 정책 제어를 검증한다.
* **Summary:** Raspberry Pi 5에서 1 GB 영역을 4 KB 간격으로 접근한 Baseline은 페이지 폴트 262,194회와 dTLB load miss 23,321,474회를 기록했다. `MADV_HUGEPAGE`를 적용하면 각 지표가 91.4%와 85.3% 감소했고, `sys` 시간은 24.5%, 벽시계 시간은 3.6% 단축되었다. 또한 `userfaultfd`로 폴트 이벤트를 워커 스레드에 전달하고 `UFFDIO_COPY`로 페이지를 공급하여, 페이지 해결 정책을 사용자 공간에서 제어할 수 있음을 확인했다.
