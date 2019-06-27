#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

#define THREAD_AWAKE -1

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    unsigned magic;                     /* Detects stack overflow. */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Priority. */
    struct list_elem elem;              // shared by ready_list, sema->waiters[] -> mutually exclusive
    struct list_elem all_elem;          // all_list
    
    int64_t sleep_ticks;                // number of ticks thread to sleep
    struct list_elem sleep_elem;        // wait/sleep list 
    
    int mlfq_priority;
    int original_priority;               // 2nd_lock_highest_waiter() + next_thread_to_run()
    struct lock *lock_waiting_on;       // nested_doante_priority(), traverse() to highest holder 
    // struct list_elem lock_elem;         // lock->block_threads[], lock_release() 2nd lock highest waiter
    struct list locks_acquired;         // thread_exit() free() all locks + lock_release() 2nd lock highest waiter    

    int recent_cpu;                     // mlfqs, moving average of cpu usage
    int niceness;                       // mlfqs, inflat your recent_cpu, let other threads run

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
#endif

  };

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

// OUR IMPLEMENTATION
void add_thread_sleeplist(struct thread *t);
void unblock_awaken_thread(void);

void thread_clear_donated_priority (void);
void thread_yield_if_not_highest_priority(void);
void thread_donate_priority(struct thread *t);
int thread_get_donated_priority (void);
// int thread_pick_higher_priority (struct thread *t);
bool is_thread (struct thread *t);

// mlfqs
int double_to_fixed_point(int n, int q);
int convert_to_integer_round_zero(int x, int q);
int convert_to_integer_round_nearest(int x, int q);
int multiply_x_by_y(int x, int y, int q);
int multiply_x_by_n(int x, int n);
int fp_divide_x_by_y(int x, int y, int q);
int fp_divide_x_by_n(int x, int n);
void thread_update_mlfqs(void);
int compute_priority(int recent_cpu, int nice);
int compute_cpu_usage(int recent_cpu, int load_average, int niceness);
int compute_load_avg(int load_average, int ready_threads);


// DEBUG
void print_all_queue(void);
void print_ready_queue(void);
void print_sleep_queue(void);
void print_all_priorities(void);
void print_ready_priorities(void);
void print_sleep_priorities(void);

#endif /* threads/thread.h */








