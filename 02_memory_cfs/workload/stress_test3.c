#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/syscall.h>

#define NUM_THREADS 8
#define MEM_SIZE (1024 * 1024 * 512)

void *cpu_memory_stress(void *arg) {
    int thread_id = *(int *)arg;
    
    // Get thread ID(TID) for setting priority
    pid_t tid = syscall(SYS_gettid);

    if (thread_id == 0) {
        // Thread 0: Highest Priority (Nice -20)
        setpriority(PRIO_PROCESS, tid, -20);
        printf("[Thread %d] 🚀 VIP Thread (Nice: -20)\n", thread_id);
    } else {
        // 나머지: Lowest Priority (Nice 19)
        setpriority(PRIO_PROCESS, tid, 19);
        printf("[Thread %d] 🐢 Normal Thread (Nice: 19)\n", thread_id);
    }

    // 1. Memory Stress
    char *buffer = (char *)malloc(MEM_SIZE);
    if (buffer) {
        for (int i = 0; i < MEM_SIZE; i += 4096) buffer[i] = (char)(i % 256);
    }

    // 2. CPU Stress
    volatile double dummy = 0.0;
    for (long long i = 0; i < 5000000000LL; i++) dummy += 1.000001;

    if (buffer) free(buffer);
    printf("[Thread %d] Job Completed\n", thread_id);
    pthread_exit(NULL);
}

int main() {
    pthread_t threads[NUM_THREADS];
    int thread_ids[NUM_THREADS];

    printf("=== CFS Extreme Priority (Nice) Test ===\n");

    for (int i = 0; i < NUM_THREADS; i++) {
        thread_ids[i] = i;
        pthread_create(&threads[i], NULL, cpu_memory_stress, &thread_ids[i]);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("=== All Tests Completed ===\n");
    return 0;
}