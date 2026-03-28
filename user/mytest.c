#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int passed = 0;
int failed = 0;

void check(const char *test, int actual, int expected)
{
  if (actual == expected) {
    printf("[PASS] %s : got %d\n", test, actual);
    passed++;
  } else {
    printf("[FAIL] %s : expected %d, got %d\n", test, expected, actual);
    failed++;
  }
}

int main()
{
  int ret;

  printf("[INFO] init(pid 1) and sh(pid 2) are created during boot.\n");
  printf("[INFO] mytest(pid 3) should be the current test program.\n");

  // ============================================================
  printf("========== Current Process State ==========\n");
  // ============================================================
  printf("[INFO] The following processes are already running:\n");
  ps(0);
  printf("\n");

  // ============================================================
  printf("========== Testing getnice() ==========\n");
  // ============================================================

  // pid 1 (init) exists, default nice = 20
  ret = getnice(1);
  check("getnice(1) - init default nice", ret, 20);

  // pid 2 (sh) exists, default nice = 20
  ret = getnice(2);
  check("getnice(2) - sh default nice", ret, 20);

  // pid 12 does not exist → should return -1
  ret = getnice(12);
  check("getnice(12) - no such process", ret, -1);

  printf("\n");

  // ============================================================
  printf("========== Testing setnice() ==========\n");
  // ============================================================

  // Set nice of pid 1 (init) to 10 → should succeed (return 0)
  ret = setnice(1, 10);
  check("setnice(1, 10) - valid", ret, 0);

  // Verify the change
  ret = getnice(1);
  check("getnice(1) - after setnice to 10", ret, 10);

  // Restore to default
  ret = setnice(1, 20);
  check("setnice(1, 20) - restore default", ret, 0);

  // pid 12 does not exist → should return -1
  ret = setnice(12, 10);
  check("setnice(12, 10) - no such process", ret, -1);

  // nice value 40 is out of range (valid: 0–39) → should return -1
  ret = setnice(1, 40);
  check("setnice(1, 40) - invalid nice value", ret, -1);

  // nice value -1 is out of range → should return -1
  ret = setnice(1, -1);
  check("setnice(1, -1) - negative nice value", ret, -1);

  printf("\n");

  // ============================================================
  printf("========== Testing ps() ==========\n");
  // ============================================================

  // ps(0) prints all processes — verify visually
  printf("[INFO] ps(0) - should print all processes (init, sh, mytest):\n");
  ps(0);
  printf("\n");

  // ps(1) prints only init
  printf("[INFO] ps(1) - should print only init:\n");
  ps(1);
  printf("\n");

  // ps(12) — no such process, should print nothing
  printf("[INFO] ps(12) - no such process, should print nothing:\n");
  ps(12);
  printf("[INFO] (no output expected above)\n");

  printf("\n");

  // ============================================================
  printf("========== Testing meminfo() ==========\n");
  // ============================================================

  uint64 mem = meminfo();
  if (mem > 0) {
    printf("[PASS] meminfo() - free memory: %lu bytes\n", mem);
    passed++;
  } else {
    printf("[FAIL] meminfo() - expected > 0, got %lu\n", mem);
    failed++;
  }

  printf("\n");

  // ============================================================
  printf("========== Testing waitpid() ==========\n");
  // ============================================================
  // Note: waitpid suspends execution until a specific process terminates.
  // To test this, we use fork() to create child processes that exit immediately,
  // then verify that waitpid correctly waits for them.

  // Test 1: fork a child, then waitpid for it
  int pid1 = fork();
  if (pid1 < 0) {
    printf("[FAIL] fork() failed\n");
    failed++;
  } else if (pid1 == 0) {
    // child process — exit immediately
    exit(0);
  } else {
    // parent — wait for pid1 to terminate
    ret = waitpid(pid1);
    check("waitpid(pid1) - child exited", ret, 0);
  }

  // Test 2: fork another child, waitpid for it
  int pid2 = fork();
  if (pid2 < 0) {
    printf("[FAIL] fork() failed\n");
    failed++;
  } else if (pid2 == 0) {
    // child process — exit immediately
    exit(0);
  } else {
    // parent — wait for pid2 to terminate
    ret = waitpid(pid2);
    check("waitpid(pid2) - child exited", ret, 0);
  }

  // Test 3: waitpid for init (pid 1) — should fail
  // (init never exits / caller is not init's parent)
  ret = waitpid(1);
  check("waitpid(1) - not our child", ret, -1);

  // Test 4: waitpid for non-existent pid — should fail
  ret = waitpid(99);
  check("waitpid(99) - no such process", ret, -1);

  printf("\n");

  // ============================================================
  printf("========== Summary ==========\n");
  // ============================================================
  printf("Total: %d passed, %d failed\n", passed, failed);

  exit(0);
}