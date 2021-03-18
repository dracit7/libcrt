
#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>
#include <pthread.h>
#include <signal.h>

#include "coroutine.h"

#define  CRT_STACK_SIZE      4096
#define  CRT_MAX_MUTEX_NUM   128
#define  CRT_MAX_COND_NUM    128

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

static struct {
  char valid;
  crt_lock_t lock;
} crt_mutexes[CRT_MAX_MUTEX_NUM];

static struct {
  char valid;
  crt_cond_t cond;
} crt_conds[CRT_MAX_COND_NUM];

#define CRT_VAR_INIT(id, arr, size, obj) ({\
  int ret = 0;\
  if (!(*(id))) {\
    for (int i = 1; i < size; i++) \
      if (!arr[i].valid) {\
        crt_##obj##_init(&arr[i].obj);\
        arr[i].valid = 1;\
        *(id) = i;\
        break;\
      }\
    if (!(*(id))) ret = -EAGAIN;\
  }\
  (ret);\
})

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
    crt_create((crt_func_t)start_routine, arg, CRT_STACK_SIZE);

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

  crt_free(t->routine);
  free(t);
  
  return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {

  /* 
   * If the mutex is not initialized, initialize it.
   * 
   * The initialization can not be done by overriding pthread_mutex_init()
   * because of PTHREAD_MUTEX_INITIALIZER.
   */
  int ret = CRT_VAR_INIT(&mutex->__align, crt_mutexes, CRT_MAX_MUTEX_NUM, lock);
  if (ret) return ret;

  crt_lock(&crt_mutexes[mutex->__align].lock, 1);
  return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
  int ret = CRT_VAR_INIT(&mutex->__align, crt_mutexes, CRT_MAX_MUTEX_NUM, lock);
  if (ret) return ret;

  if (crt_lock(&crt_mutexes[mutex->__align].lock, 0)) return 0;
  else return -EBUSY;
}

int pthread_mutex_unlock(pthread_mutex_t* mutex) {
  if (!crt_mutexes[mutex->__align].valid) return -EINVAL;
  return crt_unlock(&crt_mutexes[mutex->__align].lock);
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
  crt_mutexes[mutex->__align].valid = 0;
  return 0;
}

int pthread_sigmask(int how, const sigset_t *set, sigset_t *oldset) {
  return sigprocmask(how, set, oldset);
}

int pthread_cond_wait(pthread_cond_t *restrict cond, pthread_mutex_t *restrict mutex) {
  if (!crt_mutexes[mutex->__align].valid) return -EINVAL;

  int ret = CRT_VAR_INIT(&cond->__align, crt_conds, CRT_MAX_COND_NUM, cond);
  if (ret) return ret;

  return crt_cond_wait(&crt_conds[cond->__align].cond, &crt_mutexes[mutex->__align].lock);
}

int pthread_cond_signal(pthread_cond_t *cond) {
  int ret = CRT_VAR_INIT(&cond->__align, crt_conds, CRT_MAX_COND_NUM, cond);
  if (ret) return ret;
  
  return crt_cond_signal(&crt_conds[cond->__align].cond);
}

int pthread_cond_broadcast(pthread_cond_t *cond) {
  int ret = CRT_VAR_INIT(&cond->__align, crt_conds, CRT_MAX_COND_NUM, cond);
  if (ret) return ret;
  
  return crt_cond_broadcast(&crt_conds[cond->__align].cond);
}

int pthread_cond_destroy(pthread_cond_t *cond) {
  crt_conds[cond->__align].valid = 0;
  return 0;
}
