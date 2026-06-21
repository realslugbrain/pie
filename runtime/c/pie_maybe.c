#define _GNU_SOURCE

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#if defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>
#endif

static uint64_t pie_maybe_state = 0;

static int pie_maybe_entropy(uint64_t *out) {
#if defined(__linux__) && defined(SYS_getrandom)
  return syscall(SYS_getrandom, out, sizeof(*out), 0) == (ssize_t)sizeof(*out);
#else
  (void)out;
  return 0;
#endif
}

static uint64_t pie_maybe_seed(void) {
  uint64_t seed = 0;
  if (pie_maybe_entropy(&seed) && seed != 0) {
    return seed;
  }

  seed = (uint64_t)time(NULL);
  seed ^= (uint64_t)clock() << 17;
  seed ^= (uint64_t)(uintptr_t)&seed;
  seed ^= 0x9e3779b97f4a7c15ULL;
  return seed ? seed : 0xa0761d6478bd642fULL;
}

static uint64_t pie_maybe_next(void) {
  if (pie_maybe_state == 0) {
    pie_maybe_state = pie_maybe_seed();
  }

  uint64_t x = pie_maybe_state;
  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  pie_maybe_state = x;
  return x * 0x2545f4914f6cdd1dULL;
}

bool pie_maybe(void) {
  uint64_t value = 0;
  if (!pie_maybe_entropy(&value)) {
    value = pie_maybe_next();
  }
  return (value & 1u) != 0;
}
