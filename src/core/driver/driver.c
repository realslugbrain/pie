#define _POSIX_C_SOURCE 200809L

#include "pie/core/driver/driver.h"

#include "pie/backend/asm/asm_emit.h"
#include "pie/core/ast/ast.h"
#include "pie/core/borrow/borrowcheck.h"
#include "pie/core/diag/diag.h"
#include "pie/core/ir/ir.h"
#include "pie/core/lower/lower.h"
#include "pie/core/modules/loader.h"
#include "pie/core/modules/resolver.h"
#include "pie/core/parser/parser.h"
#include "pie/core/sema/sema.h"
#include "pie/core/source/source.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PIE_SOURCE_ROOT
#define PIE_SOURCE_ROOT "."
#endif

static char *driver_strdup(const char *s) {
  size_t len = strlen(s);
  char *copy = (char *)malloc(len + 1);
  if (!copy) {
    return NULL;
  }
  memcpy(copy, s, len + 1);
  return copy;
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

static int path_exists(const char *path) { return access(path, F_OK) == 0; }

static int dir_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int ensure_dir(const char *path) {
  if (dir_exists(path)) {
    return 1;
  }
  if (mkdir(path, 0775) == 0) {
    return 1;
  }
  fprintf(stderr, "pie: error: could not create directory '%s': %s\n", path,
          strerror(errno));
  return 0;
}

static int make_dir(const char *path) {
  if (mkdir(path, 0775) == 0) {
    return 1;
  }
  fprintf(stderr, "pie: error: could not create directory '%s': %s\n", path,
          strerror(errno));
  return 0;
}

static int has_suffix(const char *text, const char *suffix) {
  size_t text_len = strlen(text);
  size_t suffix_len = strlen(suffix);
  return text_len >= suffix_len &&
         strcmp(text + text_len - suffix_len, suffix) == 0;
}

static char *parent_dir(const char *path) {
  char *copy = driver_strdup(path);
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
    return driver_strdup(".");
  }
  if (slash == copy) {
    slash[1] = '\0';
  } else {
    *slash = '\0';
  }
  return copy;
}

static const char *path_basename(const char *path) {
  const char *end = path + strlen(path);
  while (end > path && end[-1] == '/') {
    end--;
  }
  const char *start = end;
  while (start > path && start[-1] != '/') {
    start--;
  }
  return start;
}

static char *package_name_from_path(const char *path) {
  const char *base = path_basename(path);
  size_t len = strlen(base);
  while (len > 0 && base[len - 1] == '/') {
    len--;
  }
  if (len == 0) {
    return NULL;
  }
  char *name = (char *)malloc(len + 1);
  if (!name) {
    return NULL;
  }
  memcpy(name, base, len);
  name[len] = '\0';
  return name;
}

static int valid_package_name(const char *name) {
  if (!name || !name[0] || name[0] == '_' || !islower((unsigned char)name[0])) {
    return 0;
  }
  for (const char *p = name; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (!(islower(c) || isdigit(c) || c == '_')) {
      return 0;
    }
  }
  return 1;
}

static int write_text_file(const char *path, const char *text) {
  FILE *file = fopen(path, "wb");
  if (!file) {
    fprintf(stderr, "pie: error: could not create file '%s': %s\n", path,
            strerror(errno));
    return 0;
  }
  size_t len = strlen(text);
  int ok = fwrite(text, 1, len, file) == len;
  if (fclose(file) != 0) {
    ok = 0;
  }
  if (!ok) {
    fprintf(stderr, "pie: error: could not write file '%s'\n", path);
  }
  return ok;
}

static char *read_text_file(const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) {
    fprintf(stderr, "pie: error: could not open file '%s': %s\n", path,
            strerror(errno));
    return NULL;
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    fprintf(stderr, "pie: error: could not read file '%s'\n", path);
    return NULL;
  }
  long size = ftell(file);
  if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    fprintf(stderr, "pie: error: could not read file '%s'\n", path);
    return NULL;
  }
  char *text = (char *)malloc((size_t)size + 1);
  if (!text) {
    fclose(file);
    fprintf(stderr, "pie: error: out of memory while reading '%s'\n", path);
    return NULL;
  }
  size_t read_count = fread(text, 1, (size_t)size, file);
  fclose(file);
  if (read_count != (size_t)size) {
    free(text);
    fprintf(stderr, "pie: error: could not read file '%s'\n", path);
    return NULL;
  }
  text[read_count] = '\0';
  return text;
}

static int append_range(char **out, size_t *len, size_t *cap, const char *start,
                        size_t count) {
  if (*len + count + 1 > *cap) {
    size_t next_capacity = *cap ? *cap * 2 : 1024;
    while (next_capacity < *len + count + 1) {
      next_capacity *= 2;
    }
    char *next = (char *)realloc(*out, next_capacity);
    if (!next) {
      return 0;
    }
    *out = next;
    *cap = next_capacity;
  }
  memcpy(*out + *len, start, count);
  *len += count;
  (*out)[*len] = '\0';
  return 1;
}

static int append_text(char **out, size_t *len, size_t *cap, const char *text) {
  return append_range(out, len, cap, text, strlen(text));
}

static int line_is_section(const char *line, size_t len, const char *name) {
  while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' ||
                     line[len - 1] == ' ' || line[len - 1] == '\t')) {
    len--;
  }
  while (len > 0 && (*line == ' ' || *line == '\t')) {
    line++;
    len--;
  }
  size_t name_len = strlen(name);
  return len == name_len + 2 && line[0] == '[' &&
         memcmp(line + 1, name, name_len) == 0 && line[len - 1] == ']';
}

static int line_starts_section(const char *line, size_t len) {
  while (len > 0 && (*line == ' ' || *line == '\t')) {
    line++;
    len--;
  }
  return len > 0 && line[0] == '[';
}

static int line_is_dependency(const char *line, size_t len, const char *name) {
  while (len > 0 && (*line == ' ' || *line == '\t')) {
    line++;
    len--;
  }
  size_t name_len = strlen(name);
  if (len < name_len || memcmp(line, name, name_len) != 0) {
    return 0;
  }
  line += name_len;
  len -= name_len;
  while (len > 0 && (*line == ' ' || *line == '\t')) {
    line++;
    len--;
  }
  return len > 0 && *line == '=';
}

static int split_dependency_spec(const char *spec, char **out_name,
                                 char **out_version) {
  const char *at = strchr(spec, '@');
  size_t name_len = at ? (size_t)(at - spec) : strlen(spec);
  const char *version = at ? at + 1 : "*";
  if (name_len == 0 || version[0] == '\0' || strchr(version, '@')) {
    fprintf(
        stderr,
        "pie: error: dependency must look like package or package@version\n");
    return 0;
  }

  char *name = (char *)malloc(name_len + 1);
  char *version_copy = driver_strdup(version);
  if (!name || !version_copy) {
    fprintf(stderr, "pie: error: out of memory while parsing dependency\n");
    free(name);
    free(version_copy);
    return 0;
  }
  memcpy(name, spec, name_len);
  name[name_len] = '\0';
  if (!valid_package_name(name)) {
    fprintf(stderr,
            "pie: error: dependency name '%s' must use snake_case letters, "
            "digits, and underscores\n",
            name);
    free(name);
    free(version_copy);
    return 0;
  }

  *out_name = name;
  *out_version = version_copy;
  return 1;
}

static char *find_package_root(void) {
  char *dir = getcwd(NULL, 0);
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
    fprintf(stderr, "pie: error: could not open package manifest '%s': %s\n",
            manifest_path, strerror(errno));
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
    if (!in_package) {
      continue;
    }
    if (strncmp(s, "name", 4) == 0) {
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
      name = driver_strdup(s);
      break;
    }
  }
  fclose(file);

  if (!name || name[0] == '\0') {
    free(name);
    fprintf(stderr,
            "pie: error: package manifest '%s' must define [package] name\n",
            manifest_path);
    return NULL;
  }
  return name;
}

typedef struct PieDriverPackage {
  char *root;
  char *manifest_path;
  char *name;
  char *main_path;
  char *build_dir;
  char *default_output;
} PieDriverPackage;

static void package_free(PieDriverPackage *pkg) {
  free(pkg->root);
  free(pkg->manifest_path);
  free(pkg->name);
  free(pkg->main_path);
  free(pkg->build_dir);
  free(pkg->default_output);
  memset(pkg, 0, sizeof(*pkg));
}

static int package_load(PieDriverPackage *pkg, int emit_not_found) {
  memset(pkg, 0, sizeof(*pkg));
  pkg->root = find_package_root();
  if (!pkg->root) {
    if (emit_not_found) {
      fprintf(
          stderr,
          "pie: error: no pie.toml found in this directory or its parents\n");
    }
    return 0;
  }

  pkg->manifest_path = path_join(pkg->root, "pie.toml");
  pkg->name =
      pkg->manifest_path ? parse_package_name(pkg->manifest_path) : NULL;
  char *src_dir = path_join(pkg->root, "src");
  pkg->main_path = src_dir ? path_join(src_dir, "main.pie") : NULL;
  free(src_dir);
  pkg->build_dir = path_join(pkg->root, "build");
  pkg->default_output = (pkg->build_dir && pkg->name)
                            ? path_join(pkg->build_dir, pkg->name)
                            : NULL;

  if (!pkg->manifest_path || !pkg->name || !pkg->main_path || !pkg->build_dir ||
      !pkg->default_output) {
    fprintf(stderr, "pie: error: out of memory while loading package\n");
    package_free(pkg);
    return 0;
  }
  if (!path_exists(pkg->main_path)) {
    char *lib_path = path_join(pkg->root, "src/lib.pie");
    if (lib_path && path_exists(lib_path)) {
      fprintf(stderr,
              "pie: error: library packages are parsed but not buildable yet; "
              "expected src/main.pie for executable output\n");
    } else {
      fprintf(stderr, "pie: error: package '%s' has no src/main.pie\n",
              pkg->name);
    }
    free(lib_path);
    package_free(pkg);
    return 0;
  }
  return 1;
}

static int run_process(const char *const argv[]) {
  pid_t pid = fork();
  if (pid < 0) {
    fprintf(stderr, "pie: error: fork failed: %s\n", strerror(errno));
    return 1;
  }
  if (pid == 0) {
    execvp(argv[0], (char *const *)argv);
    fprintf(stderr, "pie: error: could not execute %s: %s\n", argv[0],
            strerror(errno));
    _exit(127);
  }

  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    fprintf(stderr, "pie: error: waitpid failed: %s\n", strerror(errno));
    return 1;
  }
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  if (WIFSIGNALED(status)) {
    fprintf(stderr, "pie: error: process %s terminated by signal %d\n", argv[0],
            WTERMSIG(status));
  }
  return 1;
}

int pie_driver_new_package(const char *kind, const char *path) {
  int is_app = strcmp(kind, "app") == 0;
  int is_lib = strcmp(kind, "lib") == 0;
  if (!is_app && !is_lib) {
    fprintf(stderr, "pie: error: package kind must be 'app' or 'lib'\n");
    return 1;
  }

  char *name = package_name_from_path(path);
  if (!name) {
    fprintf(stderr, "pie: error: invalid package path '%s'\n", path);
    return 1;
  }
  if (!valid_package_name(name)) {
    fprintf(stderr,
            "pie: error: package name '%s' must use snake_case letters, "
            "digits, and underscores\n",
            name);
    free(name);
    return 1;
  }
  if (path_exists(path)) {
    fprintf(stderr, "pie: error: path '%s' already exists\n", path);
    free(name);
    return 1;
  }

  char *src_dir = path_join(path, "src");
  char *tests_dir = path_join(path, "tests");
  char *manifest_path = path_join(path, "pie.toml");
  char *source_path =
      path_join(src_dir ? src_dir : "", is_app ? "main.pie" : "lib.pie");
  if (!src_dir || !tests_dir || !manifest_path || !source_path) {
    fprintf(stderr, "pie: error: out of memory while scaffolding package\n");
    free(name);
    free(src_dir);
    free(tests_dir);
    free(manifest_path);
    free(source_path);
    return 1;
  }

  int ok = make_dir(path) && make_dir(src_dir) && make_dir(tests_dir);
  if (ok) {
    char manifest[1024];
    int written = snprintf(manifest, sizeof(manifest),
                           "[package]\nname = \"%s\"\nversion = "
                           "\"0.1.0\"\nedition = \"2026\"\ntype = \"%s\"\n",
                           name, is_app ? "app" : "lib");
    if (written < 0 || (size_t)written >= sizeof(manifest)) {
      fprintf(stderr, "pie: error: package manifest for '%s' is too large\n",
              name);
      ok = 0;
    } else {
      ok = write_text_file(manifest_path, manifest);
    }
  }

  if (ok && is_app) {
    char main_source[1024];
    int written = snprintf(
        main_source, sizeof(main_source),
        "require \"io\"\n\nfn main():\n    println(\"hello from %s\")\nend\n",
        name);
    if (written < 0 || (size_t)written >= sizeof(main_source)) {
      fprintf(stderr, "pie: error: app template for '%s' is too large\n", name);
      ok = 0;
    } else {
      ok = write_text_file(source_path, main_source);
    }
  }

  if (ok && is_lib) {
    ok = write_text_file(source_path,
                         "pub fn answer() -> int:\n    return 42\nend\n");
  }

  if (ok) {
    printf("created Pie %s package %s\n", is_app ? "app" : "lib", path);
  }

  free(name);
  free(src_dir);
  free(tests_dir);
  free(manifest_path);
  free(source_path);
  return ok ? 0 : 1;
}

static char *dependency_line(const char *name, const char *version) {
  size_t needed = strlen(name) + strlen(version) + 8;
  char *line = (char *)malloc(needed);
  if (!line) {
    return NULL;
  }
  snprintf(line, needed, "%s = \"%s\"\n", name, version);
  return line;
}

static char *manifest_add_dependency(const char *manifest, const char *name,
                                     const char *version) {
  char *dep_line = dependency_line(name, version);
  if (!dep_line) {
    return NULL;
  }

  char *out = NULL;
  size_t out_len = 0;
  size_t out_cap = 0;
  int in_dependencies = 0;
  int saw_dependencies = 0;
  int inserted = 0;
  int ok = 1;

  const char *p = manifest;
  while (*p) {
    const char *line_start = p;
    const char *line_end = strchr(p, '\n');
    size_t line_len =
        line_end ? (size_t)(line_end - line_start + 1) : strlen(line_start);

    if (line_is_section(line_start, line_len, "dependencies")) {
      in_dependencies = 1;
      saw_dependencies = 1;
      ok = append_range(&out, &out_len, &out_cap, line_start, line_len);
      p = line_start + line_len;
      continue;
    }
    if (in_dependencies && line_starts_section(line_start, line_len)) {
      if (!inserted) {
        ok = append_text(&out, &out_len, &out_cap, dep_line);
        inserted = 1;
      }
      in_dependencies = 0;
    }
    if (in_dependencies && line_is_dependency(line_start, line_len, name)) {
      ok = append_text(&out, &out_len, &out_cap, dep_line);
      inserted = 1;
    } else {
      ok = append_range(&out, &out_len, &out_cap, line_start, line_len);
    }
    if (!ok) {
      break;
    }
    p = line_start + line_len;
  }

  if (ok && saw_dependencies && in_dependencies && !inserted) {
    ok = append_text(&out, &out_len, &out_cap, dep_line);
    inserted = 1;
  }
  if (ok && !saw_dependencies) {
    if (out_len > 0 && out[out_len - 1] != '\n') {
      ok = append_text(&out, &out_len, &out_cap, "\n");
    }
    if (ok) {
      ok = append_text(&out, &out_len, &out_cap, "\n[dependencies]\n");
    }
    if (ok) {
      ok = append_text(&out, &out_len, &out_cap, dep_line);
    }
  }

  free(dep_line);
  if (!ok) {
    free(out);
    return NULL;
  }
  return out ? out : driver_strdup("");
}

static char *manifest_remove_dependency(const char *manifest, const char *name,
                                        int *out_removed) {
  char *out = NULL;
  size_t out_len = 0;
  size_t out_cap = 0;
  int in_dependencies = 0;
  int removed = 0;
  int ok = 1;

  const char *p = manifest;
  while (*p) {
    const char *line_start = p;
    const char *line_end = strchr(p, '\n');
    size_t line_len =
        line_end ? (size_t)(line_end - line_start + 1) : strlen(line_start);

    if (line_is_section(line_start, line_len, "dependencies")) {
      in_dependencies = 1;
    } else if (in_dependencies && line_starts_section(line_start, line_len)) {
      in_dependencies = 0;
    }

    if (in_dependencies && line_is_dependency(line_start, line_len, name)) {
      removed = 1;
    } else {
      ok = append_range(&out, &out_len, &out_cap, line_start, line_len);
    }
    if (!ok) {
      break;
    }
    p = line_start + line_len;
  }

  if (!ok) {
    free(out);
    return NULL;
  }
  *out_removed = removed;
  return out ? out : driver_strdup("");
}

static char *current_manifest_path(void) {
  char *root = find_package_root();
  if (!root) {
    fprintf(stderr,
            "pie: error: no pie.toml found in this directory or its parents\n");
    return NULL;
  }
  char *manifest_path = path_join(root, "pie.toml");
  free(root);
  if (!manifest_path) {
    fprintf(stderr,
            "pie: error: out of memory while finding package manifest\n");
  }
  return manifest_path;
}

int pie_driver_add_dependency(const char *spec) {
  char *name = NULL;
  char *version = NULL;
  if (!split_dependency_spec(spec, &name, &version)) {
    return 1;
  }

  char *manifest_path = current_manifest_path();
  char *manifest = manifest_path ? read_text_file(manifest_path) : NULL;
  char *next_manifest =
      manifest ? manifest_add_dependency(manifest, name, version) : NULL;
  int status = 1;
  if (next_manifest && write_text_file(manifest_path, next_manifest)) {
    printf("added dependency %s@%s\n", name, version);
    status = 0;
  } else if (manifest) {
    fprintf(stderr, "pie: error: out of memory while updating dependencies\n");
  }

  free(name);
  free(version);
  free(manifest_path);
  free(manifest);
  free(next_manifest);
  return status;
}

int pie_driver_remove_dependency(const char *name) {
  if (!valid_package_name(name)) {
    fprintf(stderr,
            "pie: error: dependency name '%s' must use snake_case letters, "
            "digits, and underscores\n",
            name);
    return 1;
  }

  char *manifest_path = current_manifest_path();
  char *manifest = manifest_path ? read_text_file(manifest_path) : NULL;
  int removed = 0;
  char *next_manifest =
      manifest ? manifest_remove_dependency(manifest, name, &removed) : NULL;
  int status = 1;
  if (next_manifest && !removed) {
    fprintf(stderr, "pie: error: dependency '%s' is not in pie.toml\n", name);
  } else if (next_manifest && write_text_file(manifest_path, next_manifest)) {
    printf("removed dependency %s\n", name);
    status = 0;
  } else if (manifest) {
    fprintf(stderr, "pie: error: out of memory while updating dependencies\n");
  }

  free(manifest_path);
  free(manifest);
  free(next_manifest);
  return status;
}

int pie_driver_check(const char *input_path) {
  PieDiagnosticBag diag;
  pie_diag_init(&diag);

  PieSource source;
  PieProgram program;
  PieIrProgram ir;
  int parsed = 0;
  int lowered = 0;
  int ok = pie_source_read_file(input_path, &source, &diag);
  if (ok) {
    ok = pie_parse_source(&source, &program, &diag);
    parsed = 1;
    if (ok) {
      ok = pie_resolve_requires(&program, source.path, &diag);
    }
    if (ok) {
      ok = pie_load_modules(&program, source.path, &diag);
    }
    if (ok) {
      ok = pie_sema_program(&program, &diag);
    }
    if (ok) {
      ok = pie_borrowcheck_program(&program, &diag);
    }
    if (ok) {
      ok = pie_lower_program(&program, &ir, &diag);
      lowered = 1;
    }
    if (lowered) {
      pie_ir_program_free(&ir);
    }
    if (parsed) {
      pie_program_free(&program);
    }
    pie_source_free(&source);
  }

  if (!ok || diag.has_error) {
    pie_diag_print(&diag);
    pie_diag_free(&diag);
    return 1;
  }
  pie_diag_free(&diag);
  return 0;
}

int pie_driver_check_package(void) {
  PieDriverPackage pkg;
  if (!package_load(&pkg, 1)) {
    return 1;
  }
  int status = pie_driver_check(pkg.main_path);
  package_free(&pkg);
  return status;
}

int pie_driver_emit_asm(const char *input_path, const char *output_path) {
  PieDiagnosticBag diag;
  pie_diag_init(&diag);

  PieSource source;
  PieProgram program;
  PieIrProgram ir;
  int parsed = 0;
  int lowered = 0;
  int ok = pie_source_read_file(input_path, &source, &diag);
  if (ok) {
    ok = pie_parse_source(&source, &program, &diag);
    parsed = 1;
    if (ok) {
      ok = pie_resolve_requires(&program, source.path, &diag);
    }
    if (ok) {
      ok = pie_load_modules(&program, source.path, &diag);
    }
    if (ok) {
      ok = pie_sema_program(&program, &diag);
    }
    if (ok) {
      ok = pie_borrowcheck_program(&program, &diag);
    }
    if (ok) {
      ok = pie_lower_program(&program, &ir, &diag);
      lowered = 1;
    }
    if (ok) {
      ok = pie_emit_linux_x64_asm(&ir, output_path, &diag);
    }
    if (lowered) {
      pie_ir_program_free(&ir);
    }
    if (parsed) {
      pie_program_free(&program);
    }
    pie_source_free(&source);
  }

  if (!ok || diag.has_error) {
    pie_diag_print(&diag);
    pie_diag_free(&diag);
    return 1;
  }
  pie_diag_free(&diag);
  return 0;
}

int pie_driver_emit_ir(const char *input_path, const char *output_path) {
  PieDiagnosticBag diag;
  pie_diag_init(&diag);

  PieSource source;
  PieProgram program;
  PieIrProgram ir;
  int parsed = 0;
  int lowered = 0;
  int ok = pie_source_read_file(input_path, &source, &diag);
  if (ok) {
    ok = pie_parse_source(&source, &program, &diag);
    parsed = 1;
    if (ok) {
      ok = pie_resolve_requires(&program, source.path, &diag);
    }
    if (ok) {
      ok = pie_load_modules(&program, source.path, &diag);
    }
    if (ok) {
      ok = pie_sema_program(&program, &diag);
    }
    if (ok) {
      ok = pie_borrowcheck_program(&program, &diag);
    }
    if (ok) {
      ok = pie_lower_program(&program, &ir, &diag);
      lowered = 1;
    }
    if (ok) {
      FILE *out = fopen(output_path, "wb");
      if (!out) {
        pie_diag_errorf(&diag, "could not open IR output '%s'", output_path);
        ok = 0;
      } else {
        pie_ir_program_write_text(&ir, out);
        fclose(out);
      }
    }
    if (lowered) {
      pie_ir_program_free(&ir);
    }
    if (parsed) {
      pie_program_free(&program);
    }
    pie_source_free(&source);
  }

  if (!ok || diag.has_error) {
    pie_diag_print(&diag);
    pie_diag_free(&diag);
    return 1;
  }
  pie_diag_free(&diag);
  return 0;
}

int pie_driver_build(const char *input_path, const char *output_path,
                     int keep_asm) {
  char *asm_path = path_with_suffix(output_path, ".asm");
  char *obj_path = path_with_suffix(output_path, ".o");
  char *runtime_obj = path_with_suffix(output_path, ".runtime.o");
  char *runtime_c_obj = path_with_suffix(output_path, ".runtime_c.o");
  char *wideint_obj = path_with_suffix(output_path, ".wideint.o");
  char *widefloat_obj = path_with_suffix(output_path, ".widefloat.o");
  char *maybe_obj = path_with_suffix(output_path, ".maybe.o");
  char *threads_obj = path_with_suffix(output_path, ".threads.o");
  if (!asm_path || !obj_path || !runtime_obj || !runtime_c_obj ||
      !wideint_obj || !widefloat_obj || !maybe_obj || !threads_obj) {
    fprintf(stderr, "pie: error: out of memory while preparing build paths\n");
    free(asm_path);
    free(obj_path);
    free(runtime_obj);
    free(runtime_c_obj);
    free(wideint_obj);
    free(widefloat_obj);
    free(maybe_obj);
    free(threads_obj);
    return 1;
  }

  int status = pie_driver_emit_asm(input_path, asm_path);
  if (status != 0) {
    free(asm_path);
    free(obj_path);
    free(runtime_obj);
    free(wideint_obj);
    free(widefloat_obj);
    return status;
  }

  const char *runtime_src = PIE_SOURCE_ROOT "/runtime/asm/linux_x64/start.asm";
  const char *runtime_c_src = PIE_SOURCE_ROOT "/runtime/c/runtime.c";
  const char *wideint_src = PIE_SOURCE_ROOT "/runtime/c/wideint.c";
  const char *widefloat_src = PIE_SOURCE_ROOT "/runtime/c/widefloat.c";
  const char *maybe_src = PIE_SOURCE_ROOT "/runtime/c/pie_maybe.c";
  const char *threads_src = PIE_SOURCE_ROOT "/runtime/c/threads.c";
  const char *nasm_runtime[] = {"nasm", "-felf64",   runtime_src,
                                "-o",   runtime_obj, NULL};
  const char *nasm_user[] = {"nasm", "-felf64", asm_path, "-o", obj_path, NULL};
  const char *cc_runtime_c[] = {"cc",          "-c",          "-O2", "-o",
                                runtime_c_obj, runtime_c_src, NULL};
  const char *cc_wideint[] = {"cc",        "-c",        "-O2", "-o",
                              wideint_obj, wideint_src, NULL};
  const char *cc_widefloat[] = {"cc",          "-c",          "-O2", "-o",
                                widefloat_obj, widefloat_src, NULL};
  const char *cc_maybe[] = {"cc",      "-c",      "-O2", "-o",
                            maybe_obj, maybe_src, NULL};
  const char *cc_threads[] = {
      "cc", "-c",        "-O2",       "-I" PIE_SOURCE_ROOT "/runtime/include",
      "-o", threads_obj, threads_src, NULL};
  const char *ld_cmd[] = {
      "cc",        "-nostartfiles", "-no-pie",   "-o",          output_path,
      runtime_obj, runtime_c_obj,   wideint_obj, widefloat_obj, maybe_obj,
      threads_obj, obj_path,        "-lm",       "-lpthread",   NULL};

  status = run_process(nasm_runtime);
  if (status == 0)
    status = run_process(cc_runtime_c);
  if (status == 0)
    status = run_process(cc_wideint);
  if (status == 0)
    status = run_process(cc_widefloat);
  if (status == 0)
    status = run_process(cc_maybe);
  if (status == 0)
    status = run_process(cc_threads);
  if (status == 0)
    status = run_process(nasm_user);
  if (status == 0)
    status = run_process(ld_cmd);

  if (!keep_asm) {
    unlink(asm_path);
  }
  unlink(obj_path);
  unlink(runtime_obj);
  unlink(runtime_c_obj);
  unlink(wideint_obj);
  unlink(widefloat_obj);
  unlink(maybe_obj);
  unlink(threads_obj);

  free(asm_path);
  free(obj_path);
  free(runtime_obj);
  free(runtime_c_obj);
  free(wideint_obj);
  free(widefloat_obj);
  free(maybe_obj);
  free(threads_obj);
  return status;
}

int pie_driver_build_package(const char *output_path, int keep_asm) {
  PieDriverPackage pkg;
  if (!package_load(&pkg, 1)) {
    return 1;
  }
  const char *out = output_path ? output_path : pkg.default_output;
  if (!output_path && !ensure_dir(pkg.build_dir)) {
    package_free(&pkg);
    return 1;
  }
  int status = pie_driver_build(pkg.main_path, out, keep_asm);
  package_free(&pkg);
  return status;
}

int pie_driver_run(const char *input_path) {
  char output_path[] = "/tmp/pie-run-XXXXXX";
  int fd = mkstemp(output_path);
  if (fd < 0) {
    fprintf(stderr,
            "pie: error: could not create temporary executable path: %s\n",
            strerror(errno));
    return 1;
  }
  close(fd);
  unlink(output_path);

  int status = pie_driver_build(input_path, output_path, 0);
  if (status != 0) {
    return status;
  }

  char *exe = driver_strdup(output_path);
  if (!exe) {
    fprintf(stderr, "pie: error: out of memory while running executable\n");
    unlink(output_path);
    return 1;
  }
  const char *argv[] = {exe, NULL};
  status = run_process(argv);
  unlink(output_path);
  free(exe);
  return status;
}

int pie_driver_run_package(void) {
  PieDriverPackage pkg;
  if (!package_load(&pkg, 1)) {
    return 1;
  }
  if (!ensure_dir(pkg.build_dir)) {
    package_free(&pkg);
    return 1;
  }

  int status = pie_driver_build(pkg.main_path, pkg.default_output, 0);
  if (status == 0) {
    char *exe = driver_strdup(pkg.default_output);
    if (!exe) {
      fprintf(stderr,
              "pie: error: out of memory while running package executable\n");
      package_free(&pkg);
      return 1;
    }
    const char *argv[] = {exe, NULL};
    status = run_process(argv);
    free(exe);
  }
  package_free(&pkg);
  return status;
}

static int run_package_tests(const PieDriverPackage *pkg) {
  char *tests_dir = path_join(pkg->root, "tests");
  if (!tests_dir) {
    fprintf(stderr, "pie: error: out of memory while finding package tests\n");
    return 1;
  }
  DIR *dir = opendir(tests_dir);
  if (!dir) {
    printf("pie: no tests found\n");
    free(tests_dir);
    return 0;
  }

  int status = 0;
  int count = 0;
  struct dirent *entry = NULL;
  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.' || !has_suffix(entry->d_name, ".pie")) {
      continue;
    }
    char *path = path_join(tests_dir, entry->d_name);
    if (!path) {
      fprintf(stderr,
              "pie: error: out of memory while running package tests\n");
      status = 1;
      break;
    }
    count++;
    int test_status = pie_driver_run(path);
    free(path);
    if (test_status != 0) {
      status = test_status;
      break;
    }
  }
  closedir(dir);
  free(tests_dir);

  if (count == 0 && status == 0) {
    printf("pie: no tests found\n");
  }
  return status;
}

int pie_driver_test_package(void) {
  PieDriverPackage pkg;
  if (!package_load(&pkg, 1)) {
    return 1;
  }
  int status = run_package_tests(&pkg);
  package_free(&pkg);
  return status;
}

int pie_driver_test(const char *input_path) {
  if (input_path) {
    return pie_driver_run(input_path);
  }

  PieDriverPackage pkg;
  if (package_load(&pkg, 0)) {
    int status = run_package_tests(&pkg);
    package_free(&pkg);
    return status;
  }

  const char *argv[] = {"ctest", "--test-dir", PIE_SOURCE_ROOT "/build",
                        "--output-on-failure", NULL};
  return run_process(argv);
}
