#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Claude AI was used and implemented in project2
// Nice-to-weight mapping table (Linux-style, nice 0~39)
static uint64 nice_to_weight[40] = {
  88761, 71755, 56483, 46273, 36291,  // nice  0 ~  4
  29154, 23254, 18705, 14949, 11916,  // nice  5 ~  9
   9548,  7620,  6100,  4904,  3906,  // nice 10 ~ 14
   3121,  2501,  1991,  1586,  1277,  // nice 15 ~ 19
   1024,   820,   655,   526,   423,  // nice 20 ~ 24
    335,   272,   215,   172,   137,  // nice 25 ~ 29
    110,    87,    70,    56,    45,  // nice 30 ~ 34
     36,    29,    23,    18,    15,  // nice 35 ~ 39
};

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    char *pa = kalloc();
    if (pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int)(p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void procinit(void)
{
  struct proc *p;

  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for (p = proc; p < &proc[NPROC]; p++)
  {
    initlock(&p->lock, "proc");
    p->state = UNUSED;
    p->kstack = KSTACK((int)(p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu *
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc *
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int allocpid()
{
  int pid;

  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc *
allocproc(void)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->state == UNUSED)
    {
      goto found;
    }
    else
    {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;
  p->nice = 20;

  // Allocate a trapframe page.
  if ((p->trapframe = (struct trapframe *)kalloc()) == 0)
  {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if (p->pagetable == 0)
  {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;
  p->nice = 20;

  // Claude AI was used and implemented in project2: initialize EEVDF fields
  p->runtime     = 0;
  p->vruntime    = 0;
  p->vdeadline   = 0;
  p->timeslice   = 5;
  p->is_eligible = 1;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if (p->trapframe)
    kfree((void *)p->trapframe);
  p->trapframe = 0;
  if (p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if (pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if (mappages(pagetable, TRAMPOLINE, PGSIZE,
               (uint64)trampoline, PTE_R | PTE_X) < 0)
  {
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if (mappages(pagetable, TRAPFRAME, PGSIZE,
               (uint64)(p->trapframe), PTE_R | PTE_W) < 0)
  {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// Set up first user process.
void userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;

  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if (n > 0)
  {
    if (sz + n > TRAPFRAME)
    {
      return -1;
    }
    if ((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0)
    {
      return -1;
    }
  }
  else if (n < 0)
  {
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int kfork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0)
  {
    return -1;
  }

  // Copy user memory from parent to child.
  if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0)
  {
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for (i = 0; i < NOFILE; i++)
    if (p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  // Claude AI was used and implemented in project2
  // Fork: child inherits parent's nice and vruntime, but resets runtime/timeslice
  np->nice        = p->nice;
  np->vruntime    = p->vruntime;
  np->runtime     = 0;
  np->timeslice   = 5;
  // recalculate vdeadline: vruntime + timeslice * 1024 / weight
  uint64 w = nice_to_weight[np->nice];
  np->vdeadline   = np->vruntime + (uint64)5 * 1024 / w;
  np->is_eligible = 1;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void reparent(struct proc *p)
{
  struct proc *pp;

  for (pp = proc; pp < &proc[NPROC]; pp++)
  {
    if (pp->parent == p)
    {
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void kexit(int status)
{
  struct proc *p = myproc();

  if (p == initproc)
    panic("init exiting");

  // Close all open files.
  for (int fd = 0; fd < NOFILE; fd++)
  {
    if (p->ofile[fd])
    {
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);

  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int kwait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (pp = proc; pp < &proc[NPROC]; pp++)
    {
      if (pp->parent == p)
      {
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if (pp->state == ZOMBIE)
        {
          // Found one.
          pid = pp->pid;
          if (addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                   sizeof(pp->xstate)) < 0)
          {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || killed(p))
    {
      release(&wait_lock);
      return -1;
    }

    // Wait for a child to exit.
    sleep(p, &wait_lock); // DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
// Claude AI was used and implemented in project2: replaced Round-Robin with EEVDF
void scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for (;;)
  {
    intr_on();
    intr_off();

    // --- Step 1: Compute global virtual time (V) ---
    // V = v0 + sum((vi - v0) * wi) / sum(wi)
    // where v0 = min vruntime among RUNNABLE/RUNNING processes
    uint64 v0 = (uint64)-1; // will hold min vruntime
    uint64 sum_w = 0;
    uint64 sum_vw = 0; // sum of (vi - v0) * wi  (computed after v0 is known)

    // First pass: find v0 and sum_w
    for (p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if (p->state == RUNNABLE || p->state == RUNNING) {
        if (p->vruntime < v0)
          v0 = p->vruntime;
        sum_w += nice_to_weight[p->nice];
      }
      release(&p->lock);
    }

    // Second pass: compute sum_vw and update eligibility
    if (sum_w > 0 && v0 != (uint64)-1) {
      for (p = proc; p < &proc[NPROC]; p++) {
        acquire(&p->lock);
        if (p->state == RUNNABLE || p->state == RUNNING) {
          sum_vw += (p->vruntime - v0) * nice_to_weight[p->nice];
        }
        release(&p->lock);
      }
      // Update eligibility: eligible if lag >= 0
      // i.e., sum_vw >= (vi - v0) * sum_w
      for (p = proc; p < &proc[NPROC]; p++) {
        acquire(&p->lock);
        if (p->state == RUNNABLE || p->state == RUNNING) {
          uint64 lhs = sum_vw;
          uint64 rhs = (p->vruntime - v0) * sum_w;
          p->is_eligible = (lhs >= rhs) ? 1 : 0;
        }
        release(&p->lock);
      }
    }

    // --- Step 2: Pick eligible process with earliest vdeadline ---
    struct proc *chosen = 0;
    uint64 earliest_vd = (uint64)-1;

    for (p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if (p->state == RUNNABLE && p->is_eligible) {
        if (p->vdeadline < earliest_vd) {
          earliest_vd = p->vdeadline;
          chosen = p;
        }
      }
      release(&p->lock);
    }

    // Fallback: if no eligible process, pick RUNNABLE with earliest vdeadline
    if (chosen == 0) {
      for (p = proc; p < &proc[NPROC]; p++) {
        acquire(&p->lock);
        if (p->state == RUNNABLE) {
          if (p->vdeadline < earliest_vd) {
            earliest_vd = p->vdeadline;
            chosen = p;
          }
        }
        release(&p->lock);
      }
    }

    // --- Step 3: Run chosen process ---
    if (chosen != 0) {
      acquire(&chosen->lock);
      chosen->state = RUNNING;
      c->proc = chosen;
      swtch(&c->context, &chosen->context);
      c->proc = 0;
      release(&chosen->lock);
    } else {
      asm volatile("wfi");
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&p->lock))
    panic("sched p->lock");
  if (mycpu()->noff != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched RUNNING");
  if (intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void forkret(void)
{
  extern char userret[];
  static int first = 1;
  struct proc *p = myproc();

  // Still holding p->lock from scheduler.
  release(&p->lock);

  if (first)
  {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);

    first = 0;
    // ensure other cores see first=0.
    __sync_synchronize();

    // We can invoke kexec() now that file system is initialized.
    // Put the return value (argc) of kexec into a0.
    p->trapframe->a0 = kexec("/init", (char *[]){"/init", 0});
    if (p->trapframe->a0 == -1)
    {
      panic("exec");
    }
  }

  // return to user space, mimicing usertrap()'s return.
  prepare_return();
  uint64 satp = MAKE_SATP(p->pagetable);
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// Sleep on channel chan, releasing condition lock lk.
// Re-acquires lk when awakened.
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock); // DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on channel chan.
// Caller should hold the condition lock.
void wakeup(void *chan)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    if (p != myproc())
    {
      acquire(&p->lock);
      if (p->state == SLEEPING && p->chan == chan)
      {
        p->state = RUNNABLE;

        // Claude AI was used and implemented in project2
        // On wakeup: reset timeslice, recalculate vdeadline and eligibility
        p->timeslice = 5;
        uint64 w = nice_to_weight[p->nice];
        p->vdeadline = p->vruntime + (uint64)5 * 1024 / w;
        p->is_eligible = 1;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int kkill(int pid)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->pid == pid)
    {
      p->killed = 1;
      if (p->state == SLEEPING)
      {
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int killed(struct proc *p)
{
  int k;

  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if (user_dst)
  {
    return copyout(p->pagetable, dst, src, len);
  }
  else
  {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if (user_src)
  {
    return copyin(p->pagetable, dst, src, len);
  }
  else
  {
    memmove(dst, (char *)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void procdump(void)
{
  static char *states[] = {
      [UNUSED] "unused",
      [USED] "used",
      [SLEEPING] "sleep ",
      [RUNNABLE] "runble",
      [RUNNING] "run   ",
      [ZOMBIE] "zombie"};
  struct proc *p;
  char *state;

  printf("\n");
  for (p = proc; p < &proc[NPROC]; p++)
  {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}


// implemented in project1, Claude AI was used
int
getnice(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      int val = p->nice;
      release(&p->lock);
      return val;
    }
    release(&p->lock);
  }
  return -1;
}

int
setnice(int pid, int value)
{
  struct proc *p;

  if(value < 0 || value > 39)
    return -1;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->nice = value;
      // Claude AI was used and implemented in project2
      // When weight changes, recalculate vdeadline with new weight
      uint64 w = nice_to_weight[value];
      p->vdeadline = p->vruntime + (uint64)5 * 1024 / w;
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

// Claude AI was used and implemented in project2
// Modified ps() to print EEVDF scheduling fields in millitick units
void
ps(int pid)
{
  struct proc *p;
  // header
  printf("name\t\tpid\tstate\t\tprio\truntime/w\truntime\t\tvruntime\tvdeadline\teligible\ttick\n");
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->state == UNUSED){
      release(&p->lock);
      continue;
    }
    if(pid != 0 && p->pid != pid){
      release(&p->lock);
      continue;
    }
    char *state;
    if(p->state == SLEEPING)      state = "SLEEPING";
    else if(p->state == RUNNABLE) state = "RUNNABLE";
    else if(p->state == RUNNING)  state = "RUNNING";
    else if(p->state == ZOMBIE)   state = "ZOMBIE";
    else                          state = "UNKNOWN";

    uint64 w = nice_to_weight[p->nice];
    // runtime/weight in milliticks: (runtime * 1024 * 1000) / weight
    uint64 runtime_w = (p->runtime * 1024 * 1000) / (w > 0 ? w : 1);
    uint64 runtime_ms  = p->runtime  * 1000;
    uint64 vruntime_ms = p->vruntime * 1000;
    uint64 vdeadline_ms = p->vdeadline * 1000;

    printf("%s\t\t%d\t%s\t%d\t%lu\t\t%lu\t\t%lu\t\t%lu\t\t%d\t\t%lu\n",
      p->name, p->pid, state, p->nice,
      runtime_w, runtime_ms, vruntime_ms, vdeadline_ms,
      p->is_eligible, p->runtime);
    release(&p->lock);
  }
}

// Claude AI was used and implemented in project2
// Called every timer interrupt for the currently running process.
// Updates runtime, vruntime, timeslice.
// When timeslice exhausts, recalculates vdeadline and resets timeslice.
void
update_eevdf_tick(struct proc *p)
{
  // p is the current running process; called from usertrap with no lock held
  acquire(&p->lock);

  uint64 w = nice_to_weight[p->nice];

  // increment actual runtime by 1 tick
  p->runtime++;

  // vruntime += 1 * 1024 / weight  (Δruntime = 1)
  p->vruntime += 1024 / (w > 0 ? w : 1);

  // decrement remaining timeslice
  p->timeslice--;

  if(p->timeslice <= 0){
    // timeslice exhausted: recalculate vdeadline and reset
    p->vdeadline  = p->vruntime + (uint64)5 * 1024 / (w > 0 ? w : 1);
    p->timeslice  = 5;
    p->is_eligible = 1; // will be properly re-evaluated in scheduler
  }

  release(&p->lock);
}

int
waitpid(int pid)
{
  struct proc *p;
  struct proc *myp = myproc();

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      if(p->parent != myp){
        release(&p->lock);
        return -1;
      }
      release(&p->lock);
      // 프로세스 종료될때까지 대기
      acquire(&myp->lock);
      while(1){
        acquire(&p->lock);
        if(p->state == ZOMBIE || p->state == UNUSED){
          release(&p->lock);
          break;
        }
        release(&p->lock);
        sleep(myp, &myp->lock);
      }
      release(&myp->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}
