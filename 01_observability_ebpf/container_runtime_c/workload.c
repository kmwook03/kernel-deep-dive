#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/syscall.h>

#define ITERATIONS 10000

// Dummy function for clone()
int dummy_fn(void *arg) {
    return 0;
}

int main() {
    printf("[Target] Workload start (Iterations: %d)\n", ITERATIONS);

    // Memory allocation for child process (1MB)
    void *stack = malloc(1024 * 1024);
    if (!stack) {
        perror("malloc failed");
        exit(1);
    }
    void *stack_top = (char *)stack + 1024 * 1024;

    for (int i=0; i<ITERATIONS; i++) {
        pid_t pid = clone(dummy_fn, stack_top, CLONE_NEWNS | SIGCHLD, NULL);
        if (pid == -1) {
            perror("clone failed");
            free(stack);
            exit(1);
        }
        waitpid(pid, NULL, 0); // Wait for the child process to finish
    }
    free(stack);
    printf("[Target] Workload end\n");
    return 0;
}
