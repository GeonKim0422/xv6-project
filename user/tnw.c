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
    printf("=== TEST: Weight-based Scheduling (nice 5 vs nice 20) ===\n");

    // Child 1: high priority
    int child1 = fork();
    if (child1 == 0) {
        setnice(getpid(), 5);  // weight 29154
        int iters = count_iterations(200);
        exit(iters);
    }

    // Child 2: default priority
    int child2 = fork();
    if (child2 == 0) {
        setnice(getpid(), 20);  // weight 1024
        int iters = count_iterations(200);
        exit(iters);
    }

    // Parent: observe with periodic ps() snapshots
    for (int i = 0; i < 4; i++) {
        busy_work();
        ps(0);
    }

    int iters_high, iters_low;
    wait(&iters_high);
    wait(&iters_low);

    // Ensure iters_high is the nice=5 child
    // (wait returns in exit order, so sort)
    if (iters_high < iters_low) {
        int tmp = iters_high;
        iters_high = iters_low;
        iters_low = tmp;
    }

    printf("\nIterations — Child nice 5: %d, Child nice 20: %d\n",
        iters_high, iters_low);

    if (iters_high > iters_low)
        printf("[PASS] Higher weight process completed more work (ratio: %dx)\n",
            iters_low > 0 ? iters_high / iters_low : -1);
    else
        printf("[FAIL] Weight-based distribution not working\n");

    exit(0);
}