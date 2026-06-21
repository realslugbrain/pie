#define _POSIX_C_SOURCE 200809L

#include "pie/core/modules/resolver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *const PIE_STD_MODULES[] = {
    "io",   "fs",     "path",     "process", "net",   "http",
    "json", "crypto", "time",     "regex",   "math",  "sync",
    "task", "wasm",   "wasm/dom", "test",    "debug",
};

static char *resolver_strdup(const char *s) {
  size_t len = strlen(s);
  char *copy = (char *)malloc(len + 1);
  if (!copy) {
    return NULL;
  }
  memcpy(copy, s, len + 1);
  return copy;
}

static int path_exists(const char *path) { return access(path, F_OK) == 0; }

static char *path_join(const char *left, const char *right) {
  size_t a = strlen(left);
  size_t b = strlen(right);
  int needs_slash = a > 0 && left[a - 1] != '/';
  char *out = (char *)malloc(a + (needs_slash ? 1 : 0) + b + 1);
  if (!out) {
    return NULL;
  }
  memcpy(out, left, a);
  size_t pos = a;
  if (needs_slash) {
    out[pos++] = '/';
  }
  memcpy(out + pos, right, b + 1);
  return out;
}

static char *path_with_suffix(const char *path, const char *suffix) {
  size_t a = strlen(path);
  size_t b = strlen(suffix);
  char *out = (char *)malloc(a + b + 1);
  if (!out) {
    return NULL;
  }
  memcpy(out, path, a);
  memcpy(out + a, suffix, b + 1);
  return out;
}

static char *parent_dir(const char *path) {
  char *copy = resolver_strdup(path);
  if (!copy) {
    return NULL;
  }
  size_t len = strlen(copy);
  while (len > 1 && copy[len - 1] == '/') {
    copy[--len] = '\0';
  }
  char *slash = strrchr(copy, '/');
  if (!slash) {
    free(copy);
    return resolver_strdup(".");
  }
  if (slash == copy) {
    slash[1] = '\0';
  } else {
    *slash = '\0';
  }
  return copy;
}

static char *trim_left(char *s) {
  while (*s == ' ' || *s == '\t') {
    s++;
  }
  return s;
}

static void trim_right_in_place(char *s) {
  size_t len = strlen(s);
  while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' ||
                     s[len - 1] == ' ' || s[len - 1] == '\t')) {
    s[--len] = '\0';
  }
}

static char *parse_package_name(const char *manifest_path) {
  FILE *file = fopen(manifest_path, "rb");
  if (!file) {
    return NULL;
  }

  char line[1024];
  int in_package = 0;
  char *name = NULL;
  while (fgets(line, sizeof(line), file)) {
    trim_right_in_place(line);
    char *s = trim_left(line);
    if (*s == '\0' || *s == '#') {
      continue;
    }
    if (*s == '[') {
      in_package = strcmp(s, "[package]") == 0;
      continue;
    }
    if (!in_package || strncmp(s, "name", 4) != 0) {
      continue;
    }
    s += 4;
    s = trim_left(s);
    if (*s != '=') {
      continue;
    }
    s++;
    s = trim_left(s);
    if (*s != '"') {
      continue;
    }
    s++;
    char *end = strchr(s, '"');
    if (!end) {
      continue;
    }
    *end = '\0';
    name = resolver_strdup(s);
    break;
  }
  fclose(file);
  return name;
}

static char *find_package_root_for_source(const char *source_path) {
  char *dir = parent_dir(source_path);
  if (!dir) {
    return NULL;
  }

  for (;;) {
    char *manifest = path_join(dir, "pie.toml");
    if (!manifest) {
      free(dir);
      return NULL;
    }
    if (path_exists(manifest)) {
      free(manifest);
      return dir;
    }
    free(manifest);

    char *parent = parent_dir(dir);
    if (!parent) {
      free(dir);
      return NULL;
    }
    if (strcmp(parent, dir) == 0) {
      free(parent);
      free(dir);
      return NULL;
    }
    free(dir);
    dir = parent;
  }
}

static int is_known_std_module(const char *path) {
  for (size_t i = 0; i < sizeof(PIE_STD_MODULES) / sizeof(PIE_STD_MODULES[0]);
       i++) {
    if (strcmp(path, PIE_STD_MODULES[i]) == 0) {
      return 1;
    }
  }
  return 0;
}

static int has_empty_or_parent_segment(const char *path) {
  const char *p = path;
  while (*p) {
    const char *slash = strchr(p, '/');
    size_t len = slash ? (size_t)(slash - p) : strlen(p);
    if (len == 0 || (len == 2 && p[0] == '.' && p[1] == '.')) {
      return 1;
    }
    if (!slash) {
      break;
    }
    p = slash + 1;
  }
  return 0;
}

static int validate_std_require(const char *path, PieDiagnosticBag *diag) {
  if (is_known_std_module(path)) {
    return 1;
  }
  pie_diag_errorf(diag, "unknown standard module '%s'", path);
  return 0;
}

static int validate_package_require(const char *path, const char *source_path,
                                    PieDiagnosticBag *diag) {
  if (path[0] == '/' || has_empty_or_parent_segment(path)) {
    pie_diag_errorf(diag, "invalid module path '%s'", path);
    return 0;
  }

  const char *slash = strchr(path, '/');
  if (!slash || slash == path || slash[1] == '\0') {
    pie_diag_errorf(
        diag,
        "package module require must look like \"package/module\", got '%s'",
        path);
    return 0;
  }

  char *root = find_package_root_for_source(source_path);
  if (!root) {
    pie_diag_errorf(diag,
                    "package module require '%s' needs a pie.toml package root",
                    path);
    return 0;
  }

  char *manifest = path_join(root, "pie.toml");
  char *package_name = manifest ? parse_package_name(manifest) : NULL;
  free(manifest);
  if (!package_name) {
    pie_diag_errorf(
        diag, "could not read package name while resolving module '%s'", path);
    free(root);
    return 0;
  }

  size_t prefix_len = (size_t)(slash - path);
  if (strlen(package_name) != prefix_len ||
      memcmp(path, package_name, prefix_len) != 0) {
    pie_diag_errorf(diag,
                    "external package module '%s' is not resolved yet; only "
                    "current-package modules are supported",
                    path);
    free(package_name);
    free(root);
    return 0;
  }

  char *src_dir = path_join(root, "src");
  char *module_no_ext = src_dir ? path_join(src_dir, slash + 1) : NULL;
  char *module_path =
      module_no_ext ? path_with_suffix(module_no_ext, ".pie") : NULL;
  if (!src_dir || !module_no_ext || !module_path) {
    pie_diag_error(diag, "out of memory while resolving module require");
    free(src_dir);
    free(module_no_ext);
    free(module_path);
    free(package_name);
    free(root);
    return 0;
  }

  int ok = path_exists(module_path);
  if (!ok) {
    pie_diag_errorf(diag, "required module '%s' not found at '%s'", path,
                    module_path);
  }

  free(src_dir);
  free(module_no_ext);
  free(module_path);
  free(package_name);
  free(root);
  return ok;
}

int pie_resolve_requires(const PieProgram *program, const char *source_path,
                         PieDiagnosticBag *diag) {
  int ok = 1;
  for (size_t i = 0; i < program->require_count; i++) {
    const PieRequire *require = &program->requires[i];
    if (require->kind == PIE_REQUIRE_STD) {
      ok = validate_std_require(require->path, diag) && ok;
    } else {
      ok = validate_package_require(require->path, source_path, diag) && ok;
    }
  }
  return ok && !diag->has_error;
}
