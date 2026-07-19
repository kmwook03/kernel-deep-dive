#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <linux/userfaultfd.h>

long page_size;

void *fault_handler_thread(void *arg) {
    int uffd = *(int *)arg;
    struct uffd_msg msg;
    struct pollfd pollfd = { .fd = uffd, .events = POLLIN };

    printf("[Worker] Ready to handle Page Faults...\n");

    while (poll(&pollfd, 1, -1) > 0) {
        // Read an event message from the kernel
        if (read(uffd, &msg, sizeof(msg)) == 0) {
            continue;
        }

        if (msg.event != UFFD_EVENT_PAGEFAULT) {
            fprintf(stderr, "[Worker] Unexpected event\n");
            continue;
        }

        // Address of the faulting page
        void *fault_addr = (void *)msg.arg.pagefault.address;
        printf("[Worker] Page Fault detected at %p! Fetching data...\n", fault_addr);

        // Simulation: Get data from disk and network
        char *temp_page = malloc(page_size);
        memset(temp_page, 'A', page_size); // Simulate data fetch

        // UFFDIO_COPY: Copy temp_page data to the faulting address 
        // and wake up the faulting thread
        struct uffdio_copy uffdio_copy;
        uffdio_copy.src = (unsigned long)temp_page;
        uffdio_copy.dst = (unsigned long)fault_addr;
        uffdio_copy.len = page_size;
        uffdio_copy.mode = 0;
        uffdio_copy.copy = 0;

        if (ioctl(uffd, UFFDIO_COPY, &uffdio_copy) == -1) {
            perror("ioctl(UFFDIO_COPY) failed");
            exit(1);
        }

        printf("[Worker] Page filled and Main Thread awakened.\n");
        free(temp_page);
    }
    return NULL;
}

int main() {
    page_size = sysconf(_SC_PAGESIZE);
    size_t alloc_size = page_size * 4; // test 4 pages

    // 1. create userfaultfd
    int uffd = syscall(SYS_userfaultfd, O_CLOEXEC | O_NONBLOCK);
    if (uffd == -1) {
        perror("syscall(SYS_userfaultfd) failed (Try running with sudo)");
        return 1;
    }

    struct uffdio_api uffdio_api = { .api = UFFD_API, .features = 0 };
    if (ioctl(uffd, UFFDIO_API, &uffdio_api) == -1) {
        perror("ioctl(UFFDIO_API) failed");
        return 1;
    }

    // 2. Allocate memory region with mmap
    void *addr = mmap(NULL, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED) {
        perror("mmap failed");
        return 1;
    }

    // 3. Register the memory region with userfaultfd (Monitoring for missing pages)
    struct uffdio_register uffdio_register;
    uffdio_register.range.start = (unsigned long)addr;
    uffdio_register.range.len = alloc_size;
    uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING;
    if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1) {
        perror("ioctl(UFFDIO_REGISTER) failed");
        return 1;
    }

    // 4. Create worker thread to handle page faults
    pthread_t thread;
    pthread_create(&thread, NULL, fault_handler_thread, &uffd);

    printf("[Main] Triggering memory accesses...\n");

    // 5. if main thread accesses the memory region, it will trigger a page fault
    for (size_t i = 0; i < alloc_size; i += page_size) {
        printf("\n[Main] Reading address %p...\n", addr + i);
        // main thread will block here until the worker thread handles the page fault
        char c = *((char *)(addr + i));
        printf("[Main] Data read success: '%c'\n", c);
    }

    return 0;
}
