#!/bin/bash

TARGET="./container_runtime_c/workload"

# 1. Build
echo "=== 1. C Program Build ==="
gcc -O2 -o $TARGET ./container_runtime_c/workload.c
echo "Build completed."
echo ""

# 2. Baseline measurement
echo "=== 2. Baseline Measurement ==="
sudo time -p $TARGET
echo ""

# 3. strace overhead measurement
echo "=== 3. strace Overhead Measurement ==="
sudo time -p strace -c $TARGET
echo ""

# 4. eBPF overhead measurement
sudo bpftrace -e 'tracepoint:syscalls:sys_enter_clone { @[probe] = count(); }' -c "$TARGET" > /dev/null &
BPF_PID=$!
sudo time -p $TARGET
sudo kill $BPF_PID 2>/dev/null
echo ""

echo "=== Benchmarking Completed ==="
