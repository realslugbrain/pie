#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct PieString {
  const char *ptr;
  size_t len;
} PieString;

extern PieString pie_int_to_string(long long n);
extern PieString pie_float_to_string(double f);

static void append_string(char **buf, size_t *len, size_t *cap, const char *src,
                          size_t src_len) {
  while (*len + src_len > *cap) {
    size_t new_cap = *cap ? *cap * 2 : 64;
    char *new_buf = (char *)realloc(*buf, new_cap);
    if (!new_buf)
      return;
    *buf = new_buf;
    *cap = new_cap;
  }
  memcpy(*buf + *len, src, src_len);
  *len += src_len;
}

static void append_value(char **buf, size_t *len, size_t *cap,
                         long long value) {
  PieString s = pie_int_to_string(value);
  if (s.ptr && s.len > 0) {
    append_string(buf, len, cap, s.ptr, s.len);
    free((void *)s.ptr);
  }
}

static void append_float_value(char **buf, size_t *len, size_t *cap,
                               double value) {
  PieString s = pie_float_to_string(value);
  if (s.ptr && s.len > 0) {
    append_string(buf, len, cap, s.ptr, s.len);
    free((void *)s.ptr);
  }
}

PieString pie_format_0(const char *tmpl, size_t tmpl_len) {
  char *buf = (char *)malloc(tmpl_len);
  size_t len = 0;
  size_t cap = tmpl_len;
  if (!buf) {
    PieString empty = {NULL, 0};
    return empty;
  }
  append_string(&buf, &len, &cap, tmpl, tmpl_len);
  PieString result = {buf, len};
  return result;
}

PieString pie_format_1(const char *tmpl, size_t tmpl_len, long long v0) {
  char *buf = NULL;
  size_t len = 0;
  size_t cap = 0;
  size_t pos = 0;
  while (pos < tmpl_len) {
    if (pos + 1 < tmpl_len && tmpl[pos] == '{' && tmpl[pos + 1] == '}') {
      if (len == 0 && cap == 0) {
        buf = (char *)malloc(tmpl_len + 32);
        cap = tmpl_len + 32;
      }
      append_value(&buf, &len, &cap, v0);
      pos += 2;
    } else {
      size_t start = pos;
      while (pos < tmpl_len && !(pos + 1 < tmpl_len && tmpl[pos] == '{' &&
                                 tmpl[pos + 1] == '}')) {
        pos++;
      }
      if (len == 0 && cap == 0) {
        buf = (char *)malloc(tmpl_len + 32);
        cap = tmpl_len + 32;
      }
      append_string(&buf, &len, &cap, tmpl + start, pos - start);
    }
  }
  if (!buf) {
    buf = (char *)malloc(1);
    cap = 1;
  }
  PieString result = {buf, len};
  return result;
}

PieString pie_format_2(const char *tmpl, size_t tmpl_len, long long v0,
                       long long v1) {
  char *buf = NULL;
  size_t len = 0;
  size_t cap = 0;
  size_t pos = 0;
  size_t arg_idx = 0;
  while (pos < tmpl_len) {
    if (pos + 1 < tmpl_len && tmpl[pos] == '{' && tmpl[pos + 1] == '}') {
      if (len == 0 && cap == 0) {
        buf = (char *)malloc(tmpl_len + 64);
        cap = tmpl_len + 64;
      }
      if (arg_idx == 0)
        append_value(&buf, &len, &cap, v0);
      else
        append_value(&buf, &len, &cap, v1);
      arg_idx++;
      pos += 2;
    } else {
      size_t start = pos;
      while (pos < tmpl_len && !(pos + 1 < tmpl_len && tmpl[pos] == '{' &&
                                 tmpl[pos + 1] == '}')) {
        pos++;
      }
      if (len == 0 && cap == 0) {
        buf = (char *)malloc(tmpl_len + 64);
        cap = tmpl_len + 64;
      }
      append_string(&buf, &len, &cap, tmpl + start, pos - start);
    }
  }
  if (!buf) {
    buf = (char *)malloc(1);
    cap = 1;
  }
  PieString result = {buf, len};
  return result;
}
