#include <uapi/linux/ptrace.h>

struct data_t {
    u64 duration;
};

BPF_HASH(start, u32, u64);
BPF_PERF_OUTPUT(events);

TRACEPOINT_PROBE(irq, irq_handler_entry)
{
    const u32 target_irq = TARGET_IRQ;
    if (args->irq != target_irq)
        return 0;
    
    u32 cpu = bpf_get_smp_processor_id();
    u64 ts = bpf_ktime_get_ns();

    start.update(&cpu, &ts);

    return 0;
}

TRACEPOINT_PROBE(irq, irq_handler_exit)
{
    const u32 target_irq = TARGET_IRQ;
    if (args->irq != target_irq)
        return 0;
    
    u32 cpu = bpf_get_smp_processor_id();
    u64 *tsp = start.lookup(&cpu);
    if (!tsp)
        return 0;   // missed entry

    struct data_t data = {};
    data.duration = bpf_ktime_get_ns() - *tsp;

    events.perf_submit(args, &data, sizeof(data));

    start.delete(&cpu);
    return 0;
}
