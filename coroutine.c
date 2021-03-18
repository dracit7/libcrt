
#include "coroutine.h"

/* 
 * Special behaviors of coroutines in libpcrt:
 * 
 * 1. Non-main coroutines would not switch to another yielding non-main
 *  coroutine. The main coroutine could switch to any coroutine.
 */

crt_list_t rqueue; /* The run queue. */

static crt_t main_crt; /* The main coroutine. */
static crt_t* cur_crt; /* The running coroutine. */

static char  main_waiting; /* Is the main coroutine waiting? */

#define crt_append_to_list(crt, list) do {\
  if (!(list)->head) (list)->head = (list)->tail = crt;\
  else (list)->tail = (list)->tail->next = crt;\
  (crt)->next = 0;\
  (list)->cnt++;\
} while (0)

#define crt_drop_list_head(list) ({\
  crt_t* _orig_head = (list)->head;\
  (list)->head = (list)->head->next;\
  (list)->cnt--;\
  _orig_head;\
})

/* 
 * Append a coroutine @crt to the run queue and set
 * its state to CRT_READY.
 */
static void crt_ready(crt_t* crt) {
  crt_append_to_list(crt, &rqueue);
  crt->state = CRT_READY;
}

/* 
 * Switch the context of @from to @to. This function returns
 * when this coroutine is scheduled again.
 */
static void crt_switch(crt_t* from, crt_t* to) {
  if (to == &main_crt) cur_crt = NULL;
  else {
    cur_crt = to;
    cur_crt->state = CRT_RUNNING;
  }

  swapcontext(&from->context, &to->context);

  from->state = CRT_RUNNING;
}

/* 
 * Find the first coroutine that is ready and run it.
 * If there'is no such coroutine, return NULL.
 */
static crt_t* crt_schedule() {
  if (!rqueue.head) return NULL;

  /* 
   * The main coroutine should be handled seperately because
   * it can yield to some non-main coroutines that's yielding.
   */
  if (cur_crt == &main_crt) {
    crt_t* to = crt_drop_list_head(&rqueue);

    crt_switch(cur_crt, to);
    return to;
  }

  /* 
   * If the head is ready, switch to it directly.
   */
  if (rqueue.head->state == CRT_READY) {
    crt_t* to = crt_drop_list_head(&rqueue);
    crt_append_to_list(cur_crt, &rqueue);

    crt_switch(cur_crt, to);
    return to;
  }

  /* 
   * Seek for an available coroutine in the queue.
   */
  crt_t* crt;
  for (crt = rqueue.head; crt->next; crt = crt->next)
    if (crt->next->state == CRT_READY) break;
  
  /* 
   * If can not find a not yielding coroutine, just return.
   */
  if (!crt->next) return NULL;

  /* 
   * Pop the coroutine to run next out of queue.
   */
  crt_t* to = crt->next;
  crt->next = crt->next->next;
  rqueue.cnt--;

  /* 
   * Append the current coroutine to queue.
   */
  crt_append_to_list(cur_crt, &rqueue);

  crt_switch(cur_crt, to);
  return to;
}

crt_t* crt_create(crt_func_t func, void* arg, size_t stack_sz) {
  debug("crt_create");

  /* 
   * Allocate and initialize a coroutine.
   */
  crt_t* crt = malloc(sizeof(crt_t));
  crt->stack = malloc(stack_sz);

  crt->stack_sz = stack_sz;
  crt->func = func;
  crt->arg = arg;
  crt->next = 0;
  crt->state = CRT_STOPPED;

  /* 
   * Build the ucontext of the coroutine.
   */
  if (getcontext(&crt->context) == -1)
    fault("getcontext");
  
  crt->context.uc_stack.ss_sp = crt->stack;
  crt->context.uc_stack.ss_size = stack_sz;
  crt->context.uc_link = &main_crt.context;

  makecontext(&crt->context, func, 1, arg);

  /* 
   * Add this coroutine to the run queue.
   */
  crt_ready(crt);

  return crt;
}

void crt_free(crt_t* crt) {
  free(crt->stack);
  free(crt);
}

/* 
 * Hand out the control flow to another non-main coroutine.
 * Returns 0 if there's no coroutine waiting to run; returns
 * 1 elsewise.
 */
int crt_yield(void) {
  debug("crt_yield from %lx", cur_crt);

  crt_t* old_cur_crt = cur_crt;

  /* 
   * If cur_crt is not set, the caller must be the main coroutine.
   */
  if (!cur_crt) cur_crt = &main_crt;

  cur_crt->state = CRT_YIELD;

  /* 
   * Run the first coroutine in the run queue.
   */
  crt_t* scheduled = crt_schedule();

  /* 
   * Restore cur_crt. This is for the occasion where we return
   * to the main thread and cur_crt should be reset to NULL.
   */
  cur_crt = old_cur_crt;

  /* 
   * crt_schedule returns NULL means that there is no coroutine
   * waiting to run. Since the thread was not really switched,
   * the state should be changed back.
   */
  if (!scheduled) {
    cur_crt->state = CRT_RUNNING;
    return 0;
  }

  /* 
   * @scheduled points to the coroutine we switched to, which sets
   * scheduled->state to CRT_YIELD if it yielded, so it must has exited
   * if scheduled->state is CRT_RUNNING.
   */
  if (scheduled->state == CRT_RUNNING) 
    scheduled->state = CRT_EXITED;
  
  return 1;
}

/* 
 * Try to come back to the main coroutine. Returns 1 if succeeded, 
 * 0 if the main coroutine is waiting on some non-main coroutines.
 */
int crt_yield_to_main(void) {
  debug("crt_yield_to_main (main %s)", main_waiting ? "waiting" : "available");

  if (!cur_crt) fault("can not yield to main when already in main!");

  if (main_waiting) return 0;

  cur_crt->state = CRT_YIELD;
  crt_append_to_list(cur_crt, &rqueue);

  crt_switch(cur_crt, &main_crt);
  return 1;
}

/* 
 * Must be called by the main coroutine. Wait until the specified
 * coroutine exits.
 */
void crt_wait(crt_t* crt) {
  debug("crt_wait %lx", crt);

  if (cur_crt) fault("only the main routine can wait for a non-main routine!");
  
  main_waiting = 1;
  while (crt->state != CRT_EXITED) crt_yield();
  main_waiting = 0;
}

/* 
 * Wake a yielding coroutine up and switch to it.
 */
void crt_wakeup(crt_t* crt) {
  debug("crt_wakeup %lx", crt);

  crt_ready(cur_crt);
  crt_switch(cur_crt, crt);
}

/* 
 * Get the current coroutine's task struct address.
 */
crt_t* crt_getcur() {
  return cur_crt;
}

/* 
 * Initialize a new coroutine lock.
 * 
 * Some applications leverage different implementations of malloc()
 * that calls pthread_mutex_*(), which would cause infinite recursion
 * if we use malloc() in crt_lock_*().
 */
void crt_lock_init(crt_lock_t* lock) {
  lock->owner = NULL;
  lock->wait_list.head = lock->wait_list.tail = NULL;
  lock->wait_list.cnt = 0;
}

/* 
 * Try to hold the lock. If the lock is holding by someone else,
 * join the waitlist and hand over the control until the lock is
 * released if @block is true, return 0 elsewise.
 * 
 * The state of a coroutine would be CRT_LOCKED IF and ONLY IF it's
 * in a lock's wait list.
 */
int crt_lock(crt_lock_t* lock, int block) {
  if (block) debug("crt_lock %lx", lock);
  else debug("crt_trylock %lx", lock);

  if (!lock->owner) {
    debug("acquired by %lx", cur_crt);
    lock->owner = cur_crt ? cur_crt : &main_crt;
    return 1;
  }

  if (!block) return 0;
  debug("failed (lock is held by %lx)", lock->owner);

  if (!cur_crt) {
    crt_append_to_list(cur_crt, &lock->wait_list);
    cur_crt->state = CRT_LOCKED;
    crt_yield_to_main();

  } else crt_wakeup(lock->owner);

  return 1;
}

/* 
 * Release the lock and set the first coroutine in the wait list as
 * the owner.
 */
int crt_unlock(crt_lock_t* lock) {
  debug("crt_unlock %lx", lock);
  if (!lock->owner) return -EINVAL;

  if (lock->wait_list.head) {
    crt_t* crt = crt_drop_list_head(&lock->wait_list);
    lock->owner = crt;
    crt_ready(crt);
  }

  return 0;
}

/* 
 * Initialize a new coroutine condition variable.
 */
void crt_cond_init(crt_cond_t* cond) {
  cond->wait_list.head = cond->wait_list.tail = NULL;
  cond->wait_list.cnt = 0;
}

int crt_cond_wait(crt_cond_t* cond, crt_lock_t* lock) {
  debug("crt_cond_wait on %lx", cond);
  if (lock->owner != cur_crt) return -EINVAL;

  crt_append_to_list(cur_crt, &cond->wait_list);
  cur_crt->state = CRT_LOCKED;

  crt_unlock(lock);
  crt_schedule();
  crt_lock(lock, 0);

  return 0;
}

int crt_cond_signal(crt_cond_t* cond) {
  debug("crt_cond_signal on %lx", cond);
  crt_t* crt = crt_drop_list_head(&cond->wait_list);
  crt_ready(crt);

  return 0;
}

int crt_cond_broadcast(crt_cond_t* cond) {
  debug("crt_cond_broadcast on %lx", cond);
  for (int i = 0; i < cond->wait_list.cnt; i++) {
    crt_t* crt = crt_drop_list_head(&cond->wait_list);
    crt_ready(crt);
  }

  return 0;
}