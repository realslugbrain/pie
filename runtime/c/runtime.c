#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <stdio.h>

void *pie_malloc(size_t size) { return malloc(size); }

void pie_free(void *ptr) { free(ptr); }

const char *pie_runtime_version(void) { return "pie-runtime-c-stub-0.1.0"; }

void pie_runtime_keep_linker(void) { (void)sizeof(size_t); }

typedef struct PieString {
  const char *ptr;
  size_t len;
} PieString;

PieString pie_string_concat(const char *ptr1, size_t len1, const char *ptr2,
                            size_t len2) {
  char *buf = (char *)malloc(len1 + len2);
  if (!buf) {
    PieString empty = {NULL, 0};
    return empty;
  }
  memcpy(buf, ptr1, len1);
  memcpy(buf + len1, ptr2, len2);
  PieString result = {buf, len1 + len2};
  return result;
}

int pie_string_eq(const char *ptr1, size_t len1, const char *ptr2,
                  size_t len2) {
  if (len1 != len2)
    return 0;
  if (len1 == 0)
    return 1;
  return memcmp(ptr1, ptr2, len1) == 0;
}

typedef struct PieList {
  size_t *data;
  size_t len;
  size_t cap;
} PieList;

static void pie_list_grow(PieList *list) {
  if (list->len >= list->cap) {
    size_t new_cap = list->cap ? list->cap * 2 : 4;
    size_t *new_data = (size_t *)realloc(list->data, new_cap * sizeof(size_t));
    if (new_data) {
      list->data = new_data;
      list->cap = new_cap;
    }
  }
}

void pie_list_push(void *list_ptr, size_t value) {
  PieList *list = (PieList *)list_ptr;
  pie_list_grow(list);
  if (list->data && list->len < list->cap) {
    list->data[list->len++] = value;
  }
}

size_t pie_list_get(void *list_ptr, size_t index) {
  PieList *list = (PieList *)list_ptr;
  if (list->data && index < list->len) {
    return list->data[index];
  }
  return 0;
}

void pie_list_set(void *list_ptr, size_t index, size_t value) {
  PieList *list = (PieList *)list_ptr;
  if (list->data && index < list->len) {
    list->data[index] = value;
  }
}

size_t pie_list_len(void *list_ptr) {
  PieList *list = (PieList *)list_ptr;
  return list->len;
}

size_t pie_list_pop(void *list_ptr) {
  PieList *list = (PieList *)list_ptr;
  if (list->data && list->len > 0) {
    return list->data[--list->len];
  }
  return 0;
}

void pie_list_insert(void *list_ptr, size_t index, size_t value) {
  PieList *list = (PieList *)list_ptr;
  if (index > list->len)
    return;
  pie_list_grow(list);
  for (size_t i = list->len; i > index; i--) {
    list->data[i] = list->data[i - 1];
  }
  list->data[index] = value;
  list->len++;
}

void pie_list_remove(void *list_ptr, size_t index) {
  PieList *list = (PieList *)list_ptr;
  if (list->data && index < list->len) {
    for (size_t i = index; i < list->len - 1; i++) {
      list->data[i] = list->data[i + 1];
    }
    list->len--;
  }
}

void pie_list_reverse(void *list_ptr) {
  PieList *list = (PieList *)list_ptr;
  if (!list->data)
    return;
  for (size_t i = 0, j = list->len - 1; i < j; i++, j--) {
    size_t tmp = list->data[i];
    list->data[i] = list->data[j];
    list->data[j] = tmp;
  }
}

static int cmp_size_t(const void *a, const void *b) {
  size_t va = *(const size_t *)a;
  size_t vb = *(const size_t *)b;
  return (va > vb) - (va < vb);
}

void pie_list_sort(void *list_ptr) {
  PieList *list = (PieList *)list_ptr;
  if (list->data && list->len > 1) {
    qsort(list->data, list->len, sizeof(size_t), cmp_size_t);
  }
}

double pie_int_to_float(long long n) { return (double)n; }

long long pie_float_to_int(double f) { return (long long)f; }

PieString pie_int_to_string(long long n) {
  char buf[32];
  int len = snprintf(buf, sizeof(buf), "%lld", n);
  char *copy = (char *)malloc(len + 1);
  if (!copy) {
    PieString empty = {NULL, 0};
    return empty;
  }
  memcpy(copy, buf, len + 1);
  PieString result = {copy, len};
  return result;
}

PieString pie_float_to_string(double f) {
  char buf[64];
  int len = snprintf(buf, sizeof(buf), "%.17g", f);
  char *copy = (char *)malloc(len + 1);
  if (!copy) {
    PieString empty = {NULL, 0};
    return empty;
  }
  memcpy(copy, buf, len + 1);
  PieString result = {copy, len};
  return result;
}

long long pie_string_to_int(const char *ptr, size_t len) {
  char buf[32];
  size_t copy_len = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
  memcpy(buf, ptr, copy_len);
  buf[copy_len] = '\0';
  return atoll(buf);
}

double pie_string_to_float(const char *ptr, size_t len) {
  char buf[64];
  size_t copy_len = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
  memcpy(buf, ptr, copy_len);
  buf[copy_len] = '\0';
  return atof(buf);
}

typedef struct PieMapEntry {
  char *key;
  size_t value;
  int occupied;
} PieMapEntry;

typedef struct PieMap {
  PieMapEntry *entries;
  size_t cap;
  size_t len;
} PieMap;

size_t pie_len(void *ptr, int type_tag) {
  if (type_tag == 0) {
    PieList *list = (PieList *)ptr;
    return list->len;
  } else if (type_tag == 1) {
    PieString *str = (PieString *)ptr;
    return str->len;
  } else if (type_tag == 2) {
    PieMap *map = (PieMap *)ptr;
    return map->len;
  }
  return 0;
}

static size_t pie_map_hash(const char *key, size_t len) {
  size_t h = 5381;
  for (size_t i = 0; i < len; i++) {
    h = ((h << 5) + h) + (unsigned char)key[i];
  }
  return h;
}

static void pie_map_grow(PieMap *map) {
  size_t new_cap = map->cap ? map->cap * 2 : 8;
  PieMapEntry *new_entries =
      (PieMapEntry *)calloc(new_cap, sizeof(PieMapEntry));
  if (!new_entries)
    return;
  for (size_t i = 0; i < map->cap; i++) {
    if (map->entries[i].occupied) {
      size_t h =
          pie_map_hash(map->entries[i].key, strlen(map->entries[i].key)) %
          new_cap;
      while (new_entries[h].occupied) {
        h = (h + 1) % new_cap;
      }
      new_entries[h] = map->entries[i];
    }
  }
  free(map->entries);
  map->entries = new_entries;
  map->cap = new_cap;
}

void *pie_map_create(void) {
  PieMap *map = (PieMap *)calloc(1, sizeof(PieMap));
  return map;
}

void pie_map_put(void *map_ptr, const char *key, size_t key_len, size_t value) {
  PieMap *map = (PieMap *)map_ptr;
  if (!map)
    return;
  if (map->len >= map->cap / 2) {
    pie_map_grow(map);
  }
  if (!map->entries) {
    map->cap = 8;
    map->entries = (PieMapEntry *)calloc(map->cap, sizeof(PieMapEntry));
    if (!map->entries)
      return;
  }
  size_t h = pie_map_hash(key, key_len) % map->cap;
  while (map->entries[h].occupied) {
    if (strlen(map->entries[h].key) == key_len &&
        memcmp(map->entries[h].key, key, key_len) == 0) {
      map->entries[h].value = value;
      return;
    }
    h = (h + 1) % map->cap;
  }
  char *key_copy = (char *)malloc(key_len + 1);
  if (!key_copy)
    return;
  memcpy(key_copy, key, key_len);
  key_copy[key_len] = '\0';
  map->entries[h].key = key_copy;
  map->entries[h].value = value;
  map->entries[h].occupied = 1;
  map->len++;
}

size_t pie_map_get(void *map_ptr, const char *key, size_t key_len) {
  PieMap *map = (PieMap *)map_ptr;
  if (!map || !map->entries)
    return 0;
  size_t h = pie_map_hash(key, key_len) % map->cap;
  while (map->entries[h].occupied) {
    if (strlen(map->entries[h].key) == key_len &&
        memcmp(map->entries[h].key, key, key_len) == 0) {
      return map->entries[h].value;
    }
    h = (h + 1) % map->cap;
  }
  return 0;
}

size_t pie_map_len(void *map_ptr) {
  PieMap *map = (PieMap *)map_ptr;
  if (!map)
    return 0;
  return map->len;
}

int pie_string_contains(const char *ptr, size_t len, const char *needle_ptr,
                        size_t needle_len) {
  if (needle_len > len)
    return 0;
  if (needle_len == 0)
    return 1;
  for (size_t i = 0; i <= len - needle_len; i++) {
    if (memcmp(ptr + i, needle_ptr, needle_len) == 0) {
      return 1;
    }
  }
  return 0;
}

PieString pie_string_upper(const char *ptr, size_t len) {
  char *buf = (char *)malloc(len);
  if (!buf) {
    PieString empty = {NULL, 0};
    return empty;
  }
  for (size_t i = 0; i < len; i++) {
    char c = ptr[i];
    if (c >= 'a' && c <= 'z') {
      buf[i] = c - 32;
    } else {
      buf[i] = c;
    }
  }
  PieString result = {buf, len};
  return result;
}

PieString pie_string_lower(const char *ptr, size_t len) {
  char *buf = (char *)malloc(len);
  if (!buf) {
    PieString empty = {NULL, 0};
    return empty;
  }
  for (size_t i = 0; i < len; i++) {
    char c = ptr[i];
    if (c >= 'A' && c <= 'Z') {
      buf[i] = c + 32;
    } else {
      buf[i] = c;
    }
  }
  PieString result = {buf, len};
  return result;
}

PieString pie_string_trim(const char *ptr, size_t len) {
  size_t start = 0;
  while (start < len && (ptr[start] == ' ' || ptr[start] == '\t' ||
                         ptr[start] == '\n' || ptr[start] == '\r')) {
    start++;
  }
  size_t end = len;
  while (end > start && (ptr[end - 1] == ' ' || ptr[end - 1] == '\t' ||
                         ptr[end - 1] == '\n' || ptr[end - 1] == '\r')) {
    end--;
  }
  size_t new_len = end - start;
  if (new_len == 0) {
    PieString empty = {NULL, 0};
    return empty;
  }
  char *buf = (char *)malloc(new_len);
  if (!buf) {
    PieString empty = {NULL, 0};
    return empty;
  }
  memcpy(buf, ptr + start, new_len);
  PieString result = {buf, new_len};
  return result;
}

PieString pie_string_replace(const char *ptr, size_t len, const char *old_ptr,
                             size_t old_len, const char *new_ptr,
                             size_t new_len) {
  if (old_len == 0) {
    char *buf = (char *)malloc(len);
    if (!buf) {
      PieString empty = {NULL, 0};
      return empty;
    }
    memcpy(buf, ptr, len);
    PieString result = {buf, len};
    return result;
  }
  size_t count = 0;
  for (size_t i = 0; i <= len - old_len; i++) {
    if (memcmp(ptr + i, old_ptr, old_len) == 0) {
      count++;
      i += old_len - 1;
    }
  }
  if (count == 0) {
    char *buf = (char *)malloc(len);
    if (!buf) {
      PieString empty = {NULL, 0};
      return empty;
    }
    memcpy(buf, ptr, len);
    PieString result = {buf, len};
    return result;
  }
  size_t result_len = len - count * old_len + count * new_len;
  char *buf = (char *)malloc(result_len);
  if (!buf) {
    PieString empty = {NULL, 0};
    return empty;
  }
  size_t pos = 0;
  for (size_t i = 0; i <= len - old_len;) {
    if (memcmp(ptr + i, old_ptr, old_len) == 0) {
      memcpy(buf + pos, new_ptr, new_len);
      pos += new_len;
      i += old_len;
    } else {
      buf[pos++] = ptr[i++];
    }
  }
  memcpy(buf + pos,
         ptr + (len - (len % old_len == 0 ? old_len : len % old_len)),
         len - (len - result_len));
  PieString result = {buf, result_len};
  return result;
}

PieString pie_string_repeat(const char *ptr, size_t len, long long count) {
  if (count <= 0 || len == 0) {
    PieString empty = {NULL, 0};
    return empty;
  }
  size_t total = (size_t)count * len;
  char *buf = (char *)malloc(total);
  if (!buf) {
    PieString empty = {NULL, 0};
    return empty;
  }
  for (size_t i = 0; i < (size_t)count; i++) {
    memcpy(buf + i * len, ptr, len);
  }
  PieString result = {buf, total};
  return result;
}

long long pie_int_power(long long base, long long exp) {
  if (exp < 0) {
    return 0;
  }
  long long result = 1;
  while (exp > 0) {
    if (exp & 1) {
      result *= base;
    }
    base *= base;
    exp >>= 1;
  }
  return result;
}

double pie_float_power(double base, double exp) {
  double result = 1.0;
  long long e = (long long)exp;
  if (e < 0) {
    base = 1.0 / base;
    e = -e;
  }
  while (e > 0) {
    if (e & 1) {
      result *= base;
    }
    base *= base;
    e >>= 1;
  }
  return result;
}

void pie_assert_fail(void) {
  fprintf(stderr, "Assertion failed\n");
  exit(1);
}

void pie_assert_eq_fail(long long right_val) {
  fprintf(stderr, "Assertion failed: values not equal (right was %lld)\n",
          right_val);
  exit(1);
}

void pie_string_index_oob(void) {
  fprintf(stderr, "string index out of bounds\n");
  exit(1);
}

PieString pie_format_int(long long value) { return pie_int_to_string(value); }

PieString pie_format_float(double value) { return pie_float_to_string(value); }

PieString pie_format_bool(int value) {
  if (value) {
    PieString result = {"true", 4};
    return result;
  } else {
    PieString result = {"false", 5};
    return result;
  }
}