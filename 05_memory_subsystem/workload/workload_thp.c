#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>

#define SIZE (1ULL * 1024 * 1024 * 1024) // 1GB
#define ITERATIONS 100

int main() {
    long page_size = sysconf(_SC_PAGESIZE);
    printf("[Info] System Page Size: %ld Bytes\n", page_size);
    printf("[Info] Allocating 1GB of memory for THP...\n");

    uint8_t *data;
    size_t thp_alignment = 2 * 1024 * 1024; // 2MB for THP alignment

    if (posix_memalign((void**)&data, thp_alignment, SIZE) != 0) {
        perror("posix_memalign failed");
        return -1;
    }

    if (madvise(data, SIZE, MADV_HUGEPAGE) != 0) {
        perror("madvise failed");
    } else {
        printf("[Info] madvise(MADV_HUGEPAGE) applied successfully.\n");
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
