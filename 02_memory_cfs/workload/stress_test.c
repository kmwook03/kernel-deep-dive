#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#define NUM_THREADS 8          // occur context switch and runqueue latency
#define MEM_SIZE (1024 * 1024 * 512) // 512MB memory allocation to each thread (Page Fault occurrence)

void *cpu_memory_stress(void *arg) {
    int thread_id = *(int *)arg;
    printf("[Thread %d] Memory Allocation and CPU Stress Started\n", thread_id);

    // 1. Memory Stress (Page Fault occurrence)
    // malloc allocates only virtual memory, actually writing values (Page Fault occurrence) maps physical memory
    char *buffer = (char *)malloc(MEM_SIZE);
    if (buffer == NULL) {
        perror("malloc failed");
        pthread_exit(NULL);
    }

    // Occur Page Fault by randomly accessing the allocated memory (simulate demand paging)
    for (int i = 0; i < MEM_SIZE; i += 4096) { // 4KB Page Size
        buffer[i] = (char)(i % 256);
    }

    // 2. CPU Stress (Runqueue Latency and Context Switch occurrence)
    // Perform a CPU-intensive task to increase the likelihood of context switches and runqueue latency
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

    printf("=== CFS Scheduler & Page Fault Stress Test ===\n");

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
