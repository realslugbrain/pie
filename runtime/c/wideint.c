#include "wideint.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void grow(PieWideInt *w, size_t need) {
  if (need <= w->cap)
    return;
  size_t nc = w->cap ? w->cap * 2 : 4;
  while (nc < need)
    nc *= 2;
  w->limbs = realloc(w->limbs, nc * sizeof(uint64_t));
  w->cap = nc;
}

static void trim(PieWideInt *w) {
  while (w->len > 0 && w->limbs[w->len - 1] == 0)
    w->len--;
  if (w->len == 0)
    w->sign = 1;
}

static int cmp_limbs(const uint64_t *a, size_t al, const uint64_t *b,
                     size_t bl) {
  if (al != bl)
    return al < bl ? -1 : 1;
  for (size_t i = al; i > 0; i--)
    if (a[i - 1] != b[i - 1])
      return a[i - 1] < b[i - 1] ? -1 : 1;
  return 0;
}

static PieWideInt *from_limbs(const uint64_t *src, size_t n, int sign) {
  PieWideInt *w = calloc(1, sizeof(PieWideInt));
  w->sign = sign;
  w->len = n;
  grow(w, n);
  memcpy(w->limbs, src, n * sizeof(uint64_t));
  trim(w);
  return w;
}

static PieWideInt *make_zero(void) {
  PieWideInt *w = calloc(1, sizeof(PieWideInt));
  w->sign = 1;
  return w;
}

static PieWideInt *clone(const PieWideInt *a) {
  return from_limbs(a->limbs, a->len, a->sign);
}

static PieWideInt *neg_clone(const PieWideInt *a) {
  PieWideInt *r = clone(a);
  if (r->len > 0)
    r->sign = -r->sign;
  return r;
}

static void add_unsigned(uint64_t *out, const uint64_t *a, size_t al,
                         const uint64_t *b, size_t bl) {
  uint64_t carry = 0;
  size_t ml = al > bl ? al : bl;
  for (size_t i = 0; i < ml; i++) {
    uint64_t av = i < al ? a[i] : 0;
    uint64_t bv = i < bl ? b[i] : 0;
    uint64_t s = av + bv + carry;
    out[i] = s;
    carry = (s < av) || (s == av && bv != 0) ? 1 : 0;
  }
  if (carry)
    out[ml] = carry;
}

static void sub_unsigned(uint64_t *out, const uint64_t *a, size_t al,
                         const uint64_t *b, size_t bl) {
  uint64_t borrow = 0;
  for (size_t i = 0; i < al; i++) {
    uint64_t bv = i < bl ? b[i] : 0;
    uint64_t diff = a[i] - bv - borrow;
    borrow = (a[i] < bv + borrow) || (bv + borrow > 0 && diff > a[i]) ? 1 : 0;
    out[i] = diff;
  }
}

static PieWideInt *iadd(const PieWideInt *a, const PieWideInt *b) {
  size_t ml = a->len > b->len ? a->len : b->len;
  uint64_t *r = calloc(ml + 1, sizeof(uint64_t));
  add_unsigned(r, a->limbs, a->len, b->limbs, b->len);
  size_t rl = ml + (r[ml] ? 1 : 0);
  PieWideInt *res = from_limbs(r, rl, 1);
  free(r);
  return res;
}

static PieWideInt *isub(const PieWideInt *a, const PieWideInt *b) {
  int c = cmp_limbs(a->limbs, a->len, b->limbs, b->len);
  if (c == 0)
    return make_zero();
  const PieWideInt *big, *small;
  int sign;
  if (c > 0) {
    big = a;
    small = b;
    sign = 1;
  } else {
    big = b;
    small = a;
    sign = -1;
  }
  uint64_t *r = calloc(big->len, sizeof(uint64_t));
  sub_unsigned(r, big->limbs, big->len, small->limbs, small->len);
  PieWideInt *res = from_limbs(r, big->len, sign);
  free(r);
  return res;
}

PieWideInt *pie_int_wide_new(int64_t value) {
  if (value == 0)
    return make_zero();
  int sign = value > 0 ? 1 : -1;
  uint64_t v = value > 0 ? (uint64_t)value : (uint64_t)(-(value + 1)) + 1;
  return from_limbs(&v, 1, sign);
}

void pie_int_wide_free(PieWideInt *a) {
  if (a) {
    free(a->limbs);
    free(a);
  }
}

PieWideInt *pie_int_wide_add(const PieWideInt *a, const PieWideInt *b) {
  if (a->sign == b->sign) {
    PieWideInt *r = iadd(a, b);
    r->sign = a->sign;
    return r;
  }
  return isub(a, b);
}

PieWideInt *pie_int_wide_sub(const PieWideInt *a, const PieWideInt *b) {
  PieWideInt *nb = neg_clone(b);
  PieWideInt *r = pie_int_wide_add(a, nb);
  pie_int_wide_free(nb);
  return r;
}

PieWideInt *pie_int_wide_mul(const PieWideInt *a, const PieWideInt *b) {
  if (a->len == 0 || b->len == 0)
    return make_zero();
  size_t rl = a->len + b->len;
  uint64_t *r = calloc(rl, sizeof(uint64_t));
  for (size_t i = 0; i < a->len; i++) {
    uint64_t carry = 0;
    for (size_t j = 0; j < b->len; j++) {
      __uint128_t p = (__uint128_t)a->limbs[i] * b->limbs[j] + r[i + j] + carry;
      r[i + j] = (uint64_t)p;
      carry = (uint64_t)(p >> 64);
    }
    if (carry)
      r[i + b->len] += carry;
  }
  while (rl > 0 && r[rl - 1] == 0)
    rl--;
  int sign = (a->sign == b->sign) ? 1 : -1;
  PieWideInt *res = from_limbs(r, rl, sign);
  free(r);
  return res;
}

static void divmod_impl(const PieWideInt *num, const PieWideInt *den,
                        PieWideInt **quot, PieWideInt **rem) {
  if (den->len == 0) {
    fprintf(stderr, "division by zero\n");
    exit(1);
  }
  int c = cmp_limbs(num->limbs, num->len, den->limbs, den->len);
  if (c < 0) {
    *quot = make_zero();
    *rem = clone(num);
    return;
  }
  if (c == 0) {
    *quot = from_limbs(&(uint64_t){1}, 1, 1);
    *rem = make_zero();
    return;
  }
  size_t dl = num->len, sl = den->len, ql = dl - sl + 1;
  uint64_t *q = calloc(ql, sizeof(uint64_t));
  uint64_t *work = calloc(dl + 1, sizeof(uint64_t));
  memcpy(work, num->limbs, dl * sizeof(uint64_t));
  for (size_t i = ql; i > 0; i--) {
    size_t pos = i - 1;
    if (pos + sl > dl)
      continue;
    uint64_t hi = work[pos + sl];
    uint64_t lo = work[pos + sl - 1];
    uint64_t d = den->limbs[sl - 1];
    uint64_t trial;
    if (hi == 0) {
      trial = lo / d;
    } else if (hi >= d) {
      trial = UINT64_MAX;
    } else {
      trial = UINT64_MAX;
    }
    if (trial == 0 && pos + sl < dl)
      trial = 1;
    else if (trial == 0)
      continue;
    for (;;) {
      uint64_t carry = 0;
      int ok = 1;
      for (size_t j = 0; j < sl; j++) {
        uint64_t p_lo = den->limbs[j] * trial;
        uint64_t p_hi =
            (uint64_t)(((unsigned __int128)den->limbs[j] * trial) >> 64);
        uint64_t sum = p_lo + carry;
        uint64_t sum_carry = (sum < p_lo) ? 1 : 0;
        carry = p_hi + sum_carry;
        if (work[pos + j] < sum) {
          if (carry > 0)
            carry--;
          else {
            ok = 0;
            break;
          }
        }
        work[pos + j] -= sum;
      }
      if (ok && work[pos + sl] >= carry) {
        work[pos + sl] -= carry;
        q[pos] = trial;
        break;
      }
      trial--;
    }
  }
  while (ql > 0 && q[ql - 1] == 0)
    ql--;
  *quot = from_limbs(q, ql, 1);
  size_t rlen = sl;
  while (rlen > 0 && work[rlen - 1] == 0)
    rlen--;
  *rem = from_limbs(work, rlen, 1);
  free(q);
  free(work);
}

PieWideInt *pie_int_wide_div(const PieWideInt *a, const PieWideInt *b) {
  PieWideInt na = *a;
  na.sign = 1;
  PieWideInt nb = *b;
  nb.sign = 1;
  PieWideInt *q, *r;
  divmod_impl(&na, &nb, &q, &r);
  pie_int_wide_free(r);
  q->sign = (a->sign == b->sign) ? 1 : -1;
  if (q->len == 0)
    q->sign = 1;
  return q;
}

PieWideInt *pie_int_wide_mod(const PieWideInt *a, const PieWideInt *b) {
  PieWideInt na = *a;
  na.sign = 1;
  PieWideInt nb = *b;
  nb.sign = 1;
  PieWideInt *q, *r;
  divmod_impl(&na, &nb, &q, &r);
  pie_int_wide_free(q);
  r->sign = a->sign;
  if (r->len == 0)
    r->sign = 1;
  return r;
}

PieWideInt *pie_int_wide_neg(const PieWideInt *a) { return neg_clone(a); }

int pie_int_wide_cmp(const PieWideInt *a, const PieWideInt *b) {
  if (a->sign != b->sign)
    return a->sign < b->sign ? -1 : 1;
  int c = cmp_limbs(a->limbs, a->len, b->limbs, b->len);
  return a->sign > 0 ? c : -c;
}

int64_t pie_int_wide_to_i64(const PieWideInt *a) {
  if (a->len == 0)
    return 0;
  int64_t v = (int64_t)a->limbs[0];
  return a->sign > 0 ? v : -v;
}

void pie_int_wide_print(const PieWideInt *a) {
  if (a->len == 0) {
    putchar('0');
    return;
  }
  if (a->sign < 0)
    putchar('-');
  size_t blen = a->len * 21 + 2;
  char *buf = malloc(blen);
  uint64_t *work = malloc(a->len * sizeof(uint64_t));
  memcpy(work, a->limbs, a->len * sizeof(uint64_t));
  size_t pos = blen - 1;
  buf[pos] = '\0';
  while (1) {
    uint64_t rem = 0;
    for (size_t i = a->len; i > 0; i--) {
      __uint128_t cur = ((__uint128_t)rem << 64) | work[i - 1];
      work[i - 1] = (uint64_t)(cur / 10);
      rem = (uint64_t)(cur % 10);
    }
    pos--;
    buf[pos] = '0' + (char)rem;
    int done = 1;
    for (size_t i = 0; i < a->len; i++)
      if (work[i] != 0) {
        done = 0;
        break;
      }
    if (done)
      break;
  }
  fputs(buf + pos, stdout);
  fflush(stdout);
  free(work);
  free(buf);
}
