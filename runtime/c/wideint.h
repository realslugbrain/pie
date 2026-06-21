#ifndef PIE_WIDEINT_H
#define PIE_WIDEINT_H

#include <stddef.h>
#include <stdint.h>

typedef struct PieWideInt {
  int sign;
  size_t len;
  size_t cap;
  uint64_t *limbs;
} PieWideInt;

PieWideInt *pie_int_wide_new(int64_t value);
void pie_int_wide_free(PieWideInt *a);
PieWideInt *pie_int_wide_add(const PieWideInt *a, const PieWideInt *b);
PieWideInt *pie_int_wide_sub(const PieWideInt *a, const PieWideInt *b);
PieWideInt *pie_int_wide_mul(const PieWideInt *a, const PieWideInt *b);
PieWideInt *pie_int_wide_div(const PieWideInt *a, const PieWideInt *b);
PieWideInt *pie_int_wide_mod(const PieWideInt *a, const PieWideInt *b);
PieWideInt *pie_int_wide_neg(const PieWideInt *a);
int pie_int_wide_cmp(const PieWideInt *a, const PieWideInt *b);
int64_t pie_int_wide_to_i64(const PieWideInt *a);
void pie_int_wide_print(const PieWideInt *a);

#endif
