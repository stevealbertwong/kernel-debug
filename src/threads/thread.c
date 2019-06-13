#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

// mlfqs
static int load_avg;

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

static struct list sleep_list; // threads waiting/sleeping

static bool thread_init_finished = false;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);
  list_init (&sleep_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
  initial_thread->sleep_ticks = 0;
  load_avg = 0;
  thread_init_finished = true;
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
// OUR IMPLEMENTATION
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  if(thread_init_finished){
    unblock_awaken_thread();
  }  

  if (thread_mlfqs && !list_empty(&all_list)) {
    thread_update_mlfqs();
  }

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /* Add to run queue. */
  thread_unblock (t);

  if (!thread_mlfqs && priority > thread_current()->priority) {
    thread_yield();
  }

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  list_push_back (&ready_list, &t->elem);
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());
  struct thread *cur = thread_current();
  struct list_elem *e;

  // remove() lock->threads[]
  if (cur->lock_waiting_on != NULL) {
        list_remove(&cur->lock_elem);
  }
  
  // lock_release() thread->locks[] 
  while (!list_empty(&cur->locks_acquired)) {
      e = list_begin(&cur->locks_acquired);
      struct lock *lock = list_entry(e, struct lock, thread_elem);
      lock_release(lock);
  }



#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->all_elem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}



/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) 
    list_push_back (&ready_list, &cur->elem);
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, all_elem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
// OUR IMPLEMENTATION
void
thread_set_priority (int new_priority) 
{
  if (!thread_mlfqs){
    thread_current ()->priority = new_priority;  
    if(thread_current()->lock_waiting_on){
      thread_donate_priority(thread_current()->lock_waiting_on->holder, thread_pick_higher_priority(thread_current()));
    }    
  }
}

// OUR IMPLEMENTATION
void
thread_clear_donated_priority (void) 
{
  if (!thread_mlfqs){
    thread_current ()->donated_priority = PRI_MIN;
  }
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

// OUR IMPLEMENTATION
int
thread_get_donated_priority (void) 
{
  return thread_current ()->donated_priority;
}

// OUR IMPLEMENTATION
int
thread_pick_higher_priority (struct thread *t) {  
  if (t->donated_priority > t->priority){
    return t->donated_priority;
  }else{
    return t->priority;
  }
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) 
{
  /* Not yet implemented. */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
// OUR IMPLEMENTATION
static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->magic = THREAD_MAGIC;

  t->donated_priority = PRI_MIN; // lock(), unlock()
  list_init(&t->locks_acquired);

  old_level = intr_disable ();
  list_push_back (&all_list, &t->all_elem);
  intr_set_level (old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
// OUR IMPLEMENTATION
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list)){
    return idle_thread;
  } else {
    //for loop ready_list -> highest donated_priority/priority(whichever higher)
    struct list_elem *next = list_begin(&ready_list);
    struct thread *t = list_entry(next, struct thread, elem);
    int highest_priority_val = thread_pick_higher_priority(t);    
    struct list_elem *e;
    struct thread *next_thread;

    for (e = list_next(next); e != list_end(&ready_list);
          e = list_next(e)) {
        t = list_entry(e, struct thread, elem);
        
        int curr_priority = thread_pick_higher_priority(t);

        if (curr_priority > highest_priority_val) {
            highest_priority_val = curr_priority;
            next = e;
        }
    }
    next_thread = list_entry(next, struct thread, elem);
    list_remove(next);
    return next_thread;
  }
}



/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);

/************************************************************/
// OUR IMPLEMENTATION














/************************************************************/
void 
add_thread_sleeplist(struct thread *t){
  ASSERT (!intr_context ()); // possessing external interrupt
  list_push_back(&sleep_list, &t->sleep_elem);
}

// add() awake_thread to ready_list. If empty sleep_list, no effect.
void 
unblock_awaken_thread(void){
    
    ASSERT(intr_get_level() == INTR_OFF);
    struct list_elem *e = list_begin(&sleep_list);

    while (e != list_end(&sleep_list)) {
        
        struct thread *t = list_entry(e, struct thread, sleep_elem);
        ASSERT(is_thread(t));
        if (t->sleep_ticks <= 1) {
            t->sleep_ticks = 0; // clean for next time sleep
            e = list_remove(e);
            thread_unblock(t);
        }
        else {
            t->sleep_ticks--;
            e = list_next(e);
        }
    }
}

void 
thread_donate_priority(struct thread *t, int priority){
  
  if(t->donated_priority < priority){        
    t->donated_priority = priority; // donate() priority to donated_priority    
    thread_donate_priority(t->lock_waiting_on->holder, priority); // recur()
  }

  // accelerated lock_release() !!!!!
  // locking_thread yield() to upper_lock holder_thread (or any other threads)
  // after nested_donate() in hopes they lock_release() before locking_thread thread_block() 
  if (!is_highest_priority(thread_pick_higher_priority(thread_current()))) {
      thread_yield();
  }  
}

bool is_highest_priority(int priority){
  // for loop ready_list -> higher of priority/donated_priority  
  enum intr_level old_level = intr_disable();
  
  int highest_priority_val = thread_pick_higher_priority(thread_current()); 
  struct list_elem *e;
  for (e = list_begin(&ready_list); e != list_end(&ready_list);
        e = list_next(e)) {
      struct thread *t = list_entry(e, struct thread, elem);
      ASSERT(is_thread(t));
      int curr_priority = thread_pick_higher_priority(t);

      if (curr_priority > highest_priority_val) {
          highest_priority_val = curr_priority;
      }
  }
  intr_set_level(old_level);
  return highest_priority_val >= priority;
}


/************************************************************/
// compute load_average n recent_cpu











/************************************************************/

#define FIXED_POINT_Q 14
#define FIXED_ONE 1 << 14
#define fp_f    (2<<(14-1)) /* Using 17.14 fixed point representation. */

// run every thread_tick()
void thread_update_mlfqs(void){
    /* Increment recent_cpu by 1 for running thread every interrupt. */
    if (thread_current() != idle_thread) {
        thread_current()->recent_cpu += FIXED_ONE;
    }

    /* Update load_avg and recent_cpu once per second. */
    if (timer_ticks() % TIMER_FREQ == 0) {
        struct list_elem *cpu_e;
        load_avg = compute_load_avg(load_avg, list_size(&ready_list));
        for (cpu_e = list_begin(&all_list); cpu_e != list_end(&all_list);
             cpu_e = list_next(cpu_e)) {
            struct thread *new_t = list_entry(cpu_e, struct thread, all_elem);
            new_t->recent_cpu =
                compute_cpu_usage(new_t->recent_cpu, load_avg, new_t->niceness);
        }
    }

    /* Update priority for all threads every four timer ticks. */
    if (timer_ticks() % 4 == 0) {
        struct list_elem *prio_e;
        for (prio_e = list_begin(&all_list); prio_e != list_end(&all_list);
             prio_e = list_next(prio_e)) {
            struct thread *new_t = list_entry(prio_e, struct thread, all_elem);
            ASSERT(is_thread(new_t));

            new_t->priority = compute_priority(new_t->recent_cpu, new_t->niceness);
        }
    }
}

/* Calculates the new priority of the thread given cpu usage
   and a niceness.

   Input:
   recent_cpu: Fixed Point
   nice:       Integer

   Return:
   priority:   Integer
*/
int compute_priority(int recent_cpu, int nice){
    int fixed_PRI_MAX = convert_to_fixed_point(PRI_MAX, FIXED_POINT_Q);
    int fixed_nice_factor = convert_to_fixed_point(nice * 2, FIXED_POINT_Q);
    int fixed_priority = fixed_PRI_MAX - (recent_cpu / 4) - fixed_nice_factor;
    int int_priority = convert_to_integer_round_nearest(fixed_priority, FIXED_POINT_Q);
    return int_priority;
}

/* Calculates the new cpu_usage of the thread given cpu usage
   and a load_average.

   Input:
   recent_cpu:    Fixed Point
   load_average:  Fixed Point

   Return:
   recent_cpu:    Fixed Point
*/
int compute_cpu_usage(int recent_cpu, int load_average, int niceness) {
    int fixed_one = convert_to_fixed_point(1, FIXED_POINT_Q);
    int fixed_fraction = divide_x_by_y(2 * load_average,
                                       2 * load_average + fixed_one,
                                       FIXED_POINT_Q);
    int fraction_multiplication = multiply_x_by_y(fixed_fraction,
                                                  recent_cpu,
                                                  FIXED_POINT_Q);
    int fixed_niceness = convert_to_fixed_point(niceness, FIXED_POINT_Q);
    int fixed_new_cpu = fraction_multiplication + fixed_niceness;
    return fixed_new_cpu;
}

/* Calculates the new load_average of the thread given previous
   load_average and number of ready threads.

   Input:
   load_avg:       Fixed Point
   ready_threads:  Integer

   Return:
   load_average:    Fixed Point
*/
int compute_load_avg(int load_average, int ready_threads) {
    int fixed_numerator = convert_to_fixed_point(59, FIXED_POINT_Q);
    int fixed_one = convert_to_fixed_point(1, FIXED_POINT_Q);
    int fixed_denominator = convert_to_fixed_point(60, FIXED_POINT_Q);
    int fixed_threads = convert_to_fixed_point(ready_threads, FIXED_POINT_Q);

    int fixed_fraction = 
            divide_x_by_y(fixed_numerator, fixed_denominator, FIXED_POINT_Q);
    int fixed_second_fraction =
            divide_x_by_y(fixed_one, fixed_denominator, FIXED_POINT_Q);

    int fraction_multiplication = 
            multiply_x_by_y(fixed_fraction, load_average, FIXED_POINT_Q);
    int second_fraction_multiplication = multiply_x_by_y(fixed_second_fraction,
                                                         fixed_threads,
                                                         FIXED_POINT_Q);
    return fraction_multiplication + second_fraction_multiplication;
}


int convert_to_fixed_point(int n, int q) {
    int f = 1 << q;
    return n * f;
}

int convert_to_integer_round_zero(int x, int q) {
    int f = 1 << q;
    return x / f;
}

int convert_to_integer_round_nearest(int x, int q) {
    int f = 1 << q;
    if (x >= 0) {
        return (x + f / 2) / f;  
    }
    return (x - f / 2) / f;
}

int multiply_x_by_y(int x, int y, int q) {
    int f = 1 << q;

    return ((int64_t) x) * y / f;
}

int multiply_x_by_n(int x, int n) {
    return x * n;
}

int divide_x_by_y(int x, int y, int q) {
    int f = 1 << q;

    return ((int64_t) x) * f / y;
}

int divide_x_by_n(int x, int n) {
    return x / n;
}





/************************************************************/
// DEBUG











/************************************************************/
void print_ready_queue(void) {
    struct list_elem *e;
    printf("READY_QUEUE: \n");
    for (e = list_begin (&ready_list); 
         e != list_end (&ready_list); e = list_next (e)) {
        struct thread *t = list_entry(e, struct thread, elem);
        printf("%s(%d) ", t->name, t->priority);
    }
    printf("\n");
}

void print_all_queue(void) {
    struct list_elem *e;
    printf("ALL_QUEUE: \n");
    for (e = list_begin (&all_list); 
         e != list_end (&all_list); e = list_next (e)) {
        struct thread *t = list_entry(e, struct thread, elem);
        printf("%s(%d) ", t->name, t->priority);
    }
    printf("\n");
}

void print_sleep_queue(void) {
    struct list_elem *e;
    printf("SLEEP_QUEUE: \n");
    for (e = list_begin (&sleep_list); 
         e != list_end (&sleep_list); e = list_next (e)) {
        struct thread *t = list_entry(e, struct thread, elem);
        printf("%s(%d) ", t->name, t->priority);
    }
    printf("\n");
}

void print_all_priorities(void) {
    struct list_elem *e;
    for (e = list_begin (&all_list); 
         e != list_end (&all_list); e = list_next (e)) {
        struct thread *t = list_entry(e, struct thread, all_elem);
        if (t != idle_thread)
            printf("%s-p%d-i%d-s%d  ", t->name, t->priority, t->tid, t->status);
    }
    printf("\n");
}

void print_ready_priorities(void) {
    struct list_elem *e;
    for (e = list_begin (&ready_list); 
         e != list_end (&ready_list); e = list_next (e)) {
        struct thread *t = list_entry(e, struct thread, all_elem);
        if (t != idle_thread)
            printf("%s-p%d-i%d-s%d  ", t->name, t->priority, t->tid, t->status);
    }
    printf("\n");
}

void print_sleep_priorities(void) {
    struct list_elem *e;
    for (e = list_begin (&sleep_list); 
         e != list_end (&sleep_list); e = list_next (e)) {
        struct thread *t = list_entry(e, struct thread, all_elem);
        if (t != idle_thread)
            printf("%s-p%d-i%d-s%d  ", t->name, t->priority, t->tid, t->status);
    }
    printf("\n");
}
