
#ifndef _COROUTINE_H
#define _COROUTINE_H

#include <ucontext.h>

#include "common.h"

typedef void (*crt_func_t) (void);

enum crt_state {
  CRT_RUNNING = 0, // The coroutine is running
  CRT_READY,       // The coroutine is not running but ready to run
  CRT_YIELD,       // The coroutine has nothing to do now
  CRT_STOPPED,     // The coroutine is not running
  CRT_LOCKED,      // The coroutine is trying to hold a lock
  CRT_EXITED,      // The coroutine has exited
};

typedef struct coroutine {
  ucontext_t context;
  struct coroutine * next;
  crt_func_t func;
  void* arg;
  char* stack;
  size_t stack_sz;
  int state;
} crt_t;

typedef struct {
  crt_t* head;
  crt_t* tail;
  size_t cnt;
} crt_list_t;

typedef struct {
  crt_t* owner;
  crt_list_t wait_list;
} crt_lock_t;

typedef struct {
  crt_list_t wait_list;
} crt_cond_t;

crt_t* crt_create(crt_func_t func, void* arg, size_t stack_sz);
void crt_free(crt_t* crt);
int crt_yield(void);
int crt_yield_to_main(void);
void crt_wait(crt_t* crt);
void crt_wakeup(crt_t* crt);
crt_t* crt_getcur();
void crt_lock_init(crt_lock_t* lock);
int crt_lock(crt_lock_t* lock, int block);
int crt_unlock(crt_lock_t* lock);
void crt_cond_init(crt_cond_t* cond);
int crt_cond_wait(crt_cond_t* cond, crt_lock_t* lock);
int crt_cond_signal(crt_cond_t* cond);
int crt_cond_broadcast(crt_cond_t* cond);

#endif