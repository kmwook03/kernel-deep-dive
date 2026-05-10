# 02. Performance: CFS Scheduler & Page Fault Analysis

## 📌 목표
시스템의 자원(CPU, Memory) 사용률이 100%가 아님에도 발생하는 **'꼬리 지연(Tail Latency)'**의 근본 원인을 커널 레벨에서 추적합니다.

일반적인 시스템 모니터링 툴(top, htop)로는 잡을 수 없는 마이크로초(us) 단위의 스케줄러 대기 시간(Runqueue Latency)과 메모리 할당 병목(Page Fault)을 eBPF로 관측하여, 실시간성이 중요한 환경에서의 성능 튜닝 포인트를 학습합니다.

## 🛠️ Test Environment & Target Workload
* **Target Workload:** 8개의 스레드가 각각 512MB의 메모리를 동적 할당받고, 무작위 접근을 통해 고의적인 Page Fault와 CPU 경합을 유발하는 C 프로그램 (`stress_test.c`).
* **Observability Tools:** `bpftrace`를 이용한 커널 트레이스포인트(`sched_switch`) 및 kprobe(`handle_mm_fault`) 훅킹.

## 📊 1. CFS 스케줄러 분석 (Runqueue Latency)
스레드가 실행 대기열(Runqueue)에 진입한 후 실제로 CPU를 할당받기까지의 대기 시간을 측정했습니다.
* **Fast Path:** 전체의 약 95% 이상은 0~32us 이내에 초고속으로 스케줄링 되었습니다.
* **Tail Latency (꼬리 지연):** 스레드 경합이 심해지자 CFS가 공정성(Fairness)을 맞추기 위해 강제 컨텍스트 스위칭을 발생시켰고, 이로 인해 **최대 32ms (32,000us)** 동안 CPU를 할당받지 못하고 대기하는 치명적인 꼬리 지연 구간을 관측했습니다.

## 📊 2. 메모리 서브시스템 분석 (Page Fault Latency)
가상 메모리에 물리 메모리가 매핑되는 `handle_mm_fault` 함수의 처리 시간을 측정하여 명확한 **쌍봉형(Bimodal) 분포**를 확인했습니다.
* **Minor Page Fault (1us 이하):** 여유 메모리 공간을 즉시 할당받는 경우, 1us 이하의 속도로 매우 빠르게 처리되었습니다.
* **Major Page Fault / Compaction (0.5ms ~ 8ms):** 시스템 메모리 압박이 심해져 커널이 물리 메모리를 확보(Swap, Compaction 등)해야 하는 상황에서 지연 시간이 **최대 4,000배(4ms 이상) 폭증**하는 병목 구간이 형성되었습니다.

## 💡 결론
이 실험을 통해 애플리케이션 코드 레벨에서의 최적화뿐만 아니라, **OS 커널의 스케줄링 큐와 메모리 파편화 상태**가 시스템 응답 속도에 결정적인 영향을 미친다는 것을 확인했습니다.

향후 고성능 시스템 아키텍처를 설계할 때, 메모리 풀링(Memory Pooling) 기법이나 스레드 친화도(CPU Affinity) 조절을 통해 커널의 개입(Hard Page Fault, Context Switch)을 최소화해야 함을 깊이 체감했습니다.

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
앞서 관측한 32ms의 꼬리 지연은 CFS(Completely Fair Scheduler)가 모든 스레드에 CPU를 공평하게 나눠주려다가 발생한 것으로 생각됩니다.

하지만 미사일 요격 시스템이나 자율 주행 브레이크 제어는 공평함이 필요 없는 최우선 작업입니다. 즉, 무조건적인 최우선 실행이 필요합니다.

이 문제에 대해 연구하기 위해 기존의 `stress_test.c` 코드에서 특정 스레드(예: Thread 0)만 리눅스의 실시간 스케줄러 정책인 `SCHED_FIFO` 또는 `SCHED_RR`로 권한을 격상시킨 후 다시 부하 측정을 진행해보았습니다. (`stress_test2.c`)

### 💥 문제 발생
```bash
kmwook@kmwookgram:~/kernel-deep-dive/02_memory_cfs$ sudo ./workload/stress_test2
=== CFS vs SCHED_FIFO Scheduling Test ===
Thread 0 (RT) failed to create - sudo permission required: Success
```

#### 문제 1. `failed`와 `Success` 동시 출력
에러 출력에 사용한 `perror()` 함수는 전역 변수인 `errno` 값을 읽어서 문자로 바꿔줍니다. 하지만 `pthread` 라이브러리 함수들은 에러가 나도 `errno`를 세팅하지 않고 함수 반환값으로 에러 코드를 직접 뱉습니다.

따라서 `errno`는 여전히 `0`(성공)인 상태였고, `perror()`는 `errno`만 읽고 `Success`라고 출력한 것이었습니다.

#### 문제 2. sudo를 사용했는데도 거부당함 (EPERM)
WSL2 및 많은 컨테이너 환경은 호스트 안정성을 위해 cgroup의 RT bandwidth를 제한하거나 0으로 설정하여 `SCHED_FIFO/SCHED_RR` 같은 real-time scheduling 사용을 기본적으로 차단합니다.

이는 `guest/container` 내부의 RT busy loop가 CPU starvation을 일으키는 것을 방지하기 위함입니다.

### 💡 대안 우회 전략 : CFS 내에서의 극단적 우선순위(Nice) 조작
WSL2의 커널(Cgroup) 제약을 우회하기 위해 커널을 재빌드하는 대신, **현재 허용된 CFS 스케줄러 환경 내에서 프로세스 우선순위(Nice value)를 극한으로 조작**하여 VIP 스레드를 보호할 수 있는지 실험 방향을 수정했습니다.

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


Thread 0이 먼저 수행되어 종료되기를 기대했으나 예상대로 나오지 않았습니다. 원인은 다음과 같이 분석됩니다.

#### 멀티 코어(Multi-core)
스케줄러 우선 순위(nice value)는 여러 스레드가 하나의 CPU 코어를 차지하려고 싸울 때(Contention)만 의미가 있습니다.

제가 연구에 사용한 노트북은 LG gram 360 2022모델로 `11th Gen Intel(R) Core(TM) i5-1135G7 @ 2.40GHz(2.42 GHz)` CPU가 탑재되어있습니다. 

인텔의 하이퍼스레딩 기술 덕분에 OS 입장에서는 8개의 논리 코어로 인식되므로 스레드 하나가 하나의 코어를 사용하는 성능을 보이게 됩니다. 따라서 대기열 자체가 생기지 않기 때문에 시나리오대로 실행되지 않았습니다.

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

eBPF 히스토그램을 살펴보면 앞서 살펴본 결과와 달리 꼬리가 `[1M, 2M)` 만큼 길어진 것을 확인했습니다. 아까는 멀티 코어 상태에서 테스트 했으나, 이번에는 싱글 코어로 진행했기 때문에 우선순위 스케줄링에 따라 끝까지 CPU 할당을 못받은 스레드가 대기열에서 1초 이상 굶은 것입니다.

## ❓추가 실험 : 단일 코어에서 CFS 동작 확인

첫 실험은 멀티 코어 환경에서만 CFS에 따라 부하를 주었기 때문에 이번에는 코어 수를 하나로 줄여서 다시 시도해보았습니다.

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

이전 VIP 테스트에서는 일반 스레드들이 1~2초 동안 한 번도 실행되지 못하는 기아 상태에 빠졌습니다. 하지만 CFS 테스트의 히스토그램을 보면 가장 늦게 기다린 시간이 32ms~64ms로 짧습니다.

이렇게 꼬리 지연이 짧은 알고리즘이 좋아보일 수 있지만, 이는 도메인에 따라 다르다고 봅니다.

범용 시스템(웹 서버, 데스크탑 등)에서는 당연히 CFS가 압도적으로 좋습니다. 하지만 하드 리얼타임 시스템(방산 레이더, 자율 주행, 심박 조율기 등)에서 CFS는 생명을 위협할 수 있는 위험한 선택입니다. 미사일이 날아오고 있는데 '공평함'을 위해서 미사일 요격 시스템을 대기열에 넣어버리고 파일 다운로드 스레드 따위를 실행 시킨다면 치명적인 결과가 발생할 것입니다.

## 💡 최종 결론

리눅스 커널의 스케줄러(CFS)와 메모리 서브시스템(Page Fault)의 동작을 eBPF로 직접 훅킹하며 튜닝해 본 결과 다음과 같은 통찰을 얻을 수 있었습니다.

1. **마이크로초(us) 단위 관측(Observability)의 중요성**

   `top`이나 `htop` 같은 유저 스페이스 도구로는 CPU 사용률이 낮아 보여도 시스템 내부에 치명적인 병목이 숨어있을 수 있음을 확인했습니다. 밀리초(ms)에서 마이크로초(us) 단위의 꼬리 지연(Tail Latency)과 CPU 기아(Starvation) 상태를 잡아내기 위해서는 eBPF와 같은 커널 레벨의 동적 추적 기술이 필수적입니다.

2. **도메인에 따른 OS 자원 관리의 양면성**

   스케줄러에 '절대적으로 완벽한 알고리즘'은 존재하지 않음을 증명했습니다. 웹 서버와 같은 일반적인 환경에서는 기아 상태를 방지하는 CFS의 '공평함'이 훌륭하게 작동하지만, **미사일 요격 레이더 시스템이나 실시간 AI 추론 인프라**처럼 하드 리얼타임(Hard Real-time)이 요구되는 도메인에서는 이 공평함이 도리어 치명적인 지연을 유발합니다. 목적에 맞춰 OS의 정책을 비틀고 제어할 줄 아는 능력이 중요함을 체감했습니다.

3. **하드웨어 아키텍처와 커널의 유기적 이해**

   스케줄링 우선순위(Nice)를 조작하더라도 하이퍼스레딩 기반의 멀티 코어 환경에서는 그 효과가 희석된다는 점을 마주했습니다. 소프트웨어적 우선순위 튜닝은 반드시 `taskset`과 같은 **CPU Affinity(코어 친화도)** 제어를 통해 하드웨어 자원의 물리적 격리가 동반되어야만 진정한 위력을 발휘한다는 것을 실증적으로 확인했습니다.

**🚀 Next Step:** 커널 내부의 스케줄링과 메모리 복사(Page Fault)가 유발하는 오버헤드를 깊이 이해했으므로, 다음 단계에서는 외부에서 유입되는 대규모 네트워크 트래픽을 커널의 네트워크 스택(`sk_buff`)에 도달하기 전에 NIC 드라이버 레벨에서 직접 차단해버리는 **XDP(eXpress Data Path) 기반 Zero-copy 방화벽 아키텍처** 연구로 확장해 나갈 계획입니다.
