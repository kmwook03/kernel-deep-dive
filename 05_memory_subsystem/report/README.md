# 05. Memory Subsystem: Bypassing OS Page Faults via Lazy Allocation (HugePages & userfaultfd)

## ❓ 배경 및 문제 정의
### 1. 대규모 메모리 워크로드와 4KB 페이징의 딜레마
현대의 AI 추론 엔진, 인메모리 데이터베이스, 실시간 대규모 처리 시스템은 기가바이트(GB)에서 테라바이트(TB) 단위의 거대한 메모리 공간을 연속적으로 요구한다. 그러나 대부분의 현대 운영체제는 과거 메모리 용량이 적던 시절에 설계된 4KB 크기의 기본 페이지 단위를 표준으로 사용하고 있다. <br>
단일 4KB 페이지는 일반적인 데스크탑 환경에서 메모리 파편화를 막는 훌륭한 타협점이지만 현대의 대규모 워크로드에서는 하드웨어 아키텍처에 치명적인 병목을 유발하는 원인이 된다.

### 2. TLB Thrashing과 MMU 오버헤드
CPU가 가상 주소를 물리 주소로 변환할 때 가장 먼저 하드웨어 기반의 변환 캐시인 TLB(Translation Lookaside Buffer)를 참조한다. 일반적인 CPU의 dTLB(Data TLB) 엔트리 수는 1,000개 - 2,000개 수준이다. <br>
만약 4KB 단위로 1,500개의 TLB 엔트리를 모두 채운다고 해도, 캐싱할 수 있는 최대 메모리 범위(TLB Coverage)는 $1500 \times 4\text{KB} \approx 6\text{MB}$에 불과하다.

따라서 1GB 이상의 거대한 메모리를 순회하는 워크로드에서는 필연적으로 기존 캐시가 끊임없이 축출되는 TLB Thrashing이 폭발적으로 발생한다. TLB Miss가 발생하면 하드웨어 MMU(Memory Management Unit)는 메인 메모리에 위치한 페이지 테이블을 여러 단계에 걸쳐 탐색하는 Page Table Walk를 수행해야 하며, 이는 메모리 접근 지연(Latency)과 심각한 CPU 사이클 낭비를 초래한다.

### 3. 범용 커널 Demand Paging의 블랙박스화
운영체제는 프로세스가 메모리를 요청할 때 물리 메모리를 즉시 매핑하지 않고, 실제 접근 시점에 발생하는 Page Fault 인터럽트를 통해 페이지를 할당하는 Demand Paging 기법을 사용한다. 이는 범용적인 시스템의 메모리 절약에는 필수적이지만 응답 속도와 결정론적 실행이 중요한 시스템에서는 치명적이다. 수십만 번의 Page Fault가 유발하는 커널 모드 전환과 인터럽트 처리는 런타임 성능을 감소시키는 제어 불가능한 오버헤드가 된다.

## 📌 목표 
본 실험은 대규모 메모리 할당 및 접근 시 발생하는 OS 레벨의 Page Fault 커널 오버헤드와 하드웨어 레벨의 TLB Thrashing 현상을 정량적으로 관측한다. 그리고 이를 해결하기 위해 Transparent HugePages(THP)를 통한 페이지 단위 확장 및 `userfaultfd`를 활용한 유저 스페이스 기반의 지연 할당(Lazy Allocation) 기법을 구현하고 그 효과를 검증한다.

## 🛠️ 테스트 환경 & 타겟 워크로드
* **Hardware:** Raspberry Pi 5 Model B Rev 1.0
* **OS / Kernel:** Ubuntu Server 24.04.4 LTS, Linux `6.8.0-1047-raspi` (`aarch64`)
* **Observability Tool:** Linux `perf` (Performance Counters API)
* **Target Workload:** 1GB 크기의 거대한 배열을 동적 할당한 뒤, 시스템 기본 페이지 크기(4KB) 단위로 건너뛰며 접근(Stride Access)하여 의도적으로 캐시 지역성을 파괴하고 TLB Miss 및 다량의 Page Fault를 유발하는 C 프로그램

## 🧪 실험 설계 
1. Phase 1(Baseline): 4KB 페이지 환경에서 1GB 메모리에 Stride 패턴으로 접근하며 perf를 통해 dTLB Miss Rate와 Page Fault 발생 횟수를 기준점으로 측정한다.
2. Phase 2 (HugePage): C 코드 내에서 posix_memalign으로 2MB 정렬을 맞추고, madvise(MADV_HUGEPAGE) 시스템 콜을 호출하여 커널에 THP 사용을 요청한 뒤 동일 워크로드의 성능 개선율을 측정한다.
3. Phase 3 (`userfaultfd`): 시스템 콜(`SYS_userfaultfd`)을 호출해 특정 메모리 영역의 Page Fault 처리 권한을 커널에서 유저 스페이스로 가져온다. 메인 스레드의 Fault를 백그라운드 워커 스레드가 감지하고 `UFFDIO_COPY`로 데이터를 동적 주입하는 비동기 할당 구조를 구현한다.

## 📊 1. Baseline: TLB Thrashing 유발 및 측정
시스템의 하드웨어 캐시와 OS의 메모리 관리 메커니즘을 극한으로 몰아붙이기 위한 스트레스 테스트 코드(`workload.c`)를 활용한다. 이 코드는 `posix_memalign()`을 사용하여 시작 주소가 정렬된 1GB의 메모리를 할당 받은 후 4KB 크기씩 건너뛰며 1바이트만 값을 쓴다. 이를 통해 Page Fault가 반복적으로 발생시켜 커널을 병목에 빠뜨리고 공간 지역성을 무시하는 접근으로 dTLB Miss를 지속적으로 발생시킨다.
```
 Performance counter stats for './workload':

        1197721123      dTLB-loads                                                            
          23321474      dTLB-load-misses                 #    1.95% of all dTLB cache accesses
            262194      page-faults                                                           

       2.496188984 seconds time elapsed

       1.681575000 seconds user
       0.807835000 seconds sys
```
1GB 메모리를 4KB 단위로 접근할 때 발생하는 이론적인 Page Fault 횟수($1 \text{GB} / 4\text{KB} = 262,144$번)와 실제 측정값(262,194번)이 정확히 일치한다. 하드웨어 TLB 캐시 용량을 초과하는 26만 개의 페이지를 쉴 새 없이 순회함에 따라 2,332만 번에 달하는 심각한 TLB Thrashing이 발생했으며, 커널 인터럽트 처리로 인해 sys 시간이 0.8초나 소요되었다.

## 📊 2. HugePage
Baseline 측정과 달리 주소 정렬(`thp_alignment`)을 2MB로 설정하여 2MB로 증가할 페이지 크기를 MMU가 인식할 수 있도록 하였다. 이후 `madvise()` 함수를 통해 페이지 크기를 2MB로 확장시킨 후 동일한 스트레스 루프를 실행시킨다.
```
 Performance counter stats for './workload_thp':

         275499081      dTLB-loads                                                            
           3414722      dTLB-load-misses                 #    1.24% of all dTLB cache accesses
             22536      page-faults                                                           

       2.407181747 seconds time elapsed

       1.795903000 seconds user
       0.609288000 seconds sys
```

| 지표 | Baseline (4KB) | THP 적용 (2MB) | 변화율 |
|-|-|-|-|
| Page Faults | 262,194 | 22,536 | 약 91.4% 감소 |
| dTLB Misses | 23,321,474 | 3,414,722 | 약 85.3% 감소 |
| Sys Time | 0.807초 | 0.609초 | 약 24.5% 단축 |

이론상 1GB를 2MB HugePage로 나누면 Page Fault는 딱 512번만 발생해야한다. 하지만 22,536번 발생했다.
원인은 라즈베리 파이 커널의 물리 메모리 단편화로, 커널이 1GB를 할당하면서 연속된 2MB 물리 메모리 블록을 최대한 찾아서 할당했지만, 공간이 부족한 구간에서는 어쩔 수 없이 기본 4KB 페이지로 fallback한 것이다.
그럼에도 TLB Missrk 2,300만 번에서 340만 번으로 급감하며 전체 성능이 유의미하게 감소하였다.

## 📊 3. userfaultfd 기반 Lazy Allocation 
본 워크로드(`workload_uffd.c`)에서는 커널의 개입을 최소화하고 애플리케이션이 직접 메모리의 흐름을 통제한다. <br> 
`userfaultfd` 시스템 콜을 호출해 커널과 통신할 전용 파일 디스크립터(`uffd`)를 열고 `mmap`으로 물리 메모리가 할당되지 않은 빈 가상 주소 공간을 만든다. 이후 `ioctl()` 시스템 콜의 옵션으로 `UFFDIO_REGISTER`를 주어 `mmap`으로 할당한 주소 영역에 Page Fault가 발생하더라도 커널이 임의로 물리 메모리를 할당하거나 프로세스를 죽이지 않고 `uffd`로 메시지만 보내도록 설정을 바꾸었다.

이를 통해 메인 스레드가 물리 메모리가 매핑되지 않은 가상 주소(`0xffffb13b4000`)를 읽으려 시도할 때 발생하는 Page Fault를 커널이 직접 처리(SIGSEGV 발생 또는 Demand Paging)하지 않고, 파일 디스크립터를 통해 이벤트를 유저 스페이스로 전달했다. 이후 메인 스레드는 메모리가 할당될 때 까지 `sleep`한다.

`poll()`을 통해 `uffd`를 감시하며 대기하던 워커 스레드가 메시지를 감지하여 사용자 정의 데이터('A')를 메모리에 복사(`UFFDIO_COPY`)한 뒤 메인 스레드의 실행을 재개시킨다. 이는 메모리 할당의 주도권이 완벽하게 유저 스페이스로 넘어왔음을 증명한다.



## 💡 결론
시스템의 메모리 할당을 OS 커널의 범용적인 Demand Paging 메커니즘에만 의존할 경우, 대규모 데이터를 다루는 환경에서는 TLB Thrashing과 인터럽트 폭주로 인한 심각한 병목을 피할 수 없다.

본 프로젝트를 통해 `madvise`를 통한 OS 정책 제어와 `userfaultfd`를 통한 유저 스페이스 메모리 핸들링 기법을 적용함으로써, 워크로드의 특성에 맞춰 시스템의 로우레벨 자원을 프로그래머가 능동적으로 오케스트레이션(Orchestration)할 때 얻을 수 있는 인프라 효율성을 증명하였다.

<details>
<summary><b>Terminal Output</b></summary>
<div markdown="1">
```
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
