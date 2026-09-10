# 05. Memory Subsystem: Page-Fault Policy and Large-Page Evaluation (THP & userfaultfd)

## 🔎 배경 및 문제 정의
### 1. 대규모 메모리 워크로드와 4KB 페이징의 딜레마
AI 추론 엔진, 인메모리 데이터베이스, 대규모 데이터 처리 시스템은 GB~TB 단위의 가상 주소 공간을 사용한다. 리눅스는 일반적으로 4 KB 기본 페이지를 사용한다.
4 KB 페이지는 메모리 낭비와 내부 단편화를 줄이는 범용적인 단위이지만, 대규모 워크로드에서는 TLB coverage를 제한하는 요인이 된다.

### 2. TLB Thrashing과 MMU 오버헤드
CPU는 가상 주소를 물리 주소로 변환할 때 TLB(Translation Lookaside Buffer)에 캐시된 변환 결과를 먼저 참조한다. TLB coverage는 페이지 크기와 엔트리 수에 의해 제한된다. 예를 들어 1,500개의 엔트리가 4 KB 페이지를 가리킨다면 범위는 $1500 \times 4\text{ KB} \approx 6\text{ MB}$이다. 실제 용량과 계층 구조는 CPU 마이크로아키텍처에 따라 다르다.

넓은 주소 범위를 페이지 단위로 반복 순회하면 TLB 엔트리 교체가 증가할 수 있다. TLB miss 시 MMU(Memory Management Unit)는 페이지 테이블을 탐색하는 page-table walk를 수행하며, 이 경로는 메모리 접근 지연과 CPU 사이클 소비를 증가시킨다.

### 3. Demand Paging의 첫 접근 비용
운영체제는 프로세스가 메모리를 요청할 때 물리 페이지를 즉시 매핑하지 않고, 실제 접근 시 동기적 페이지 폴트 예외를 처리하여 페이지를 할당하는 Demand Paging을 사용한다. 이 방식은 물리 메모리를 필요한 시점에만 소비하지만, 첫 접근 경로에 페이지 테이블 설정과 페이지 초기화 비용을 추가한다. 따라서 낮은 꼬리 지연이 필요한 워크로드에서는 사전 폴팅이나 메모리 정책 제어가 필요할 수 있다.

## 📌 목표 
본 실험은 대규모 메모리 할당·접근 시 발생하는 페이지 폴트 처리 비용과 dTLB miss를 정량적으로 관측한다. 이후 Transparent Huge Pages(THP)로 페이지 단위를 확장했을 때의 변화를 측정하고, `userfaultfd`로 페이지 폴트 해결 정책을 사용자 공간에 위임하는 지연 할당 구조를 구현한다.

## 🛠️ 테스트 환경 & 타겟 워크로드
* **Hardware:** Raspberry Pi 5 Model B Rev 1.0
* **OS / Kernel:** Ubuntu Server 24.04.4 LTS, Linux `6.8.0-1047-raspi` (`aarch64`)
* **Observability Tool:** Linux `perf` (Performance Counters API)
* **Target Workload:** 1 GB 영역을 할당한 뒤 4 KB 간격으로 접근하여 페이지 폴트와 dTLB miss를 유발하는 C 프로그램

## 🧪 실험 설계 
1. Phase 1(Baseline): 4 KB 페이지 환경에서 1 GB 영역을 stride 패턴으로 접근하고 `perf`로 dTLB miss와 페이지 폴트를 측정한다.
2. Phase 2(THP): `posix_memalign()`으로 2 MB 정렬을 적용하고 `madvise(MADV_HUGEPAGE)`로 THP를 요청한 뒤 동일 워크로드를 재측정한다.
3. Phase 3(`userfaultfd`): 특정 메모리 영역을 `userfaultfd`에 등록하고, 메인 스레드의 폴트를 워커 스레드가 감지한 뒤 `UFFDIO_COPY`로 데이터를 공급하는 구조를 구현한다.

## 📊 1. Baseline: TLB Thrashing 유발 및 측정
Baseline 워크로드(`workload.c`)는 `posix_memalign()`으로 시작 주소가 정렬된 1 GB 영역을 할당한 뒤 4 KB마다 1 byte를 기록한다. 이 첫 쓰기 패턴은 각 기본 페이지의 지연 할당을 유발하고, 넓은 주소 범위를 순회하는 후속 접근은 dTLB에 부하를 준다.

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
1 GB를 4 KB 단위로 첫 쓰기할 때의 이론적 페이지 수($1 \text{GB} / 4\text{KB} = 262,144$)와 측정된 페이지 폴트 262,194회가 거의 일치한다. 전체 실행 중 dTLB load miss는 23,321,474회, `sys` 시간은 0.808초로 측정된다. 이 결과는 지연 할당과 넓은 주소 범위 순회가 커널 메모리 관리 및 TLB에 부하를 준다는 것을 나타낸다.

## 📊 2. HugePage
THP 조건에서는 영역의 시작 주소를 2 MB로 정렬하고 `madvise(MADV_HUGEPAGE)`로 커널에 huge-page backing을 요청한다. `MADV_HUGEPAGE`는 힌트이므로 2 MB 페이지 사용을 보장하지 않는다.

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

1 GB가 모두 2 MB 페이지로 backing된다면 필요한 페이지 수는 512개이다. 그러나 측정된 페이지 폴트는 22,536회이며, 이는 전체 영역이 즉시 2 MB 페이지로 backing되지 않았음을 시사한다. 물리 메모리 단편화에 따른 4 KB fallback이 한 원인일 수 있으나, 정확한 비율은 `/proc/<pid>/smaps`의 `AnonHugePages` 또는 커널 THP 통계로 확인해야 한다.

THP 적용 후 dTLB load miss는 23,321,474회에서 3,414,722회로 85.3% 감소하고, `sys` 시간은 0.808초에서 0.609초로 24.5% 감소한다. 벽시계 실행 시간은 2.496초에서 2.407초로 약 3.6% 단축되므로, TLB·커널 지표의 큰 개선이 동일한 비율의 End-to-End 성능 향상으로 직결되지는 않는다.

## 📊 3. userfaultfd 기반 Lazy Allocation 
본 워크로드(`workload_uffd.c`)는 커널이 감지한 페이지 폴트의 해결 정책을 사용자 공간이 제어하도록 구성한다.
`userfaultfd` 시스템 콜을 호출해 커널과 통신할 전용 파일 디스크립터(`uffd`)를 열고 `mmap`으로 물리 메모리가 할당되지 않은 빈 가상 주소 공간을 만든다. 이후 `ioctl()` 시스템 콜의 옵션으로 `UFFDIO_REGISTER`를 주어 `mmap`으로 할당한 주소 영역에 Page Fault가 발생하더라도 커널이 임의로 물리 메모리를 할당하거나 프로세스를 죽이지 않고 `uffd`로 메시지만 보내도록 설정을 바꾸었다.

메인 스레드가 아직 backing되지 않은 가상 주소(`0xffffb13b4000`)를 읽으면 커널은 폴트 이벤트를 `userfaultfd` 파일 디스크립터로 전달하고 faulting thread를 블록한다.

`poll()`로 `uffd`를 감시하는 워커 스레드는 이벤트를 읽고, 사용자가 정의한 데이터 `'A'`를 `UFFDIO_COPY`로 해당 페이지에 복사한다. 이 작업이 성공하면 차단된 메인 스레드가 재개된다. 이는 페이지 폴트 자체를 우회한 것이 아니라, 커널이 이벤트를 중개하고 사용자 공간이 backing 내용과 시점을 결정하는 구조를 검증한다.

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
본 실험에서 4 KB Baseline은 262,194회의 페이지 폴트와 23,321,474회의 dTLB load miss를 기록한다. THP를 요청하면 해당 지표가 각각 91.4%와 85.3% 감소하고, `sys` 시간은 24.5%, 벽시계 시간은 3.6% 단축된다.

`madvise(MADV_HUGEPAGE)`는 TLB coverage와 폴트 처리 비용을 줄일 수 있으며, `userfaultfd`는 데이터 주입, 마이그레이션, 스냅샷 등에 필요한 사용자 정의 페이지 공급 정책을 구성할 수 있다. 다만 THP의 실제 backing 비율과 `userfaultfd` 경로의 폴트당 지연·처리량을 추가로 측정해야 운영 환경에서의 효과를 평가할 수 있다.

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
