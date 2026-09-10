# 05. Memory Subsystem: Page-Fault Policy and Large-Page Evaluation (THP & userfaultfd)

## ❓ 배경 및 문제 정의
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
 Performance counter stats for './workload':

        1197721123      dTLB-loads                                                            
          23321474      dTLB-load-misses                 #    1.95% of all dTLB cache accesses
            262194      page-faults                                                           

       2.496188984 seconds time elapsed

       1.681575000 seconds user
       0.807835000 seconds sys
```
1 GB를 4 KB 단위로 첫 쓰기할 때의 이론적 페이지 수($1 \text{GB} / 4\text{KB} = 262,144$)와 측정된 페이지 폴트 262,194회가 거의 일치한다. 전체 실행 중 dTLB load miss는 23,321,474회, `sys` 시간은 0.808초로 측정된다. 이 결과는 지연 할당과 넓은 주소 범위 순회가 커널 메모리 관리 및 TLB에 부하를 준다는 것을 나타낸다.

## 📊 2. HugePage
THP 조건에서는 영역의 시작 주소를 2 MB로 정렬하고 `madvise(MADV_HUGEPAGE)`로 커널에 huge-page backing을 요청한다. `MADV_HUGEPAGE`는 힌트이므로 2 MB 페이지 사용을 보장하지 않는다.

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

1 GB가 모두 2 MB 페이지로 backing된다면 필요한 페이지 수는 512개이다. 그러나 측정된 페이지 폴트는 22,536회이며, 이는 전체 영역이 즉시 2 MB 페이지로 backing되지 않았음을 시사한다. 물리 메모리 단편화에 따른 4 KB fallback이 한 원인일 수 있으나, 정확한 비율은 `/proc/<pid>/smaps`의 `AnonHugePages` 또는 커널 THP 통계로 확인해야 한다.

THP 적용 후 dTLB load miss는 23,321,474회에서 3,414,722회로 85.3% 감소하고, `sys` 시간은 0.808초에서 0.609초로 24.5% 감소한다. 벽시계 실행 시간은 2.496초에서 2.407초로 약 3.6% 단축되므로, TLB·커널 지표의 큰 개선이 동일한 비율의 End-to-End 성능 향상으로 직결되지는 않는다.

## 📊 3. userfaultfd 기반 Lazy Allocation 
본 워크로드(`workload_uffd.c`)는 커널이 감지한 페이지 폴트의 해결 정책을 사용자 공간이 제어하도록 구성한다.
`userfaultfd` 시스템 콜을 호출해 커널과 통신할 전용 파일 디스크립터(`uffd`)를 열고 `mmap`으로 물리 메모리가 할당되지 않은 빈 가상 주소 공간을 만든다. 이후 `ioctl()` 시스템 콜의 옵션으로 `UFFDIO_REGISTER`를 주어 `mmap`으로 할당한 주소 영역에 Page Fault가 발생하더라도 커널이 임의로 물리 메모리를 할당하거나 프로세스를 죽이지 않고 `uffd`로 메시지만 보내도록 설정을 바꾸었다.

메인 스레드가 아직 backing되지 않은 가상 주소(`0xffffb13b4000`)를 읽으면 커널은 폴트 이벤트를 `userfaultfd` 파일 디스크립터로 전달하고 faulting thread를 블록한다.

`poll()`로 `uffd`를 감시하는 워커 스레드는 이벤트를 읽고, 사용자가 정의한 데이터 `'A'`를 `UFFDIO_COPY`로 해당 페이지에 복사한다. 이 작업이 성공하면 차단된 메인 스레드가 재개된다. 이는 페이지 폴트 자체를 우회한 것이 아니라, 커널이 이벤트를 중개하고 사용자 공간이 backing 내용과 시점을 결정하는 구조를 검증한다.



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
