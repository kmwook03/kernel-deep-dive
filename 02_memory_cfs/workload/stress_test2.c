#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <sched.h> // Header for scheduling policies and parameters

#define NUM_THREADS 8
#define MEM_SIZE (1024 * 1024 * 512)

void *cpu_memory_stress(void *arg) {
    int thread_id = *(int *)arg;

    // Check the scheduling policy applied to the current thread
    int policy;
    struct sched_param param;
    pthread_getschedparam(pthread_self(), &policy, &param);
    
    if (policy == SCHED_FIFO) {
        printf("[Thread %d] 🚀 Real-time Scheduler (SCHED_FIFO) initialized (Priority: %d)\n", thread_id, param.sched_priority);
    } else {
        printf("[Thread %d] 🐢 General Scheduler (CFS) applied.\n", thread_id);
    }

    // 1. Memory Stress
    char *buffer = (char *)malloc(MEM_SIZE);
    if (buffer == NULL) {
        perror("malloc failed");
        pthread_exit(NULL);
    }

    for (int i = 0; i < MEM_SIZE; i += 4096) {
        buffer[i] = (char)(i % 256);
    }

    // 2. CPU Stress
    volatile double dummy = 0.0;
    for (long long i = 0; i < 5000000000LL; i++) {
        dummy += 1.000001;
    }

    free(buffer);
    printf("[Thread %d] Job Completed\n", thread_id);
    pthread_exit(NULL);
}

int main() {
    pthread_t threads[NUM_THREADS];
    int thread_ids[NUM_THREADS];
    
    // Scheduling attributes for real-time and normal threads
    pthread_attr_t attr_rt, attr_normal;
    struct sched_param param_rt;

    printf("=== CFS vs SCHED_FIFO Scheduling Test ===\n");

    // Initialize thread attributes
    pthread_attr_init(&attr_rt);
    pthread_attr_init(&attr_normal);

    // Configure real-time thread attributes
    pthread_attr_setinheritsched(&attr_rt, PTHREAD_EXPLICIT_SCHED); // Explicitly set scheduling policy for RT thread
    pthread_attr_setschedpolicy(&attr_rt, SCHED_FIFO);              // Set real-time scheduling policy (SCHED_FIFO)
    param_rt.sched_priority = 99;                                   // Kernel-allowed highest priority (1~99)
    pthread_attr_setschedparam(&attr_rt, &param_rt);

    for (int i = 0; i < NUM_THREADS; i++) {
        thread_ids[i] = i;
        if (i == 0) {
            // Create the first thread with real-time scheduling (SCHED_FIFO)
            int ret = pthread_create(&threads[i], &attr_rt, cpu_memory_stress, &thread_ids[i]);
            if (ret != 0) {
                perror("Thread 0 (RT) failed to create - sudo permission required");
                exit(1);
            }
        } else {
            // Create the remaining threads with normal scheduling (CFS)
            pthread_create(&threads[i], &attr_normal, cpu_memory_stress, &thread_ids[i]);
        }
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    pthread_attr_destroy(&attr_rt);
    pthread_attr_destroy(&attr_normal);

    printf("=== All Tests Completed ===\n");
    return 0;
}
