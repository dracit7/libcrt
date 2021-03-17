
#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>
#include <pthread.h>

#include "coroutine.h"

#define  FLEXSC_STACK_SIZE   4096
#define  FLEXSC_MAX_THREADS  4096

static int (*orig_pthread_create) (pthread_t *thread, const pthread_attr_t *attr,
  void *(*start_routine) (void *), void *arg);
static int (*orig_pthread_join) (pthread_t thread, void **retval);

typedef struct crt_thread {
  pthread_t id;
  crt_t* routine;
  struct crt_thread* next;
} crt_thread;

static struct {
  crt_thread* head;
  crt_thread* tail;
  int id_space;
} crt_threads;

#define FREE_CRT_THREAD(thread) do {\
  crt_free((thread)->routine);\
  free(thread);\
} while (0)

int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine) (void *), void *arg)
{

  /* 
   * Allocate a new thread structure.
   */
  if (!crt_threads.head) 
    crt_threads.head = crt_threads.tail = malloc(sizeof(crt_thread));
  else crt_threads.tail = crt_threads.tail->next = malloc(sizeof(crt_thread));

  /* 
   * Initialize it.
   */
  *thread = crt_threads.tail->id = ++crt_threads.id_space;
  crt_threads.tail->next = 0;
  crt_threads.tail->routine = 
    crt_create((crt_func_t)start_routine, arg, FLEXSC_STACK_SIZE);

  info("Created user-level thread %ld (%lx)", *thread, crt_threads.tail->routine);

  /* 
   * Switch to it.
   */
  crt_yield();

  return 0;
}

int pthread_join(pthread_t thread, void **retval)
{

  /* 
   * Find the coroutine to wait for by the thread's ID.
   */
  crt_thread* t;
  for (t = crt_threads.head; t; t = t->next)
    if (t->id == thread) break;
  
  if (!t) return -ESRCH;

  /* 
   * Call crt's interface to wait until this thread exits.
   */
  info("Waiting for thread %ld (%lx) to terminate...", thread, t->routine);
  crt_wait(t->routine);

  /* 
   * Free the exited thread.
   */
  if (t == crt_threads.head) crt_threads.head = t->next;
  else {
    crt_thread* th;
    for (th = crt_threads.head; th->next != t; th = th->next) ;
    th->next = t->next;
  }

  FREE_CRT_THREAD(t);
  
  return 0;
}