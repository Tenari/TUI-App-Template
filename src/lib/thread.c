#include "thread.h"

typedef struct ThreadQueue {
  void* items;
  u32 type_size;
  u32 max;
  u32 head;
  u32 tail;
  u32 count;
  Mutex mutex;
  Cond not_empty;
  Cond not_full;
} ThreadQueue;

Thread spawnThread(void * (*threadFn)(void *), void* thread_arg) {
  pthread_t thread;
  pthread_create(&thread, NULL, threadFn, thread_arg);
  Thread result = { thread };
  return result;
}

Mutex newMutex() {
  Mutex result = { PTHREAD_MUTEX_INITIALIZER };
  return result;
}

Cond newCond() {
  Cond result = { 0 };
  pthread_cond_init(&result.cond, NULL);
  return result;
}

void lockMutex(Mutex* m) {
  pthread_mutex_lock(&m->mutex);
}

void unlockMutex(Mutex* m) {
  pthread_mutex_unlock(&m->mutex);
}

void signalCond(Cond* cond) {
  pthread_cond_signal(&cond->cond);
}

void waitForCondSignal(Cond* cond, Mutex* mutex) {
  pthread_cond_wait(&cond->cond, &mutex->mutex);
}

fn ThreadQueue newThreadQueue(Arena* a, u32 type_size, u32 items_max) {
  ThreadQueue result = {0};
  result.type_size = type_size;
  result.max = items_max;
  result.items = arenaAllocArraySized(a, type_size, items_max);
  result.mutex = newMutex();
  result.not_full = newCond();
  result.not_empty = newCond();
  return result;
}

fn void threadSafeQueuePush(ThreadQueue* queue, void* item) {
  lockMutex(&queue->mutex); {
    while (queue->count == queue->max) {
      waitForCondSignal(&queue->not_full, &queue->mutex);
    }

    memcpy(queue->items + (queue->tail * (sizeof item)), item, queue->type_size);
    queue->tail = (queue->tail + 1) % queue->max;
    queue->count++;

    signalCond(&queue->not_empty);
  } unlockMutex(&queue->mutex);
}

fn void* threadSafeQueuePop(ThreadQueue* q) {
  void* result = NULL;

  lockMutex(&q->mutex); {
    while (q->count == 0) {
        waitForCondSignal(&q->not_empty, &q->mutex);
    }

    result = &q->items[q->head];
    q->head = (q->head + 1) % q->max;
    q->count--;

    signalCond(&q->not_full);
  } unlockMutex(&q->mutex);

  return result;
}

// immediately returns NULL if there's nothing in the ThreadQueue
fn void* threadSafeNonblockingQueuePop(ThreadQueue* q, void* copy_target, u64 len) {
  void* result = NULL;

  lockMutex(&q->mutex); {
    if (q->count > 0) {
      result = &q->items[q->head];
      MemoryCopy(copy_target, result, len);
      q->head = (q->head + 1) % q->max;
      q->count--;

      signalCond(&q->not_full);
    }
  } unlockMutex(&q->mutex);

  return result;
}

