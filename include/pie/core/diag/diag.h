#ifndef PIE_CORE_DIAG_DIAG_H
#define PIE_CORE_DIAG_DIAG_H

#include <stddef.h>

typedef struct PieDiagnosticBag {
  char **items;
  size_t count;
  size_t capacity;
  int has_error;
} PieDiagnosticBag;

void pie_diag_init(PieDiagnosticBag *bag);
void pie_diag_free(PieDiagnosticBag *bag);
void pie_diag_error(PieDiagnosticBag *bag, const char *message);
void pie_diag_errorf(PieDiagnosticBag *bag, const char *fmt, ...);
void pie_diag_print(const PieDiagnosticBag *bag);

#endif
