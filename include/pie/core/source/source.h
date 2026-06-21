#ifndef PIE_CORE_SOURCE_SOURCE_H
#define PIE_CORE_SOURCE_SOURCE_H

#include <stddef.h>

#include "pie/core/diag/diag.h"

typedef struct PieSource {
  char *path;
  char *text;
  size_t len;
} PieSource;

int pie_source_read_file(const char *path, PieSource *out,
                         PieDiagnosticBag *diag);
void pie_source_free(PieSource *source);

#endif
