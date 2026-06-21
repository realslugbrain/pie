#ifndef PIE_THREADS_H
#define PIE_THREADS_H

#include <stddef.h>

typedef struct {
  void *fn;
  void *env;
} PieThreadArg;

typedef struct PieChannel {
  void *data;
  size_t elem_size;
  size_t head;
  size_t tail;
  size_t count;
  size_t capacity;
  int closed;
  void *mutex;
  void *not_empty;
  void *not_full;
} PieChannel;

void *pie_thread_spawn(void *fn, void *env);
void pie_thread_join(void *handle);
void *pie_thread_mutex_create(void);
void pie_thread_mutex_lock(void *mutex);
void pie_thread_mutex_unlock(void *mutex);
void pie_thread_mutex_destroy(void *mutex);
void pie_thread_sleep_ms(int ms);

void *pie_channel_create(size_t elem_size, size_t capacity);
void pie_channel_send(void *ch_ptr, void *elem);
int pie_channel_recv(void *ch_ptr, void *out_elem);
void pie_channel_close(void *ch_ptr);
int pie_channel_closed(void *ch_ptr);
void pie_channel_destroy(void *ch_ptr);

#endif
