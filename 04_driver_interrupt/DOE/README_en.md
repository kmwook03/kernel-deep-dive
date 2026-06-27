## Interrupt

### What is Interrupt?
It is a system mechanism where, if an event requiring immediate attention occurs while a program is executing sequentially, the CPU suspends the currently running task and processes the event with priority.

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

1. An event occurs in the device.
2. The Interrupt Controller determines the IRQ number.

    * It collects interrupts from multiple devices, determines their priorities, and forwards them to the CPU.
3. The CPU halts the currently executing task.

    * At this point, the CPU saves the PC (Program Counter), registers, etc., onto the stack.
4. The CPU refers to the Interrupt Vector Table to find the ISR address corresponding to the IRQ number.
5. The ISR is executed.
    
    * The function registered by the driver is executed.
6. The ISR terminates.

    * The previously saved context is restored.

If a long-running task is executed within an ISR, the ISR occupies the CPU for an extended period, causing other interrupts to wait and delaying the overall system processing.
Therefore, ISRs must be kept short and fast.

### Solution of Linux
Linux uses the Top Half / Bottom Half mechanism to solve this problem.
The role of the Top Half is quick processing, saving necessary information, scheduling the Bottom Half, and terminating.

`Example`
```c
irqreturn_t irq_handler(...)
{
    save_event();

    queue_work(...);

    return IRQ_HANDLED;
}
```

Time-consuming tasks are executed in the Bottom Half.
While Linux provides several types of Bottom Halves, this project uses a Workqueue.
This is because it allows the use of sleeping functions, is the most versatile, and is widely used in device drivers.

The overall structure is as follows:
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
1. Executing heavy tasks within the ISR increases interrupt latency.
2. Separating tasks into Deferred Work using a Workqueue significantly reduces the execution time of the Top Half.
3. Using eBPF allows for quantitative measurement of ISR execution time, confirming the differences between the two designs.

The objective of this experiment is to quantitatively verify why Linux divides interrupt processing into a Top Half and a Bottom Half.
We will compare an intentionally inefficient Interrupt Handler with a Workqueue-based implementation, and quantify the impact of deferred work on interrupt latency through eBPF tracing.
