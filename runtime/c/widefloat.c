#include "widefloat.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

PieWideFloat *pie_float_wide_new(double value) {
  PieWideFloat *w = calloc(1, sizeof(PieWideFloat));
  w->value = (long double)value;
  return w;
}

void pie_float_wide_free(PieWideFloat *a) { free(a); }

PieWideFloat *pie_float_wide_add(const PieWideFloat *a, const PieWideFloat *b) {
  PieWideFloat *r = calloc(1, sizeof(PieWideFloat));
  r->value = a->value + b->value;
  return r;
}

PieWideFloat *pie_float_wide_sub(const PieWideFloat *a, const PieWideFloat *b) {
  PieWideFloat *r = calloc(1, sizeof(PieWideFloat));
  r->value = a->value - b->value;
  return r;
}

PieWideFloat *pie_float_wide_mul(const PieWideFloat *a, const PieWideFloat *b) {
  PieWideFloat *r = calloc(1, sizeof(PieWideFloat));
  r->value = a->value * b->value;
  return r;
}

PieWideFloat *pie_float_wide_div(const PieWideFloat *a, const PieWideFloat *b) {
  PieWideFloat *r = calloc(1, sizeof(PieWideFloat));
  r->value = a->value / b->value;
  return r;
}

PieWideFloat *pie_float_wide_neg(const PieWideFloat *a) {
  PieWideFloat *r = calloc(1, sizeof(PieWideFloat));
  r->value = -a->value;
  return r;
}

int pie_float_wide_cmp(const PieWideFloat *a, const PieWideFloat *b) {
  if (a->value < b->value)
    return -1;
  if (a->value > b->value)
    return 1;
  return 0;
}

double pie_float_wide_to_f64(const PieWideFloat *a) { return (double)a->value; }

void pie_float_wide_print(const PieWideFloat *a) {
  char buf[128];
  snprintf(buf, sizeof(buf), "%.15Lg", a->value);
  fputs(buf, stdout);
  fflush(stdout);
}
