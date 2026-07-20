# 05. Memory Subsystem: Bypassing OS Page Faults via Lazy Allocation (HugePages & userfaultfd)

## 🔎 배경 및 문제 정의
### 1. 대규모 메모리 워크로드와 4KB 페이징의 딜레마
현대의 AI 추론 엔진, 인메모리 데이터베이스, 실시간 대규모 처리 시스템은 기가바이트(GB)에서 테라바이트(TB) 단위의 거대한 메모리 공간을 연속적으로 요구한다. 그러나 대부분의 현대 운영체제는 과거 메모리 용량이 적던 시절에 설계된 4KB 크기의 기본 페이지 단위를 표준으로 사용하고 있다. <br>
단일 4KB 페이지는 일반적인 데스크탑 환경에서 메모리 파편화를 막는 훌륭한 타협점이지만 현대의 대규모 워크로드에서는 하드웨어 아키텍처에 치명적인 병목을 유발하는 원인이 된다.

### 2. TLB Thrashing과 MMU 오버헤드
CPU가 가상 주소를 물리 주소로 변환할 때 가장 먼저 하드웨어 기반의 변환 캐시인 TLB(Translation Lookaside Buffer)를 참조한다. 일반적인 CPU의 dTLB(Data TLB) 엔트리 수는 1,000개 - 2,000개 수준이다. <br>
만약 4KB 단위로 1,500개의 TLB 엔트리를 모두 채운다고 가정해도, 하드웨어가 캐싱할 수 있는 최대 메모리 범위(TLB Coverage)는 $1500 \times 4\text{KB} \approx 6\text{MB}$에 불과하다.

따라서 1GB 이상의 거대한 메모리를 순회하는 워크로드에서는 필연적으로 기존 캐시가 끊임없이 축출되는 TLB Thrashing이 폭발적으로 발생한다. TLB Miss가 발생하면 하드웨어 MMU(Memory Management Unit)는 메인 메모리에 위치한 페이지 테이블을 여러 단계에 걸쳐 탐색하는 Page Table Walk를 수행해야 하며, 이는 메모리 접근 지연(Latency)과 심각한 CPU 사이클 낭비를 초래한다.

### 3. 범용 커널 Demand Paging의 블랙박스화
운영체제는 프로세스가 메모리를 요청할 때 물리 메모리를 즉시 매핑하지 않고, 실제 접근 시점에 발생하는 Page Fault 인터럽트를 통해 페이지를 할당하는 Demand Paging 기법을 사용한다. 이는 범용 시스템의 메모리 절약에는 필수적이나, 응답 속도와 결정론적(Deterministic) 실행이 중요한 시스템에서는 치명적이다. 수십만 번의 Page Fault가 연속적으로 유발하는 커널 모드 전환과 인터럽트 처리는 런타임 성능을 감소시키는 제어 불가능한 오버헤드가 된다.

## 📌 목표 
본 실험은 대규모 메모리 할당 및 접근 시 발생하는 OS 레벨의 Page Fault 오버헤드와 하드웨어 레벨의 TLB Thrashing 현상을 정량적으로 관측한다. 나아가 이를 해결하기 위해 Transparent HugePages(THP)를 통한 하드웨어 친화적 확장과 `userfaultfd`를 활용한 유저 스페이스 기반의 제어권 탈취 및 지연 할당(Lazy Allocation) 기법을 구현하고, 각각의 접근법이 갖는 성능적 특성과 Trade-off를 검증한다.

## 🛠️ 테스트 환경 & 타겟 워크로드
* **Hardware:** Raspberry Pi 5 Model B Rev 1.0
* **OS / Kernel:** Ubuntu Server 24.04.4 LTS, Linux `6.8.0-1047-raspi` (`aarch64`)
* **Observability Tool:** Linux `perf` (Performance Counters API)
* **Target Workload:** 1GB 크기의 거대한 배열을 동적 할당한 뒤, 시스템 기본 페이지 크기(4KB) 단위로 건너뛰며 접근(Stride Access)하여 의도적으로 캐시 지역성을 파괴하고 TLB Miss 및 다량의 Page Fault를 유발하는 C 프로그램

## 🧪 실험 설계 
1. Phase 1(Baseline): 4KB 페이지 환경에서 1GB 메모리에 Stride 패턴으로 접근하며 perf를 통해 dTLB Miss Rate와 Page Fault 발생 횟수를 기준점으로 측정한다.
2. Phase 2 (HugePage): 코드 내에서 `posix_memalign`으로 2MB 정렬을 맞추고, `madvise(MADV_HUGEPAGE)` 시스템 콜을 호출하여 커널에 THP 사용을 명시적으로 요청한 뒤 처리량(Throughput) 개선율을 측정한다.
3. Phase 3 (userfaultfd): `SYS_userfaultfd`를 호출해 특정 메모리 영역의 Page Fault 처리 권한을 커널에서 유저 스페이스로 이관한다. 메인 스레드의 Fault를 백그라운드 워커 스레드가 감지하고 `UFFDIO_COPY`로 데이터를 동적 주입하는 비동기 할당 파이프라인을 구현하여 스케줄링 오버헤드를 측정한다.

## 📊 Phase 1. Baseline: TLB Thrashing 유발 및 측정
시스템의 하드웨어 캐시와 OS의 메모리 관리 메커니즘을 극한으로 몰아붙이기 위한 스트레스 테스트 코드(`workload.c`)를 실행했다. `posix_memalign()`을 사용하여 시작 주소가 정렬된 1GB의 메모리를 할당 받은 후, 4KB 크기씩 건너뛰며 1바이트에만 값을 쓰는 방식이다. 이를 통해 공간 지역성(Spatial Locality)을 파괴하고 커널을 Page Fault 병목에 빠뜨린다.
```
[Info] System Page Size: 4096 Bytes
[Info] Allocating 1GB of memory...
[Info] Starting TLB thrashing (Stride: 4096 Bytes)...
[Info] Memory access complete.

 Performance counter stats for './workload':

        1144982560      dTLB-loads                                                            
          26844609      dTLB-load-misses                 #    2.34% of all dTLB cache accesses
            262194      page-faults                                                           

       2.634251882 seconds time elapsed

       1.857838000 seconds user
       0.771102000 seconds sys
```
1GB 메모리를 4KB 단위로 접근할 때 발생하는 이론적인 Page Fault 횟수($1 \text{GB} / 4\text{KB} = 262,144$번)와 유사한 측정값(262,194번)이 관측되었다. 하드웨어 TLB 캐시 용량을 초과하는 26만 개의 페이지를 쉴 새 없이 순회함에 따라 2,684만 번에 달하는 심각한 TLB Thrashing이 발생했으며, 커널 인터럽트 처리로 인해 sys 시간이 0.77초나 소요되었다.

## 📊 Phase 2. HugePage: 하드웨어 효율 극대화
Baseline 측정과 달리 주소 정렬(`thp_alignment`)을 2MB로 설정하여 MMU가 HugePage 크기를 인식할 수 있도록 기반을 마련하였다. 이후 `madvise()`를 통해 해당 영역의 페이지 정책을 2MB로 확장할 것을 커널에 지시(Hinting)한 후 동일한 스트레스 루프를 실행했다.(`workload_thp.c`)
```
[Info] System Page Size: 4096 Bytes
[Info] Allocating 1GB of memory for THP...
[Info] madvise(MADV_HUGEPAGE) applied successfully.
[Info] Starting TLB thrashing (Stride: 4096 Bytes)...
[Info] Memory access complete.

 Performance counter stats for './workload_thp':

          58378083      dTLB-loads                                                            
            431540      dTLB-load-misses                 #    0.74% of all dTLB cache accesses
               563      page-faults                                                           

       2.072130400 seconds time elapsed

       1.868557000 seconds user
       0.199845000 seconds sys
```

| 지표 | Baseline (4KB) | THP 적용 (2MB) | 변화율 |
|-|-|-|-|
| Page Faults | 262,194 | 563 | 약 99.7% 감소 |
| dTLB Misses | 26,844,609 | 431,540 | 약 98.3% 감소 |
| Sys Time | 0.771초 | 0.199초 | 약 74.1% 단축 |

이론상 1GB를 2MB HugePage로 나누면 Page Fault는 딱 512번만 발생해야 한다. 실제 측정 결과 Page Fault가 563번으로 관측되며 이론치에 근접하게 발생하였다. 커널이 물리 메모리 단편화 없이 1GB 대부분을 성공적으로 2MB 연속 블록으로 매핑해냈으며, 이에 따라 TLB Miss 역시 약 2,684만 번에서 43만 번으로 크게 감소하여 전체 성능과 커널 오버헤드(Sys Time)가 대폭 향상되었다.

## 📊 Phase 3. userfaultfd 기반 Lazy Allocation : 제어권 이관
본 워크로드(`workload_uffd.c`)에서는 커널의 개입을 최소화하고 애플리케이션이 직접 메모리의 흐름을 통제한다. <br> 
`userfaultfd` 시스템 콜을 호출해 전용 파일 디스크립터(`uffd`)를 열고, `mmap`으로 물리 메모리가 할당되지 않은 가상 주소 공간을 생성한다. 이후 `ioctl(UFFDIO_REGISTER)`을 통해 해당 영역에 Page Fault가 발생하더라도 커널이 임의로 물리 메모리를 할당하거나 프로세스를 종료(SIGSEGV)시키지 않고 `uffd`로 이벤트 메시지만 보내도록 덫(Trap)을 설정했다.

이를 통해 메인 스레드가 미할당 주소를 읽으려 할 때 커널의 Demand Paging을 무력화시켰다. 메인 스레드가 블로킹된 사이, `poll()`로 대기하던 백그라운드 워커 스레드가 이벤트를 감지하여 데이터를 복사(`UFFDIO_COPY`)한 뒤 메인 스레드의 실행을 재개시킨다. 메모리 할당의 주도권이 완벽하게 유저 스페이스로 넘어왔음을 증명하는 구조다.

```
[Main] Triggering memory accesses...
[Worker] Ready to handle Page Faults...
[Main] Memory access complete.

 Performance counter stats for './workload_uffd':

        5197635385      dTLB-loads                                                            
          75977504      dTLB-load-misses                 #    1.46% of all dTLB cache accesses
            262196      page-faults                                                           

       7.141437371 seconds time elapsed

       2.386435000 seconds user
       4.942678000 seconds sys
```

| 지표 | Phase 1 (Baseline, 4KB) | Phase 2 (HugePage, 2MB) | Phase 3 (userfaultfd, 4KB) |
|-|-|-|-|
| Page Faults | 262,194 | 563 (최저) | 262,196 |
| dTLB Misses | 2,684만 (2.34%) | 43만 (0.74%) (최저) | 7,597만 (1.46%) |
| Sys Time | 0.771초 | 0.199초 (최저) | 4.942초 (최대) |
| 총 소요 시간 | 2.634초 | 2.072초 (최저) | 7.141초 (최대) |

측정 결과, 본 워크로드는 Sys Time과 총 소요 시간 관점에서 가장 비효율적인 성능을 보였다. 그 원인은 극심한 스레드 핑퐁 오버헤드에 있다. 메인 스레드 $\rightarrow$ 커널(블로킹) $\rightarrow$ 워커 스레드(깨어남) $\rightarrow$ `ioctl` 호출 $\rightarrow$ 커널 $\rightarrow$ 메인 스레드 재개라는 복잡한 파이프라인을 26만 번이나 거치며 발생한 런타임 스케줄링 오버헤드가 Sys Time 증가로 이어졌다.

또한 동일한 Stride 접근 패턴임에도 TLB Miss가 Baseline 대비 약 2.8배 치솟았다. 이는 컨텍스트 스위칭이 빈번하게 발생할 때마다 CPU 코어의 레지스터가 교체되고, 워커 스레드가 커널 공간을 드나들며 TLB 엔트리를 지속적으로 방출(Eviction) 및 오염(Pollution)시켰기 때문이다. 즉, 데이터 접근 패턴뿐만 아니라 스케줄링 주기 자체가 캐시 지역성을 파괴할 수 있음을 보여준다.

## 🧠 결과 분석: 오케스트레이션 전략의 이원화
본 실험 결과는 대상 워크로드의 핵심 성능 지표에 따라 메모리 서브시스템의 최적화 전략이 철저히 이원화되어야 함을 시사한다. 처리량(Throughput)이 최우선시되는 대규모 연속 메모리 할당 및 고속 접근이 필요한 환경의 경우, 사용자 영역의 개입을 최소화하고 커널에 할당 정책을 위임하는 것이 타당하다. `madvise(MADV_HUGEPAGE)`를 통한 명시적 메모리 힌팅은 하드웨어 MMU의 THP(Transparent HugePages) 커버리지를 극대화함으로써, 연속 할당 시 발생하는 운영체제 레벨의 런타임 병목을 가장 신뢰할 수 있는 방식으로 제거한다.

그럼에도 불구하고 최신 클라우드 및 AI 인프라 시스템이 `userfaultfd`를 적극적으로 채택하는 이유는 초기 응답성 최적화에 있다. 수십 GB 규모의 AI 모델 가중치나 분산 데이터베이스 스냅샷을 로딩할 때 발생하는 시스템의 주된 병목은 수 밀리초(ms) 단위의 딜레이를 갖는 디스크 및 네트워크 I/O에 집중된다. 이러한 I/O Bound 환경에서는 `userfaultfd`가 유발하는 마이크로초(µs) 단위의 컨텍스트 스위칭 패널티가 막대한 I/O 대기 시간 속에 효과적으로 은닉(Latency Hiding)된다.

즉, 거대 데이터를 메모리에 선적재(Pre-load)하여 발생하는 시스템 정지(Cold Start) 현상을 감수하는 대신, 실제 접근이 발생한 페이지 청크 단위로 워커 스레드가 데이터를 비동기 주입하는 Fine-grained 온디맨드 지연 로딩 파이프라인을 설계함으로써 초기 구동 지연을 기저 수준으로 단축할 수 있다.

## 💡 결론
시스템의 메모리 할당을 범용적인 OS Demand Paging 메커니즘에만 의존할 경우, 대규모 데이터를 다루는 현대의 워크로드에서는 TLB Thrashing과 인터럽트 폭주로 인한 심각한 병목을 피할 수 없다.

본 프로젝트는 운영체제 이론을 확인함과 동시에 하드웨어 아키텍처와 커널 서브시스템의 한계를 정량적으로 분석하였다. 이를 바탕으로 `madvise`를 이용한 OS 정책 제어와 `userfaultfd`를 활용한 제어권 탈취 기법을 교차 검증함으로써, 워크로드의 성격에 맞춰 시스템의 로우레벨 자원을 프로그래머가 능동적으로 오케스트레이션(Orchestration)할 때 극대화된 인프라 효율성을 얻을 수 있음을 확인하였다.
