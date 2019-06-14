/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

// OUR IMPLEMENTATION
void thread_recv_highest_waiter_priority(struct thread *holder);
int highest_lock_priority(struct lock *lock);
static bool thread_less_func(const struct list_elem *l, const struct list_elem *r, void *aux);


/************************************************************/
// semaphore











/************************************************************/

void
sema_init (struct semaphore *sema, unsigned value) 
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}


void
sema_down (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0) 
    {
      // 1st option: sort() !!!!! to make sure donated_thread run first !!!!
      ASSERT(list_begin(&sema->waiters) != NULL);
      list_insert_ordered(&sema->waiters, &thread_current()->elem, 
                          thread_less_func, NULL);
      
      // 2nd option: delete() from ready_list n stored in sema->waiters[]
      // list_remove(&thread_current()->elem);
      // list_push_back (&sema->waiters, &thread_current ()->elem);
      thread_block ();
    }
  sema->value--;
  intr_set_level (old_level);
}

void
sema_up (struct semaphore *sema) 
{
  ASSERT (sema != NULL);
  enum intr_level old_level;
  old_level = intr_disable ();
  sema->value++;

  ASSERT(list_begin(&sema->waiters) != NULL);
  if (!list_empty (&sema->waiters)){
    // see sema_down()
    list_sort(&(sema->waiters), thread_less_func, NULL);
    thread_unblock (list_entry (list_pop_front (&sema->waiters),
                                struct thread, elem));
  }
  intr_set_level (old_level);
}


bool
sema_try_down (struct semaphore *sema) 
{
  enum intr_level old_level;
  bool success;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (sema->value > 0) 
    {
      sema->value--;
      success = true; 
    }
  else
    success = false;
  intr_set_level (old_level);

  return success;
}


static void
sema_test_helper (void *sema_) 
{
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++) 
    {
      sema_down (&sema[0]);
      sema_up (&sema[1]);
    }
}


/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) 
{
  struct semaphore sema[2];
  int i;

  printf ("Testing semaphores...");
  sema_init (&sema[0], 0);
  sema_init (&sema[1], 0);
  thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++) 
    {
      sema_up (&sema[0]);
      sema_down (&sema[1]);
    }
  printf ("done.\n");
}


/************************************************************/
// lock











/************************************************************/

void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);

  lock->holder = NULL;
  list_init(&lock->blocked_threads);
  sema_init (&lock->semaphore, 1);
}



void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  enum intr_level old_level = intr_disable();
  
  // separate mlfqs test from priority_donate test
  if(thread_mlfqs) {
    sema_down(&lock->semaphore);
    lock->holder = thread_current();
    intr_set_level(old_level);
    return;
  }
  
  // 1. nested donation 
  // if((&lock->semaphore)->value == 0){ 
  bool success = sema_try_down(&lock->semaphore);
  if(!success){

    // low_thread chain() to upper_lock -> nested_donate()
    thread_current()->lock_waiting_on = lock;    
    ASSERT(is_thread(lock->holder));
    
    // lock_release() -> 2nd lock highest waiter + donate_priority()
    list_push_back(&lock->blocked_threads, &thread_current()->lock_elem);
    thread_donate_priority(lock->holder, thread_pick_higher_priority(thread_current()));
    
    // 2. acquire() lock
    sema_down(&lock->semaphore); // <- end point where thread_block()
        
    // 3. update() 4 
    // <- start point when thread switch back in (holder lock_release())
    list_remove(&thread_current()->lock_elem); // for lock_release()
    thread_current()->lock_waiting_on = NULL;    
  }
  
  // lock->new_holder, holder_thread->locks[]
  lock->holder = thread_current();
  list_push_back(&thread_current()->locks_acquired, &lock->thread_elem);
    
  // new holder receive priority donation 
  // thread_recv_highest_waiter_priority(thread_current());
  
  intr_set_level(old_level);  
  
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock)
{
  bool success;

  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  success = sema_try_down (&lock->semaphore);
  if (success)
    lock->holder = thread_current ();
  return success;
}

/* Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
// OUR IMPLEMENTATION
void
lock_release (struct lock *lock) 
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));
  
  enum intr_level old_level;  
  old_level = intr_disable ();
  
  struct thread *cur = thread_current();

  if(thread_mlfqs) {
    lock->holder = NULL;
    sema_up (&lock->semaphore);
    intr_set_level (old_level);
    return;
  }

  // 1. thread_unblock()
  sema_up (&lock->semaphore);

  // 2. update() 2 (holder x lock_waiting_on + no change in lock's waiting threads)
  lock->holder = NULL;
  list_remove (&lock->thread_elem); // thread's waiting locks
  
  // 3. clear holder's donated_priority / 2nd lock's highest waiter  
  // 3.a thread has no more locks 
  if (list_empty (&cur->locks_acquired)){
    thread_clear_donated_priority();
  // 3.b thread has remaining locks
  } else { 
    // holder loop() thru remaining locks + waiters -> 2nd lock highest priority
    thread_recv_highest_waiter_priority(cur);
  }

  if (!is_highest_priority(thread_pick_higher_priority(cur))){
        thread_yield();
  }

  intr_set_level (old_level);
}


/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) 
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}





/************************************************************/
// CV











/************************************************************/


/* One semaphore in a list. */
struct semaphore_elem 
  {
    struct list_elem elem;              /* List element. */
    struct semaphore semaphore;         /* This semaphore. */
  };


/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) 
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));
  
  sema_init (&waiter.semaphore, 0);
  list_push_back (&cond->waiters, &waiter.elem);
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  if (!list_empty (&cond->waiters)) 
    sema_up (&list_entry (list_pop_front (&cond->waiters),
                          struct semaphore_elem, elem)->semaphore);
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}



/************************************************************/
// helper











/************************************************************/

static bool thread_less_func(const struct list_elem *l, const struct list_elem *r, void *aux) {
  struct thread *lthread, *rthread;
  ASSERT (l != NULL && r != NULL);
  lthread = list_entry(l, struct thread, elem);
  rthread = list_entry(r, struct thread, elem);
  if(!thread_mlfqs) {
    return (thread_pick_higher_priority(lthread) >= thread_pick_higher_priority(rthread));    
  } else {
    return (lthread->priority >= rthread->priority);
  }
}


// holder receives highest priority from its locks' waiters
void thread_recv_highest_waiter_priority(struct thread *holder){
  if (!list_empty(&holder->locks_acquired)) {
        struct list_elem *e;
        // 1st for loop
        for (e = list_begin(&holder->locks_acquired);
                e != list_end(&holder->locks_acquired);
                e = list_next(e)) {
            struct lock *waiter_lock = list_entry(e, struct lock, thread_elem);
            // 2nd for loop
            int highest_priority = highest_lock_priority(waiter_lock);
            if (highest_priority > holder->donated_priority) {
                holder->donated_priority = highest_priority;
            }
        }
    }
}

int highest_lock_priority(struct lock *lock){
    struct list_elem *e;
    int max_priority = PRI_MIN;
    if (!list_empty(&lock->blocked_threads)) {
        for (e = list_begin(&lock->blocked_threads);
             e != list_end(&lock->blocked_threads);
             e = list_next(e)) {
            struct thread *waiter_thread = list_entry(e, struct thread, lock_elem);
            int cur_priority = thread_pick_higher_priority(waiter_thread);
            if (cur_priority > max_priority) {
                max_priority = cur_priority;
            }
        }
    }
    return max_priority;
}