#ifndef LIB_THREAD_H
#define LIB_THREAD_H

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "../base/include.h"

typedef struct Thread {
  pthread_t thread;
} Thread;

typedef struct Mutex {
  pthread_mutex_t mutex;
} Mutex;

typedef struct Cond {
  pthread_cond_t cond;
} Cond;

Thread spawnThread(void * (*threadFn)(void *), void* thread_arg);
Mutex newMutex();
Cond newCond();
void lockMutex(Mutex* m);
void unlockMutex(Mutex* m);
void signalCond(Cond* cond);
void waitForCondSignal(Cond* cond, Mutex* mutex);

#endif //LIB_THREAD_H
