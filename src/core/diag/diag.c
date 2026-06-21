#include "pie/core/diag/diag.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *pie_diag_strdup(const char *s) {
  size_t len = strlen(s);
  char *copy = (char *)malloc(len + 1);
  if (!copy) {
    return NULL;
  }
  memcpy(copy, s, len + 1);
  return copy;
}

void pie_diag_init(PieDiagnosticBag *bag) {
  bag->items = NULL;
  bag->count = 0;
  bag->capacity = 0;
  bag->has_error = 0;
}

void pie_diag_free(PieDiagnosticBag *bag) {
  for (size_t i = 0; i < bag->count; i++) {
    free(bag->items[i]);
  }
  free(bag->items);
  pie_diag_init(bag);
}

void pie_diag_error(PieDiagnosticBag *bag, const char *message) {
  if (bag->count == bag->capacity) {
    size_t next_capacity = bag->capacity ? bag->capacity * 2 : 8;
    char **next = (char **)realloc(bag->items, next_capacity * sizeof(char *));
    if (!next) {
      fprintf(stderr, "fatal: out of memory while recording diagnostic\n");
      abort();
    }
    bag->items = next;
    bag->capacity = next_capacity;
  }

  char *copy = pie_diag_strdup(message);
  if (!copy) {
    fprintf(stderr, "fatal: out of memory while recording diagnostic\n");
    abort();
  }
  bag->items[bag->count++] = copy;
  bag->has_error = 1;
}

void pie_diag_errorf(PieDiagnosticBag *bag, const char *fmt, ...) {
  char stack_buf[1024];
  va_list args;
  va_start(args, fmt);
  int needed = vsnprintf(stack_buf, sizeof(stack_buf), fmt, args);
  va_end(args);

  if (needed < 0) {
    pie_diag_error(bag, "internal diagnostic formatting error");
    return;
  }

  if ((size_t)needed < sizeof(stack_buf)) {
    pie_diag_error(bag, stack_buf);
    return;
  }

  char *heap_buf = (char *)malloc((size_t)needed + 1);
  if (!heap_buf) {
    fprintf(stderr, "fatal: out of memory while formatting diagnostic\n");
    abort();
  }

  va_start(args, fmt);
  vsnprintf(heap_buf, (size_t)needed + 1, fmt, args);
  va_end(args);

  pie_diag_error(bag, heap_buf);
  free(heap_buf);
}

void pie_diag_print(const PieDiagnosticBag *bag) {
  for (size_t i = 0; i < bag->count; i++) {
    fprintf(stderr, "pie: error: %s\n", bag->items[i]);
  }
}
