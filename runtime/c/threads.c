#define _GNU_SOURCE
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "pie_threads.h"

typedef struct PieChannelElem {
  struct PieChannelElem *next;
  char data[];
} PieChannelElem;

static void *pie_thread_trampoline(void *arg) {
  PieThreadArg ta = *(PieThreadArg *)arg;
  free(arg);
  typedef void (*ThreadFn)(void *);
  ThreadFn fn = (ThreadFn)ta.fn;
  fn(ta.env);
  return NULL;
}

void *pie_thread_spawn(void *fn, void *env) {
  PieThreadArg *arg = (PieThreadArg *)malloc(sizeof(PieThreadArg));
  if (!arg)
    return NULL;
  arg->fn = fn;
  arg->env = env;

  pthread_t *thread = (pthread_t *)malloc(sizeof(pthread_t));
  if (!thread) {
    free(arg);
    return NULL;
  }

  if (pthread_create(thread, NULL, pie_thread_trampoline, arg) != 0) {
    free(arg);
    free(thread);
    return NULL;
  }

  return thread;
}

void pie_thread_join(void *handle) {
  if (!handle)
    return;
  pthread_t *thread = (pthread_t *)handle;
  pthread_join(*thread, NULL);
  free(thread);
}

void *pie_thread_mutex_create(void) {
  pthread_mutex_t *mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
  if (!mutex)
    return NULL;
  pthread_mutex_init(mutex, NULL);
  return mutex;
}

void pie_thread_mutex_lock(void *mutex) {
  if (!mutex)
    return;
  pthread_mutex_lock((pthread_mutex_t *)mutex);
}

void pie_thread_mutex_unlock(void *mutex) {
  if (!mutex)
    return;
  pthread_mutex_unlock((pthread_mutex_t *)mutex);
}

void pie_thread_mutex_destroy(void *mutex) {
  if (!mutex)
    return;
  pthread_mutex_destroy((pthread_mutex_t *)mutex);
  free(mutex);
}

void pie_thread_sleep_ms(int ms) {
  struct timespec ts;
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000L;
  nanosleep(&ts, NULL);
}

void *pie_channel_create(size_t elem_size, size_t capacity) {
  PieChannel *ch = (PieChannel *)malloc(sizeof(PieChannel));
  if (!ch)
    return NULL;

  ch->elem_size = elem_size;
  ch->capacity = capacity > 0 ? capacity : 64;
  ch->head = 0;
  ch->tail = 0;
  ch->count = 0;
  ch->closed = 0;
  ch->data = malloc(elem_size * ch->capacity);
  ch->mutex = pie_thread_mutex_create();
  ch->not_empty = NULL;
  ch->not_full = NULL;

  if (!ch->data || !ch->mutex) {
    free(ch->data);
    free(ch->mutex);
    free(ch);
    return NULL;
  }

  return ch;
}

void pie_channel_send(void *ch_ptr, void *elem) {
  if (!ch_ptr)
    return;
  PieChannel *ch = (PieChannel *)ch_ptr;

  pthread_mutex_lock((pthread_mutex_t *)ch->mutex);
  while (ch->count >= ch->capacity && !ch->closed) {
    pthread_mutex_unlock((pthread_mutex_t *)ch->mutex);
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 1000000;
    nanosleep(&ts, NULL);
    pthread_mutex_lock((pthread_mutex_t *)ch->mutex);
  }

  if (ch->closed) {
    pthread_mutex_unlock((pthread_mutex_t *)ch->mutex);
    return;
  }

  memcpy((char *)ch->data + (ch->tail * ch->elem_size), elem, ch->elem_size);
  ch->tail = (ch->tail + 1) % ch->capacity;
  ch->count++;
  pthread_mutex_unlock((pthread_mutex_t *)ch->mutex);
}

int pie_channel_recv(void *ch_ptr, void *out_elem) {
  if (!ch_ptr)
    return 0;
  PieChannel *ch = (PieChannel *)ch_ptr;

  pthread_mutex_lock((pthread_mutex_t *)ch->mutex);
  while (ch->count == 0 && !ch->closed) {
    pthread_mutex_unlock((pthread_mutex_t *)ch->mutex);
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 1000000;
    nanosleep(&ts, NULL);
    pthread_mutex_lock((pthread_mutex_t *)ch->mutex);
  }

  if (ch->count == 0 && ch->closed) {
    pthread_mutex_unlock((pthread_mutex_t *)ch->mutex);
    return 0;
  }

  memcpy(out_elem, (char *)ch->data + (ch->head * ch->elem_size),
         ch->elem_size);
  ch->head = (ch->head + 1) % ch->capacity;
  ch->count--;
  pthread_mutex_unlock((pthread_mutex_t *)ch->mutex);
  return 1;
}

void pie_channel_close(void *ch_ptr) {
  if (!ch_ptr)
    return;
  PieChannel *ch = (PieChannel *)ch_ptr;
  pthread_mutex_lock((pthread_mutex_t *)ch->mutex);
  ch->closed = 1;
  pthread_mutex_unlock((pthread_mutex_t *)ch->mutex);
}

int pie_channel_closed(void *ch_ptr) {
  if (!ch_ptr)
    return 1;
  PieChannel *ch = (PieChannel *)ch_ptr;
  pthread_mutex_lock((pthread_mutex_t *)ch->mutex);
  int closed = ch->closed;
  pthread_mutex_unlock((pthread_mutex_t *)ch->mutex);
  return closed;
}

void pie_channel_destroy(void *ch_ptr) {
  if (!ch_ptr)
    return;
  PieChannel *ch = (PieChannel *)ch_ptr;
  pie_thread_mutex_destroy(ch->mutex);
  free(ch->data);
  free(ch);
}
