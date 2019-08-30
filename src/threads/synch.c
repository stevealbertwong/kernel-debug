#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static bool
cv_more_func(const struct list_elem* a, const struct list_elem *b, void* aux UNUSED);
static bool
comparator_greater_thread_priority(const struct list_elem* a, const struct list_elem *b, void* aux UNUSED);
static bool
comparator_greater_lock_priority(const struct list_elem* a, const struct list_elem *b, void* aux UNUSED);

/************************************************************/
// semaphore
// ready_list[] <-> sema->waiters[] 














/************************************************************/

void
sema_init (struct semaphore *sema, unsigned value) 
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}

/**
 * temperarily store threads in semaphore
 * ready_list[] -> sema->waiters[] 
 * 
 * remove from ready_list[] before adding to sema->waiters[] ??
 */ 
void
sema_down (struct semaphore *sema)
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0)
    {
      list_insert_ordered (&sema->waiters, &thread_current()->elem,
          comparator_greater_thread_priority, NULL);
      
      //list_remove(&thread_current()->elem);
      thread_block ();
    }
  sema->value--;
  intr_set_level (old_level);
}

// void
// sema_down (struct semaphore *sema) 
// {
//   ASSERT (sema != NULL);
//   ASSERT (!intr_context ());
//   enum intr_level old_level;
//   old_level = intr_disable ();

//   while (sema->value == 0) 
//     {
//       if(!thread_mlfqs){
//         // 1st option: sort() !!!!! to make sure donated_thread run first !!!!
//         ASSERT(list_begin(&sema->waiters) != NULL);

//          // 1. remove from ready_list[], add to lock->semaphore->waiters[]
//         if (is_interior(&thread_current()->elem)){
//           list_remove(&thread_current()->elem);
//         }
//         // 2. stored in lock->sema->waiters[]
//         // list_insert_ordered(&sema->waiters, &thread_current()->elem, 
//         //                     thread_less_func, NULL);
//         list_push_back (&sema->waiters, &thread_current ()->elem);

//       } else { // mlfqs
//         list_push_back (&sema->waiters, &thread_current ()->elem);
//       }
//       thread_block ();
//     }
//   sema->value--;

//   intr_set_level (old_level);
// }



/**
 * pop threads from sema to ready_list, based on priority
 * ready_list[] <- sema->waiters[] 
 * 
 */
void
sema_up (struct semaphore *sema)
{
  enum intr_level old_level;
  struct thread *target = NULL;
  ASSERT (sema != NULL);
  old_level = intr_disable ();

  sema->value++;
  if (!list_empty (&sema->waiters)) {
    list_sort(&(sema->waiters), comparator_greater_thread_priority, NULL);
    target = list_entry (list_pop_front (&sema->waiters), struct thread, elem);
    thread_unblock (target);
  }
  thread_yield_if_not_highest_priority();
  intr_set_level (old_level);
}
// void
// sema_up (struct semaphore *sema) 
// {
//   ASSERT (sema != NULL);
//   enum intr_level old_level;
//   old_level = intr_disable ();

//   ASSERT(list_begin(&sema->waiters) != NULL);
//   if (!list_empty (&sema->waiters)){
//     // if(!thread_mlfqs){
//     //   // see sema_down()
//     //   list_sort(&(sema->waiters), thread_less_func, NULL);
//     // }
//     // // move from sema->waiters[] to ready_list[]
//     // thread_unblock (list_entry (list_pop_front (&sema->waiters),
//     //                             struct thread, elem));
//     struct list_elem *max = list_max(&sema->waiters, 
//                 (list_less_func*) thread_more_func, NULL);
//     list_remove(max);
//     struct thread *t = list_entry(max, struct thread, elem);
//     thread_unblock(t);
//   }
//   sema->value++;
//   thread_yield_if_not_highest_priority();
//   intr_set_level (old_level);
// }



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
  sema_init (&lock->semaphore, 1); // 1 lock 1 holder infinite waiters
}

/**
 * 1. update() 4 thread sys data structure:
 *    thread->holding_lock, thread->waiting_locks[], 
 *    lock->holder, lock->waiters[]
 * 2. nested_donate_priority()
 * 3. sema_down() block itself in lock's sema->waiters
 */ 
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  enum intr_level old_level = intr_disable();
  
  if(thread_mlfqs) {
    sema_down(&lock->semaphore);
    lock->holder = thread_current();
    intr_set_level(old_level);
    return;
  } 
  
  bool success = sema_try_down(&lock->semaphore); // ??
  if(!success){
    // ASSERT(is_thread(lock->holder));
    
    // 1. u() 4 + donate_priority()
    thread_current()->lock_waiting_on = lock;
    list_push_back(&lock->semaphore.waiters, &thread_current()->elem);
    thread_donate_priority(lock->holder);
    
    // 2. sema_down() block itself in lock's sema->waiters
    sema_down(&lock->semaphore); // <- end point where thread_block()
  }
  
  // <- restart point: waiter acquires lock when holder lock_release()
  
  // 3. u() 4 after lock acquired
  thread_current()->lock_waiting_on = NULL;
  lock->holder = thread_current();
  // list_push_back(&thread_current()->locks_acquired, &lock->thread_locks_list_elem);
  list_insert_ordered(&(lock->holder->locks_acquired), &(lock->thread_locks_list_elem),
      comparator_greater_lock_priority, NULL);

  intr_set_level(old_level);  
  
}



// for debugging
void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));
  struct lock *current_lock = lock;
  struct thread *t_holder = lock->holder; // current holder thread
  struct thread *t_current = thread_current();
  enum intr_level old_level = intr_disable();

  t_current->lock_waiting_on = lock;

  if(t_holder == NULL) {
    current_lock->holder->priority = t_current->priority;
  }

  while (t_holder != NULL && t_holder->priority < t_current->priority) {
    set_priority(t_holder, t_current->priority);
    
    if (current_lock->holder->priority < t_current->priority) {
      current_lock->holder->priority = t_current->priority;
    }
      current_lock = t_holder->lock_waiting_on;
      if(current_lock == NULL) break;
      t_holder = current_lock->holder;
  }
  sema_down (&lock->semaphore);
  lock->holder = thread_current ();
  lock->holder->lock_waiting_on = NULL;
  list_insert_ordered(&(lock->holder->locks_acquired), &(lock->thread_locks_list_elem),
      comparator_greater_lock_priority, NULL);
  
  intr_set_level(old_level);  
}



/**
 * 
 * 1. update() thread, lock data structure
 * 2.1 back to original priority 
 * 2.2. get donation from other locks aready acquired (1 thread could hold many locks at the same time)
 */ 
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
    sema_up (&lock->semaphore); // next waiter thread
    intr_set_level (old_level);
    return;
  }
  
  // 1. update() thread, lock data structure
  lock->holder = NULL;
  sema_up (&lock->semaphore); // release sema->waiters[] on this lock
  list_remove (&lock->thread_locks_list_elem); // thread{}->acquired_locks[]
  
  // 2.1 back to original priority 
  if (list_empty(&cur->locks_acquired)) { // only holding 1 lock
    cur->priority = cur->original_priority;
  
  // 2.2. get donation from other locks aready acquired (1 thread could hold many locks at the same time)
  } else {
      
    list_sort(&cur->locks_acquired, comparator_greater_lock_priority, NULL); // TODO seems duplicated ??
    struct lock *next_highest_lock_acquired = list_entry( list_front(&(cur->locks_acquired)), struct lock, thread_locks_list_elem);
    cur->priority = next_highest_lock_acquired->holder->priority;
    thread_yield_if_not_highest_priority();
  }
  
  // thread_set_priority(cur->original_priority); // set() ori_pri -> then nested_donate() ori_pri to priority
  intr_set_level (old_level);
}


bool
lock_try_acquire (struct lock *lock)
{
  bool success;

  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  success = sema_try_down (&lock->semaphore);
  if (success)
    lock->holder = thread_current ();
    list_push_back(&thread_current()->locks_acquired, &lock->thread_locks_list_elem);
  return success;
}

// check if lock is held by thread_current
bool
lock_held_by_current_thread (const struct lock *lock) 
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}


/************************************************************/
// CV 
// add priority to existing waiter_semaphores[] data structure











/************************************************************/

// weird hack to string()semaphore into list[] -> coz they want you to add priority
struct semaphore_elem // each item in cond->waiters[]
{ 
  struct list_elem elem;
  struct semaphore semaphore; // 1 sema 1 blocked thread
  int priority;
};


void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);
  list_init (&cond->waiters); // waiter_semaphores[]
}


void
cond_wait (struct condition *cond, struct lock *lock) 
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));
  
  sema_init (&waiter.semaphore, 0);
  
  // list_push_back (&cond->waiters, &waiter.elem); // waiter.elem == semaphore_elem
  waiter.priority = thread_current()->priority;
  list_insert_ordered (&cond->waiters, &(waiter.elem), cv_more_func, NULL);
  
  lock_release (lock);
  sema_down (&waiter.semaphore); // ready_list --> semaphore.waiters[]
  lock_acquire (lock);
}


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


void
cond_broadcast (struct condition *cond, struct lock *lock) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}

static bool
cv_more_func(const struct list_elem* a, const struct list_elem *b, void* aux UNUSED)
{
  const struct semaphore_elem* x = list_entry(a, struct semaphore_elem, elem);
  const struct semaphore_elem* y = list_entry(b, struct semaphore_elem, elem);
  ASSERT(x != NULL && y != NULL);
  return x->priority > y->priority;
}








/************************************************************/
// helper











/************************************************************/

bool thread_less_func(const struct list_elem *l, const struct list_elem *r, void *aux) {
  struct thread *lthread, *rthread;
  ASSERT (l != NULL && r != NULL);
  lthread = list_entry(l, struct thread, elem);
  rthread = list_entry(r, struct thread, elem);
  return (lthread->priority >= rthread->priority);
}

bool thread_more_func(const struct list_elem *l, const struct list_elem *r, void *aux) {
  struct thread *lthread, *rthread;
  ASSERT (l != NULL && r != NULL);
  lthread = list_entry(l, struct thread, elem);
  rthread = list_entry(r, struct thread, elem);
  return (lthread->priority < rthread->priority); // donated priority
}

static bool
comparator_greater_thread_priority(const struct list_elem* a, const struct list_elem *b, void* aux UNUSED)
{
  const struct thread* x = list_entry(a, struct thread, elem);
  const struct thread* y = list_entry(b, struct thread, elem);
  ASSERT(x != NULL && y != NULL);
  return x->priority > y->priority;
}

static bool
comparator_greater_lock_priority(const struct list_elem* a, const struct list_elem *b, void* aux UNUSED)
{
  const struct lock* x = list_entry(a, struct lock, thread_locks_list_elem);
  const struct lock* y = list_entry(b, struct lock, thread_locks_list_elem);
  ASSERT(x != NULL && y != NULL);
  return x->holder->priority > y->holder->priority;
}

// // holder receives highest priority from its locks' waiters
// void thread_recv_highest_waiter_priority(struct thread *holder){
//   if (!list_empty(&holder->locks_acquired)) {
//         struct list_elem *e;
//         // 1st for loop
//         for (e = list_begin(&holder->locks_acquired);
//                 e != list_end(&holder->locks_acquired);
//                 e = list_next(e)) {
//             struct lock *waiter_lock = list_entry(e, struct lock, thread_locks_list_elem);
//             // 2nd for loop
//             int highest_priority = highest_lock_priority(waiter_lock);
//             if (highest_priority > holder->priority) {
//                 holder->priority = highest_priority;
//             }
//         }
//     }
// }

// int highest_lock_priority(struct lock *lock){
//     struct list_elem *e;
//     int max_priority = PRI_MIN;
//     if (!list_empty(&lock->blocked_threads)) {
//         for (e = list_begin(&lock->blocked_threads);
//              e != list_end(&lock->blocked_threads);
//              e = list_next(e)) {
//             struct thread *waiter_thread = list_entry(e, struct thread, lock_elem);
//             int cur_priority = waiter_thread->priority;
//             if (cur_priority > max_priority) {
//                 max_priority = cur_priority;
//             }
//         }
//     }
//     return max_priority;
// }
