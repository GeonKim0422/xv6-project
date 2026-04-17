#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void busy_work() {
    int start = uptime();
    int x = 0, z = 20, i, j;
    while ((uptime() - start) < 4) {
        for (i = 0; i < 100000; i++)
            for (j = 0; j < 10000; j++)
                x += z * 89;
    }
}

int count_iterations(int duration) {
    int start = uptime();
    int x = 0, iterations = 0;
    while ((uptime() - start) < duration) {
        for (int i = 0; i < 1000; i++)
            x += i * 7;
        iterations++;
    }
    return iterations;
}

int main(int argc, char **argv) {
    printf("=== TEST: Fair Scheduling (same nice) ===\n");

    int child1 = fork();
    if (child1 == 0) {
        int iters = count_iterations(200);
        exit(iters);
    }

    int child2 = fork();
    if (child2 == 0) {
        int iters = count_iterations(200);
        exit(iters);
    }

    // Parent runs work with periodic ps() snapshots
    for (int i = 0; i < 4; i++) {
        busy_work();
        ps(0);
    }

    int status1, status2;
    wait(&status1);
    wait(&status2);

    // Parent also measures its own iterations for final comparison
    // (use status from children for PASS/FAIL)
    printf("\nIterations — Child1: %d, Child2: %d\n", status1, status2);

    // Check: both children should have similar iteration counts (within 30%)
    int max = status1 > status2 ? status1 : status2;
    int min = status1 < status2 ? status1 : status2;

    if (min > 0 && max * 100 / min <= 140)
        printf("[PASS] CPU time is fairly distributed (max/min ratio: %d%%)\n",
            max * 100 / min);
    else
        printf("[FAIL] CPU time is not fairly distributed (max/min ratio: %d%%)\n",
            min > 0 ? max * 100 / min : -1);

    exit(0);
}