## Interrupt

### What is Interrupt?
프로그램이 순차적으로 실행되던 중 CPU가 즉시 처리해야할 이벤트가 발생하면 현재 실행 중인 작업을 중단하고 해당 이벤트를 우선적으로 처리하는 시스템 메커니즘입니다.

### Cycle
```
          Device
            ↓
       Interrupt Line
            ↓
    Interrupt Controller (GIC/APIC)
            ↓
           CPU
            ↓
    Current Context Save
            ↓
    Interrupt Vector
            ↓
ISR (Interrupt Service Routine)
            ↓
    Return From Interrupt
            ↓
    Original Program Resume
```

1. 장치에서 이벤트 발생
2. Interrupt Controller가 IRQ 번호 결정

    * 여러 장치에서 발생한 Interrupt 수집, 우선순위를 결정하여 CPU에 전달
3. CPU는 현재 실행 중인 작업 멈춤

    * 이 때 CPU는 PC, Register 등을 스택에 저장
4. Interrupt Vector Table 참조하여 IRQ 번호에 대응되는 ISR 주소 탐색
5. ISR 실행
    
    * 드라이버가 등록한 함수가 실행됨
6. ISR 종료

    * 아까 저장했던 Context 복원

ISR 안에서 실행 시간이 긴 작업을 수행하면 해당 ISR이 CPU를 오래 점유하면서 다른 Interrupt도 대기하게 되고 시스템 전체의 처리를 지연시킵니다.
따라서 우리는 ISR을 짧고 빠르게 작성해야합니다.

### Solution of Linux
Linux는 이 문제를 해결하기 위해 Top Half를 사용합니다.
Top Half의 역할은 빠른 처리, 필요한 정보 저장, Bottom Half 예약, 종료입니다.

`예시`
```c
irqreturn_t irq_handler(...)
{
    save_event();

    queue_work(...);

    return IRQ_HANDLED;
}
```

시간이 오래 걸리는 작업은 Bottom Half에서 수행합니다.
Linux에는 여러 종류의 Bottom Half가 있지만, 이번 프로젝트에는 Workqueue를 사용합니다.
Sleep 명령을 사용할 수 있고 가장 범용적이며 Driver에서 가장 많이 사용하기 때문입니다.

전체 구조는 아래와같습니다.

```
    Interrupt
        ↓
    ISR (Top Half)
        ↓
    queue_work()
        ↓
    Return
        ↓
    Worker Thread
        ↓
    Heavy Processing
```

### Project Hypothesis
1. ISR에서 무거운 작업을 수행하면 Interrupt Latency가 증가한다.
2. Workqueue를 이용해 Deferred Work로 분리하면 Top Half 실행 시간이 크게 감소한다.
3. eBPF를 이용하면 ISR 실행 시간을 정량적으로 측정하여 두 설계의 차이를 확인할 수 있다.

이 실험의 목적은 Linux가 인터럽트 처리를 Top Half와 Bottom Half로 구분하는 이유를 수치적으로 확인하는 것입니다.
의도적으로 비효율적인 Interrupt Handler와 Workqueue 기반 구현을 비교하고, eBPF tracing을 통해 지연된 작업이 Interrupt Latency에 미치는 영향을 수치화합니다.
