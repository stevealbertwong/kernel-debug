#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

struct semaphore 
  {
    unsigned value;             // counter
    struct list waiters;        // store() locked_thread{} from ready_list
  };

void sema_init (struct semaphore *, unsigned value);
void sema_down (struct semaphore *);
bool sema_try_down (struct semaphore *);
void sema_up (struct semaphore *);
void sema_self_test (void);

struct lock 
  {
    struct thread *holder;      // nested_doante_priority(), traverse() to highest holder 
    struct semaphore semaphore; /* Binary semaphore controlling access. */
    
    // OUR IMPLEMENTATION
    int priority;
    struct list blocked_threads; //lock_release() -> 2nd lock highest waiter
    struct list_elem thread_locks_list_elem; //thread->locks[], lock_release() 2nd lock highest waiter + thread_exit() free() all locks
  };

void lock_init (struct lock *);
void lock_acquire (struct lock *);
bool lock_try_acquire (struct lock *);
void lock_release (struct lock *);
bool lock_held_by_current_thread (const struct lock *);


/* Condition variable. */
struct condition 
  {
    struct list waiters; // waiter_semaphores[]
  };

void cond_init (struct condition *);
void cond_wait (struct condition *, struct lock *);
void cond_signal (struct condition *, struct lock *);
void cond_broadcast (struct condition *, struct lock *);

/* Optimization barrier.

   The compiler will not reorder operations across an
   optimization barrier.  See "Optimization Barriers" in the
   reference guide for more information.*/
#define barrier() asm volatile ("" : : : "memory")


// OUR IMPLEMENTATION
// void thread_recv_highest_waiter_priority(struct thread *holder);
int highest_lock_priority(struct lock *lock);
bool thread_less_func(const struct list_elem *l, const struct list_elem *r, void *aux);
bool thread_more_func(const struct list_elem *l, const struct list_elem *r, void *aux);

#endif /* threads/synch.h */

