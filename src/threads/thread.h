#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/synch.h"
#include "filesys/file.h"

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
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */


struct thread
  {
    tid_t tid;
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    unsigned magic;                     /* Detects stack overflow. */
    uint8_t *stack;                     // esp
    struct list_elem elem;              // shared by ready_list, sema->waiters[] -> mutually exclusive
    struct list_elem all_elem;          // all_list
    
    int64_t sleep_ticks;                // number of ticks thread to sleep
    struct list_elem sleep_elem;        // wait/sleep list 

    int priority;                       // donated priority
    int original_priority;              // 2nd_lock_highest_waiter() + next_thread_to_run()
    struct lock *lock_waiting_on;       // nested_doante_priority(), traverse() to highest holder 
    struct list locks_acquired;         // thread_exit() free() all locks + lock_release() 2nd lock highest waiter    
    // struct list_elem lock_elem;         // lock->block_threads[], lock_release() 2nd lock highest waiter

    int mlfq_priority;
    int recent_cpu;                     // mlfqs, moving average of cpu usage
    int niceness;                       // mlfqs, inflat your recent_cpu, let other threads run

    struct thread *parent;              // check parent, child lineage
    struct semaphore sema_elf_call_exit;    // process_wait() -> store parent until all children exits
    struct semaphore sema_elf_exit_status;     // process_exit() -> store child until parent get child's exit_status 
    struct semaphore sema_load_elf;    // process_execute() wait til start_process() done loading ELF -> elf_exit_status
    int elf_exit_status;                // kernel knows whether user thread load_elf successfully 
    bool exited;                        // parent wont wait on already exited child 
    bool waited;                        // wait() twice error
    struct file *elf_file;              // disable/allow write
    struct list fd_list;
    struct list mmap_list;
    int total_fd;

    struct hash *supt;            // each thread's upage status
    struct lock supt_lock;       // each thread 1 supt lock
    uint32_t *pagedir;                  // accessed, dirty bit

  };

// for mapping fid w file
struct file_desc
{
	int id; // fd, index in fd_list
	struct list_elem fd_list_elem;
	struct file *f;
	struct dir *d;
};


// for mapping mmapid w sptes, for munmap() only
struct mmap_desc
{
	int id; 
	struct list_elem mmap_list_elem;
	void *upage;// starting spte
  uint32_t file_size; // ending spte	
  
  //1 thd read() n mmap() same file, when munmap(), won't file_close() fd->file{}
  struct file* dup_file; 
};




extern bool thread_mlfqs;               // round-robin vs mlfqs scheduler, kernel command-line option "-o mlfqs"

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
void set_priority(struct thread *target, int new_priority);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);
struct thread *tid_to_thread(tid_t tid);

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
int divide_x_by_y(int x, int y, int q);
int divide_x_by_n(int x, int n);
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








