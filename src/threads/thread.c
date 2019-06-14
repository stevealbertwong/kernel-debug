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
#define MAX(a,b) (((a) > (b)) ? (a) : (b))

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



/************************************************************/
// setup thread system











/************************************************************/

// init() initial_thread
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  // system-wide
  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);
  list_init (&sleep_list);
  load_avg = 0;

  // initial_thread-wsie
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();

  if (thread_mlfqs) {
      initial_thread->niceness = 0;
      initial_thread->recent_cpu = 0;
      initial_thread->priority = compute_priority(initial_thread->recent_cpu, initial_thread->niceness);
  }
  thread_init_finished = true;
}


// init() any thread (except initial_thread)
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
  t->magic = THREAD_MAGIC;

  t->sleep_ticks = 0;
  t->lock_waiting_on = NULL;

  t->donated_priority = PRI_MIN; // lock(), unlock()
  list_init(&t->locks_acquired);

  if (list_empty(&all_list)) {
    t->niceness = 0;  /* Set niceness to 0 on initial thread */
    t->recent_cpu = 0; /* Set cpu_usage to 0 on initial thread */
  }
  else {
    /* Inherit niceness and cpu_usage from parent */
    t->niceness = thread_current()->niceness;
    t->recent_cpu = thread_current()->recent_cpu;
  }
  if (thread_mlfqs) {
    // t->priority = 0;
    t->priority = compute_priority(t->recent_cpu, t->niceness);
  } else {
    t->priority = priority;
  }

  old_level = intr_disable ();
  list_push_back (&all_list, &t->all_elem);
  intr_set_level (old_level);
}

// init() idle_thread + starts preemptive thread scheduling 
void
thread_start (void) 
{
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);
  intr_enable (); // starts preemptive thread scheduling by enabling interrupts.
  sema_down (&idle_started); // Wait for the idle thread to initialize idle_thread.
}





/************************************************************/
// major APIs - block(), unblock(), yield()











/************************************************************/


struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}


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

  thread_unblock (t); // add to run queue.

  if (!thread_mlfqs && priority > thread_current()->priority) {
    thread_yield();
  }
  return tid;
}


void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) 
  if(thread_mlfqs){
    list_push_back (&ready_list, &cur->elem);
  }else{
    list_insert_ordered(&ready_list, &cur->elem, thread_less_func, 
                        NULL);
  }
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}



void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}


void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  if(thread_mlfqs){
    list_push_back (&ready_list, &t->elem);
  }else{
    list_insert_ordered(&ready_list, &t->elem, thread_less_func, 
                        NULL);
  }
  t->status = THREAD_READY;
  intr_set_level (old_level);
}



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

  intr_disable ();
  list_remove (&thread_current()->all_elem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}







/************************************************************/
// major APIs - thread system















/************************************************************/


// timer interrupt handler (external interrupt)
void
thread_tick (void) 
{
  struct thread *t = thread_current ();
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

  if (++thread_ticks >= TIME_SLICE) // preemption !!!!!
    intr_yield_on_return ();
}


static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      intr_disable ();
      thread_block ();
      asm volatile ("sti; hlt" : : : "memory");
    }
}


static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}


struct thread *
running_thread (void) 
{
  uint32_t *esp;
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}


static void *
alloc_frame (struct thread *t, size_t size) 
{
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}


static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list)){
    return idle_thread;
  } else {
    //for loop ready_list -> highest donated_priority/priority
    struct list_elem *e;
    struct thread *next_thread;
    struct list_elem *next_thread_pointer = list_begin(&ready_list);
    struct thread *t = list_entry(next_thread_pointer, struct thread, elem);
    int highest_priority_val = thread_pick_higher_priority(t);    


    for (e = list_next(next_thread_pointer); e != list_end(&ready_list);
          e = list_next(e)) {
        t = list_entry(e, struct thread, elem);
        
        int curr_priority = thread_pick_higher_priority(t);

        if (curr_priority > highest_priority_val) {
            highest_priority_val = curr_priority;
            next_thread_pointer = e;
        }
    }
    next_thread = list_entry(next_thread_pointer, struct thread, elem);
    list_remove(next_thread_pointer);
    return next_thread;
  }
}

// static struct thread *
// next_thread_to_run (void) 
// {
//   // NOTE FROM ILYA: This will almost certainly merge conflict, but
//   // I think my version should work for your part too, so you might
//   // want to consider keeping this version.

//   if (list_empty (&ready_list)) {
//     return idle_thread;
//   }

//   int highest_pri = PRI_MIN - 1;
//   struct thread *max;
//   struct list_elem *max_elem;
//   struct list_elem *e;

//   for( e = list_begin(&ready_list); e != list_end(&ready_list);
//        e = list_next(e)) 
//   {
//     struct thread *t = list_entry(e, struct thread, elem);
    
//     if(thread_mlfqs) {
//       max = t;
//       max_elem = e;
//       highest_pri = t->priority;
//     }
//     else if(!thread_mlfqs && t->priority > highest_pri) {
//       max = t;
//       max_elem = e;
//       highest_pri = t->priority;
//     }
//   }
//   ASSERT(max_elem != NULL);
//   list_remove(max_elem);
//   return max;
// }


void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  ASSERT (intr_get_level () == INTR_OFF);

  cur->status = THREAD_RUNNING;

  thread_ticks = 0; /* Start new time slice. */

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  // If  thread we switched from is dying, destroy its struct thread.
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}


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
// major APIs - for timer interrupt + lock_acquire()














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
thread_donate_priority(struct thread *holder_thread){
    ASSERT(is_thread(holder_thread));
    struct list_elem *e, *f;    
    int max = 0;
    // holder_thread get highest donation of all waiter_thread
    for (e = list_begin(&(holder_thread->locks_acquired)); 
         e != list_end(&(holder_thread->locks_acquired)); e = list_next(e)) {

        struct lock *l = list_entry(e, struct lock, thread_elem);
        for (f = list_begin(&(l->blocked_threads)); 
             f != list_end(&(l->blocked_threads)); f = list_next(f)) {
            
            struct thread *waiting_thread = list_entry(f, struct thread, lock_elem);
            max = MAX(max, thread_pick_higher_priority(waiting_thread));
        }
    }
    holder_thread->donated_priority = max;
    if (holder_thread->lock_waiting_on && holder_thread->lock_waiting_on->holder) { 
        thread_donate_priority(holder_thread->lock_waiting_on->holder);
    }
    ASSERT(max >= holder_thread->priority);
}


//TODO: fix this
// void 
// thread_donate_priority(struct thread *t, int priority){
//   if(t->donated_priority < priority){        
//     t->donated_priority = priority; // donate() priority to donated_priority    
//     thread_donate_priority(t->lock_waiting_on->holder, priority); // recur()
//   }
//   // accelerated lock_release() !!!!!
//   // locking_thread yield() to upper_lock holder_thread (or any other threads)
//   // after nested_donate() in hopes they lock_release() before locking_thread thread_block() 
//   if (!is_highest_priority(thread_pick_higher_priority(thread_current()))) {
//       thread_yield();
//   }  
// }






/************************************************************/
// major APIs - getter, setter











/************************************************************/


void
thread_set_priority (int new_priority) 
{
  enum intr_level old_level;
  old_level = intr_disable();

  if (!thread_mlfqs){
    thread_current()->priority = new_priority;

    if(thread_current()->lock_waiting_on != NULL){
      thread_donate_priority(thread_current()->lock_waiting_on->holder, thread_pick_higher_priority(thread_current()));
    }

    if (!is_highest_priority(new_priority)){
        thread_yield();
    }
  }

  intr_set_level(old_level);
}


void
thread_clear_donated_priority (void) 
{
  if (!thread_mlfqs){
    thread_current ()->donated_priority = PRI_MIN;
  }
}

int
thread_get_priority (void) 
{
  // return thread_current ()->priority;
  return thread_pick_higher_priority(thread_current ());
}


int
thread_get_donated_priority (void) 
{
  return thread_current ()->donated_priority;
}


int
thread_pick_higher_priority (struct thread *t) {  
  if (t->donated_priority > t->priority){
    return t->donated_priority;
  }else{
    return t->priority;
  }
}


void
thread_set_nice (int nice UNUSED) 
{
    enum intr_level old_level = intr_disable();
    struct thread *cur = thread_current();
    cur->niceness = nice;
    cur->priority = compute_priority(cur->recent_cpu, cur->niceness);
    if (!is_highest_priority(cur->priority)) {
        thread_yield();
    }
    intr_set_level(old_level);
}

int
thread_get_nice (void) 
{
  return thread_current()->niceness;  
}


int
thread_get_load_avg (void) 
{
  enum intr_level old_level;
  old_level = intr_disable();
  //100 times the system load average
  int ret_value =  ((load_avg * 100) + (1 << 14) / 2) / (1 << 14);  
  intr_set_level(old_level);
  return ret_value;
}


int
thread_get_recent_cpu (void) 
{
  enum intr_level old_level;
  old_level = intr_disable();
  //100 times the current thread's recent_cpu value
  int ret_value =  ((load_avg * 100) + (1 << 14) / 2) / (1 << 14);  
  intr_set_level(old_level);
  return ret_value;
}







/************************************************************/
// compute mlfqs, load_average n recent_cpu











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
// helper












/************************************************************/



// whether arg priority is the highest in ready_list
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
  return priority >= highest_priority_val;
}



void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}


const char *
thread_name (void) 
{
  return thread_current ()->name;
}


tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}


void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, all_elem);
      func (t, aux); // aux == arg
    }
}

/* Returns true if T appears to point to a valid thread. */
bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
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
