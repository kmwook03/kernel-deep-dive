#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#define SIZE (1ULL * 1024 * 1024 * 1024) // 1GB
#define ITERATIONS 100

int main() {
    long page_size = sysconf(_SC_PAGESIZE);
    printf("[Info] System Page Size: %ld Bytes\n", page_size);
    printf("[Info] Allocating 1GB of memory...\n");

    uint8_t *data;
    if (posix_memalign((void**)&data, page_size, SIZE) != 0) {
        perror("posix_memalign failed");
        return -1;
    }

    printf("[Info] Starting TLB thrashing (Stride: %ld Bytes)...\n", page_size);

    // TLB Thrashing Loop
    for (int i = 0; i < ITERATIONS; i++) {
        // Jump Page Size for break TLB locality
        for (uint64_t j = 0; j < SIZE; j += page_size) {
            // Make Dirty
            data[j] = (uint8_t)(i & 0xFF); // Write to each page
        }
    }

    printf("[Info] Memory access complete.\n");
    free(data);

    return 0;
}
