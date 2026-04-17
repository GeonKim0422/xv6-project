#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void test_nice_inheritance() {
    int parent_nice = 30;
    setnice(getpid(), parent_nice);

    int child = fork();
    if (child == 0) {
        int my_nice = getnice(getpid());
        printf("Child nice value: %d (expected: %d)\n", my_nice, parent_nice);
        if (my_nice == parent_nice)
            exit(0);
        else
            exit(1);
    } else {
        int status;
        wait(&status);
        if (status == 0)
            printf("[PASS] Nice value correctly inherited\n");
        else
            printf("[FAIL] Nice value not inherited (expected %d)\n", parent_nice);
    }
}

int main(int argc, char **argv) {
    printf("=== TEST: Nice Inheritance ===\n");
    test_nice_inheritance();
    exit(0);
}
