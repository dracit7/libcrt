
#ifndef _COROUTINE_H
#define _COROUTINE_H

#include <ucontext.h>

#include "common.h"

typedef void (*crt_func_t) (void);

enum crt_state {
  CRT_RUNNING = 0, // The coroutine is running
  CRT_YIELD,   // The coroutine has nothing to do now
  CRT_STOPPED, // The coroutine is not running
  CRT_EXITED,  // The coroutine has exited
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

crt_t* crt_create(crt_func_t func, void* arg, size_t stack_sz);
void crt_free(crt_t* crt);
int crt_yield(void);
int crt_yield_to_main(void);
void crt_wait(crt_t* crt);
void crt_wakeup(crt_t* crt);
crt_t* crt_getcur();

#endif