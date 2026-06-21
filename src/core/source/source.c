#include "pie/core/source/source.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *pie_source_strdup(const char *s) {
  size_t len = strlen(s);
  char *copy = (char *)malloc(len + 1);
  if (!copy) {
    return NULL;
  }
  memcpy(copy, s, len + 1);
  return copy;
}

int pie_source_read_file(const char *path, PieSource *out,
                         PieDiagnosticBag *diag) {
  out->path = NULL;
  out->text = NULL;
  out->len = 0;

  FILE *f = fopen(path, "rb");
  if (!f) {
    pie_diag_errorf(diag, "could not open source file '%s'", path);
    return 0;
  }

  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    pie_diag_errorf(diag, "could not seek source file '%s'", path);
    return 0;
  }

  long size = ftell(f);
  if (size < 0) {
    fclose(f);
    pie_diag_errorf(diag, "could not measure source file '%s'", path);
    return 0;
  }

  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    pie_diag_errorf(diag, "could not rewind source file '%s'", path);
    return 0;
  }

  char *text = (char *)malloc((size_t)size + 1);
  if (!text) {
    fclose(f);
    pie_diag_error(diag, "out of memory while reading source");
    return 0;
  }

  size_t read = fread(text, 1, (size_t)size, f);
  fclose(f);

  if (read != (size_t)size) {
    free(text);
    pie_diag_errorf(diag, "could not read complete source file '%s'", path);
    return 0;
  }

  text[read] = '\0';
  out->path = pie_source_strdup(path);
  out->text = text;
  out->len = read;
  if (!out->path) {
    pie_source_free(out);
    pie_diag_error(diag, "out of memory while storing source path");
    return 0;
  }
  return 1;
}

void pie_source_free(PieSource *source) {
  free(source->path);
  free(source->text);
  source->path = NULL;
  source->text = NULL;
  source->len = 0;
}
