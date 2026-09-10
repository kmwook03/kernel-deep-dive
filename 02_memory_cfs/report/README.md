# 02. Performance: CFS Scheduler & Page Fault Analysis

## 📌 목표
시스템의 CPU·메모리 사용률이 100%에 도달하지 않은 상태에서도 발생하는 꼬리 지연(Tail Latency)의 원인을 커널 레벨에서 추적한다.

`top`, `htop` 등의 집계형 모니터링 도구로는 포착하기 어려운 마이크로초(μs) 단위의 실행 대기열 지연(Runqueue Latency)과 페이지 폴트 처리 지연을 eBPF로 관측하고, CPU 친화도와 nice 값이 스케줄링에 미치는 영향을 검증한다.

## 🛠️ Test Environment & Target Workload
* **Target Workload:** 8개 스레드가 각각 512 MB를 동적 할당하고 무작위로 접근하여 페이지 폴트와 CPU 경합을 유발하는 C 프로그램(`stress_test.c`)을 사용한다.
* **Observability Tools:** `bpftrace`로 커널 트레이스포인트 `sched_switch`와 kprobe `handle_mm_fault`를 추적한다.

## 📊 1. CFS 스케줄러 분석 (Runqueue Latency)
스레드가 실행 대기열에 진입한 시점부터 CPU를 할당받는 시점까지의 지연을 측정한다.
* **Fast Path:** 관측치의 대부분이 0~32 μs 구간에 분포한다.
* **Tail Latency:** 경합 시 일부 스레드는 16~32 ms 구간까지 대기한다. 이 꼬리는 CFS의 공정성 정책과 실행 가능 태스크 간 경합이 결합한 결과로 해석한다.

## 📊 2. 메모리 서브시스템 분석 (Page Fault Latency)
`handle_mm_fault` 함수의 처리 시간은 1 μs 이하와 0.5~8 ms 구간에 집중된 쌍봉형(Bimodal) 분포를 보인다.
* **짧은 경로(1 μs 이하):** 대부분의 빠른 폴트 처리가 이 구간에 집중된다.
* **긴 경로(0.5~8 ms):** 메모리 압력 상태에서 일부 폴트의 처리 시간이 밀리초 단위로 증가한다. 현재 프로브는 `handle_mm_fault` 실행 시간만 측정하므로, 이 구간을 major fault·swap·compaction의 결과로 각각 분리해 단정할 수는 없다.

## 💡 결론
본 결과는 애플리케이션 코드 외에도 커널 스케줄러의 대기열 상태와 메모리 폴트 처리가 응답 지연의 꼬리에 기여함을 보인다.

따라서 지연 민감형 워크로드에서는 메모리 풀링, 사전 폴팅, CPU 친화도 등을 각각 평가하고 폴트와 스케줄링 경합을 통제할 필요가 있다.

<details>
<summary><b>터미널 출력</b></summary>
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

## 🔧 성능 튜닝 시도
앞서 관측한 16~32 ms 구간의 꼬리 지연은 CFS(Completely Fair Scheduler)의 공정성 정책 하에서 실행 가능 스레드가 CPU를 경쟁한 결과로 가정한다.

일반 시스템의 공정성과 달리, 일부 실시간 제어 작업은 특정 태스크의 마감 시간을 보장해야 한다. 이를 모사하기 위해 우선순위 차등화 실험을 수행한다.

`stress_test2.c`에서 Thread 0에만 실시간 스케줄링 정책인 `SCHED_FIFO` 또는 `SCHED_RR`을 적용하고 부하를 재측정하려 한다.

### 💥 문제 발생
```bash
kmwook@kmwookgram:~/kernel-deep-dive/02_memory_cfs$ sudo ./workload/stress_test2
=== CFS vs SCHED_FIFO Scheduling Test ===
Thread 0 (RT) failed to create - sudo permission required: Success
```

#### 문제 1. `failed`와 `Success` 동시 출력
오류 출력에 사용한 `perror()`는 전역 `errno` 값을 문자열로 변환한다. 반면 POSIX 스레드 함수는 일반적으로 오류 번호를 반환값으로 제공하며 `errno`를 설정하지 않는다.

따라서 `errno`가 `0`인 상태에서 `perror()`를 호출하여 실패 메시지와 `Success`가 동시에 출력되었다. 정확한 오류를 출력하려면 반환된 오류 번호를 `strerror()`에 전달해야 한다.

#### 문제 2. sudo를 사용했는데도 거부당함 (EPERM)
WSL2 및 일부 컨테이너 환경은 RT bandwidth 설정이나 capability 제한으로 `SCHED_FIFO`/`SCHED_RR` 적용을 거부할 수 있다. 본 환경에서는 관리자 권한으로 실행했음에도 `EPERM`이 반환되었다.

이러한 제한은 게스트나 컨테이너의 RT busy loop가 호스트의 CPU 기아를 유발하는 위험을 줄인다.

### 💡 대안 우회 전략 : CFS 내에서의 극단적 우선순위(Nice) 조작
실시간 정책을 적용할 수 없으므로, 실험 범위를 CFS 내 nice 값 차등화로 조정한다. 이 실험은 실시간 스케줄링을 대체하지 않으며, CFS 가중치의 효과를 관측하기 위한 대안이다.

1. **전략:** 특정 스레드(Thread 0)에는 커널이 허용하는 최고 우선순위인 `Nice -20`을 부여하고, 나머지 스레드들에는 최하 우선순위인 `Nice 19`를 부여(`setpriority` 시스템 콜 활용).
2. **실행 및 관측:** 다시 부하 테스트를 진행하며 eBPF로 스케줄링 양상을 관측.

### 📊 3. 스케줄링 제어 튜닝 결과 1
<details>
<summary><b>터미널 출력</b></summary>
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


Thread 0이 먼저 종료할 것으로 예상했으나 실제 종료 순서는 다르게 나타난다. 원인은 다음과 같이 분석한다.

#### 멀티 코어(Multi-core)
스케줄러 우선순위는 여러 실행 가능 스레드가 같은 CPU를 경쟁할 때 가장 뚜렷한 차이를 만든다.

실험 호스트는 Intel Core i5-1135G7을 탑재한 LG gram 360 2022 모델이며, 운영체제에 8개의 논리 CPU를 제공한다.

8개 스레드를 8개 논리 CPU에서 실행하면 각 스레드가 다른 CPU에서 동시에 실행될 수 있어 동일 실행 대기열의 경합이 줄어든다. 이로 인해 nice 값의 효과가 종료 순서에 명확하게 나타나지 않은 것으로 해석한다.

### 📊 4. 스케줄링 제어 튜닝 결과 2

#### 해결 방안 (`taskset -c 0`)

<details>
<summary><b>터미널 출력</b></summary>
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

eBPF 히스토그램의 꼬리는 `[1M, 2M)` 구간까지 확장된다. 모든 스레드를 하나의 CPU에 고정한 상태에서 nice -20 스레드와 nice 19 스레드가 경쟁하면, 낮은 가중치를 받은 일부 스레드의 대기 시간이 1초 이상으로 증가할 수 있음을 나타낸다.

## ❓추가 실험 : 단일 코어에서 CFS 동작 확인

멀티코어 조건에서 경합이 희석되는 영향을 제거하기 위해 `taskset -c 0`으로 모든 스레드를 단일 CPU에 고정하고, 동일한 CFS 정책에서 재측정한다.

<details>
<summary><b>터미널 출력</b></summary>
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

이전의 극단적 nice 차등화 실험에서는 일반 스레드의 대기 시간이 1~2초 구간까지 늘어났다. 반면 동일한 nice 값을 사용한 단일 CPU CFS 실험에서는 최대 관측 구간이 32~64 ms였다.

공정성은 대기 시간의 편중을 줄이지만, 특정 태스크의 마감 시간을 보장하는 실시간성과는 별개의 목표이다.

범용 워크로드에서는 CFS의 공정성이 처리량과 상호작용성을 균형 있게 제공한다. 반면 하드 리얼타임 시스템은 최악 응답 시간에 대한 보장이 필요하므로, CFS의 평균적 공정성만으로는 요구사항을 충족할 수 없다. 이러한 시스템은 실시간 스케줄링 정책, CPU 격리, 우선순위 역전 대응, 최악 실행 시간 분석을 함께 적용해야 한다.

## 💡 최종 결론

eBPF로 CFS와 페이지 폴트 경로를 관측하고 CPU 친화도·nice 값을 변경한 결과, 다음을 확인한다.

1. **마이크로초(us) 단위 관측(Observability)의 중요성**

   `top`이나 `htop`의 평균 사용률만으로는 마이크로초~밀리초 단위의 꼬리 지연을 식별하기 어렵다. eBPF 기반 이벤트 추적은 스케줄러 대기와 폴트 처리의 분포를 분리해 보여 준다.

2. **도메인에 따른 OS 자원 관리의 양면성**

   CFS의 공정성과 실시간 스케줄링의 마감 보장은 다른 설계 목표를 갖는다. 따라서 스케줄링 정책은 범용 처리량, 상호작용성, 최악 응답 시간 등 도메인의 요구사항에 따라 선택해야 한다.

3. **하드웨어 아키텍처와 커널의 유기적 이해**

   논리 CPU 수가 실행 스레드 수와 같은 조건에서는 nice 값의 효과가 경합 감소로 인해 희석된다. `taskset`으로 CPU 친화도를 제한한 실험은 우선순위 효과를 드러냈지만, 낮은 가중치의 스레드에 1초 이상의 대기를 발생시켰다. 즉 CPU 친화도와 우선순위는 독립적으로 해석할 수 없다.

**Next Step:** 다음 단계에서는 외부 트래픽을 `sk_buff` 기반 상위 네트워크 스택 진입 전에 필터링하는 XDP 방화벽을 연구한다.
