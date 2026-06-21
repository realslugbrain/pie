#ifndef PIE_WIDEFLOAT_H
#define PIE_WIDEFLOAT_H

#include <stdint.h>

typedef struct PieWideFloat {
  long double value;
} PieWideFloat;

PieWideFloat *pie_float_wide_new(double value);
void pie_float_wide_free(PieWideFloat *a);
PieWideFloat *pie_float_wide_add(const PieWideFloat *a, const PieWideFloat *b);
PieWideFloat *pie_float_wide_sub(const PieWideFloat *a, const PieWideFloat *b);
PieWideFloat *pie_float_wide_mul(const PieWideFloat *a, const PieWideFloat *b);
PieWideFloat *pie_float_wide_div(const PieWideFloat *a, const PieWideFloat *b);
PieWideFloat *pie_float_wide_neg(const PieWideFloat *a);
int pie_float_wide_cmp(const PieWideFloat *a, const PieWideFloat *b);
double pie_float_wide_to_f64(const PieWideFloat *a);
void pie_float_wide_print(const PieWideFloat *a);

#endif
