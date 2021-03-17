
#include "coroutine.h"

/* 
 * Special behaviors of coroutines in libpcrt:
 * 
 * 1. Non-main coroutines would not switch to another yielding non-main
 *  coroutine. The main coroutine could switch to any coroutine.
 * 
 * 2. Only the main coroutine would return from swapcontext(). Non-main
 *  coroutines either yield to another coroutine initiatively or exit.
 */

static struct {
  crt_t* head;
  crt_t* tail;
  int ncrt;
} rqueue; /* The run queue. */

static crt_t main_crt; /* The main coroutine. */
static crt_t* cur_crt; /* The running coroutine. */

static char  main_waiting;

/* 
 * Append a coroutine @crt to the run queue.
 */
static void crt_ready(crt_t* crt) {
  if (!rqueue.head) rqueue.head = rqueue.tail = crt;
  else rqueue.tail = rqueue.tail->next = crt;
  crt->next = 0;
  rqueue.ncrt++;
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
 * Find the first coroutine that is not yielding and run it.
 * If there'is no such coroutine, return NULL.
 */
static crt_t* crt_schedule() {
  if (!rqueue.head) return NULL;

  /* 
   * The main coroutine should be handled seperately because
   * it can yield to some non-main coroutines that's yielding.
   */
  if (cur_crt == &main_crt) {
    crt_t* to = rqueue.head;
    rqueue.head = rqueue.head->next;
    rqueue.ncrt--;

    crt_switch(cur_crt, to);
    return to;
  }

  /* 
   * If the head is not yielding, switch to it directly.
   */
  if (rqueue.head->state != CRT_YIELD) {
    
    crt_t* to = rqueue.head;
    rqueue.head = rqueue.head->next;
    rqueue.ncrt--;

    crt_ready(cur_crt);
    crt_switch(cur_crt, to);
    return to;
  }

  /* 
   * Seek for an available coroutine in the queue.
   */
  crt_t* crt;
  for (crt = rqueue.head; crt->next; crt = crt->next)
    if (crt->next->state != CRT_YIELD) break;
  
  /* 
   * If can not find a not yielding coroutine, just return.
   */
  if (!crt->next) return NULL;

  /* 
   * Pop the coroutine to run next out of queue.
   */
  crt_t* to = crt->next;
  crt->next = crt->next->next;
  rqueue.ncrt--;

  /* 
   * Append the current coroutine to queue.
   */
  crt_ready(cur_crt);

  crt_switch(cur_crt, to);
  return to;
}

crt_t* crt_create(crt_func_t func, void* arg, size_t stack_sz) {
  log(call, "crt_create");

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
  log(call, "crt_yield from %lx", cur_crt);

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
  log(call, "crt_yield_to_main (main %s)", main_waiting ? "waiting" : "available");

  if (!cur_crt) fault("can not yield to main when already in main!");

  if (main_waiting) return 0;

  crt_ready(cur_crt);
  cur_crt->state = CRT_YIELD;
  crt_switch(cur_crt, &main_crt);

  return 1;
}

/* 
 * Must be called by the main coroutine. Wait until the specified
 * coroutine exits.
 */
void crt_wait(crt_t* crt) {
  log(call, "crt_wait %lx", crt);

  if (cur_crt) fault("only the main routine can wait for a non-main routine!");
  
  main_waiting = 1;
  while (crt->state != CRT_EXITED) crt_yield();
  main_waiting = 0;
}

/* 
 * Wake a yielding coroutine up and switch to it.
 */
void crt_wakeup(crt_t* crt) {
  log(call, "crt_wakeup %lx", crt);

  crt_ready(cur_crt);
  cur_crt->state = CRT_YIELD;
  crt_switch(cur_crt, crt);
}

/* 
 * Get the current coroutine's task struct address.
 */
crt_t* crt_getcur() {
  return cur_crt;
}