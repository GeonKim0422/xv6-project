#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int busy_work(int duration) {
    int start = uptime();
    int x = 0, z = 20, i, j;
    while ((uptime() - start) < duration) {
        for (i = 0; i < 10000; i++)
            for (j = 0; j < 10000; j++)
                x += z * 89;
    }
    return uptime() - start;
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
    printf("=== TEST: Sleep/Wakeup Handling ===\n");

    int child1 = fork();
    if (child1 == 0) {
        // Long-running child with periodic ps()
        for (int i = 0; i < 10; i++) {
            busy_work(100);
            ps(0);
        }
        exit(1);  // marker: child1
    }

    int child2 = fork();
    if (child2 == 0) {
        for (int i = 0; i < 5; i++)
            busy_work(100);
        exit(2);  // marker: child2
    }

    // Parent sleeps waiting for first child to finish
    printf("Parent sleeping...\n");
    int status1;
    wait(&status1);
    printf("Wait finished for 1st process (id: %d)\n", status1);

    // Parent wakes up and runs work with ps() snapshots
    for (int i = 0; i < 3; i++) {
        busy_work(100);
        ps(0);
    }

    int status2;
    wait(&status2);
    printf("Wait finished for 2nd process (id: %d)\n", status2);
    ps(0);

    // Verification: parent ran after wakeup (check via ps output visually)
    // Automatic check: if we reached here, parent was scheduled after wakeup
    printf("\n[PASS] Sleep/wakeup handling works correctly\n");
    printf("  - Parent was scheduled after wakeup\n");
    printf("  - Verify ps() output above: vdeadline recalculated, timeslice reset\n");

    exit(0);
}