#define _POSIX_C_SOURCE 200809L

#include "pie/core/modules/loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pie/core/parser/parser.h"
#include "pie/core/source/source.h"

static char *ldup(const char *s) {
  if (!s)
    return NULL;
  size_t len = strlen(s);
  char *copy = (char *)malloc(len + 1);
  if (!copy)
    return NULL;
  memcpy(copy, s, len + 1);
  return copy;
}

static void *dup_mem(const void *src, size_t size) {
  if (!src || size == 0)
    return NULL;
  void *copy = malloc(size);
  if (!copy)
    return NULL;
  memcpy(copy, src, size);
  return copy;
}

static PieAstType deep_copy_type(PieAstType t) {
  if (t.kind == PIE_AST_TYPE_STRUCT) {
    t.struct_name = ldup(t.struct_name);
  } else {
    t.struct_name = NULL;
  }
  if (t.kind == PIE_AST_TYPE_ENUM) {
    t.enum_name = ldup(t.enum_name);
  } else {
    t.enum_name = NULL;
  }
  if (t.kind == PIE_AST_TYPE_CLOSURE && t.func_param_kinds &&
      t.func_param_count > 0) {
    t.func_param_kinds = dup_mem(t.func_param_kinds,
                                 t.func_param_count * sizeof(PieAstTypeKind));
    t.func_param_widths =
        dup_mem(t.func_param_widths, t.func_param_count * sizeof(int));
  } else {
    t.func_param_kinds = NULL;
    t.func_param_widths = NULL;
  }
  return t;
}

static PieFunction deep_copy_function(const PieFunction *src) {
  PieFunction f = *src;
  f.name = ldup(src->name);
  f.borrowed_body = 0;
  if (src->param_count > 0) {
    f.param_names = (char **)malloc(src->param_count * sizeof(char *));
    f.param_types = (PieAstType *)malloc(src->param_count * sizeof(PieAstType));
    for (size_t i = 0; i < src->param_count; i++) {
      f.param_names[i] = ldup(src->param_names[i]);
      f.param_types[i] = deep_copy_type(src->param_types[i]);
    }
  }

  f.body = src->body;
  f.borrowed_body = 1;
  return f;
}

static PieStructDef deep_copy_struct_def(const PieStructDef *src) {
  PieStructDef d;
  d.name = ldup(src->name);
  d.is_pub = src->is_pub;
  d.is_export = src->is_export;
  d.field_count = src->field_count;
  d.field_capacity = src->field_count;
  if (src->field_count > 0) {
    d.fields =
        (PieStructField *)malloc(src->field_count * sizeof(PieStructField));
    for (size_t i = 0; i < src->field_count; i++) {
      d.fields[i].name = ldup(src->fields[i].name);
      d.fields[i].type = deep_copy_type(src->fields[i].type);
    }
  } else {
    d.fields = NULL;
  }
  return d;
}

static PieEnumDef deep_copy_enum_def(const PieEnumDef *src) {
  PieEnumDef d;
  d.name = ldup(src->name);
  d.is_pub = src->is_pub;
  d.is_export = src->is_export;
  d.variant_count = src->variant_count;
  for (size_t i = 0; i < src->variant_count; i++) {
    d.variants[i].name = ldup(src->variants[i].name);
    d.variants[i].payload_count = src->variants[i].payload_count;
    if (src->variants[i].payload_count > 0) {
      d.variants[i].payload_kinds =
          dup_mem(src->variants[i].payload_kinds,
                  src->variants[i].payload_count * sizeof(PieAstTypeKind));
      d.variants[i].payload_widths =
          dup_mem(src->variants[i].payload_widths,
                  src->variants[i].payload_count * sizeof(int));
    } else {
      d.variants[i].payload_kinds = NULL;
      d.variants[i].payload_widths = NULL;
    }
  }
  return d;
}

static char *path_join(const char *left, const char *right) {
  size_t a = strlen(left);
  size_t b = strlen(right);
  int needs_slash = a > 0 && left[a - 1] != '/';
  char *out = (char *)malloc(a + (needs_slash ? 1 : 0) + b + 1);
  if (!out)
    return NULL;
  memcpy(out, left, a);
  size_t pos = a;
  if (needs_slash)
    out[pos++] = '/';
  memcpy(out + pos, right, b + 1);
  return out;
}

static char *path_with_suffix(const char *path, const char *suffix) {
  size_t a = strlen(path);
  size_t b = strlen(suffix);
  char *out = (char *)malloc(a + b + 1);
  if (!out)
    return NULL;
  memcpy(out, path, a);
  memcpy(out + a, suffix, b + 1);
  return out;
}

static char *parent_dir(const char *path) {
  char *copy = ldup(path);
  if (!copy)
    return NULL;
  size_t len = strlen(copy);
  while (len > 1 && copy[len - 1] == '/')
    copy[--len] = '\0';
  char *slash = strrchr(copy, '/');
  if (!slash) {
    free(copy);
    return ldup(".");
  }
  if (slash == copy)
    slash[1] = '\0';
  else
    *slash = '\0';
  return copy;
}

static int path_exists(const char *path) { return access(path, F_OK) == 0; }

typedef struct LoadedModule {
  char *path;
} LoadedModule;

static int is_already_loaded(const LoadedModule *loaded, size_t count,
                             const char *path) {
  for (size_t i = 0; i < count; i++) {
    if (strcmp(loaded[i].path, path) == 0)
      return 1;
  }
  return 0;
}

static int function_exists(const PieProgram *program, const char *name) {
  for (size_t i = 0; i < program->function_count; i++) {
    if (strcmp(program->functions[i].name, name) == 0)
      return 1;
  }
  return 0;
}

static int struct_exists(const PieProgram *program, const char *name) {
  for (size_t i = 0; i < program->struct_count; i++) {
    if (strcmp(program->structs[i].name, name) == 0)
      return 1;
  }
  return 0;
}

static int enum_exists(const PieProgram *program, const char *name) {
  for (size_t i = 0; i < program->enum_count; i++) {
    if (strcmp(program->enums[i].name, name) == 0)
      return 1;
  }
  return 0;
}

static char *find_package_root_for_source(const char *source_path) {
  char *dir = parent_dir(source_path);
  if (!dir)
    return NULL;
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
  while (*s == ' ' || *s == '\t')
    s++;
  return s;
}

static void trim_right_in_place(char *s) {
  size_t len = strlen(s);
  while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' ||
                     s[len - 1] == ' ' || s[len - 1] == '\t'))
    s[--len] = '\0';
}

static char *parse_package_name(const char *manifest_path) {
  FILE *file = fopen(manifest_path, "rb");
  if (!file)
    return NULL;
  char line[1024];
  int in_package = 0;
  char *name = NULL;
  while (fgets(line, sizeof(line), file)) {
    trim_right_in_place(line);
    char *s = trim_left(line);
    if (*s == '\0' || *s == '#')
      continue;
    if (*s == '[') {
      in_package = strcmp(s, "[package]") == 0;
      continue;
    }
    if (!in_package || strncmp(s, "name", 4) != 0)
      continue;
    s += 4;
    s = trim_left(s);
    if (*s != '=')
      continue;
    s++;
    s = trim_left(s);
    if (*s != '"')
      continue;
    s++;
    char *end = strchr(s, '"');
    if (!end)
      continue;
    *end = '\0';
    name = ldup(s);
    break;
  }
  fclose(file);
  return name;
}

static int resolve_module_path(const char *require_path,
                               const char *source_path, char **out_path) {
  char *root = find_package_root_for_source(source_path);
  if (!root)
    return 0;

  char *manifest = path_join(root, "pie.toml");
  char *package_name = manifest ? parse_package_name(manifest) : NULL;
  free(manifest);
  if (!package_name) {
    free(root);
    return 0;
  }

  const char *slash = strchr(require_path, '/');
  size_t prefix_len =
      slash ? (size_t)(slash - require_path) : strlen(require_path);

  if (strlen(package_name) != prefix_len ||
      memcmp(require_path, package_name, prefix_len) != 0) {
    free(package_name);
    free(root);
    return 0;
  }

  char *src_dir = path_join(root, "src");
  char *module_no_ext =
      src_dir ? path_join(src_dir, slash ? slash + 1 : require_path) : NULL;
  char *module_path =
      module_no_ext ? path_with_suffix(module_no_ext, ".pie") : NULL;
  free(src_dir);
  free(module_no_ext);
  free(package_name);
  free(root);

  if (!module_path || !path_exists(module_path)) {
    free(module_path);
    return 0;
  }

  *out_path = module_path;
  return 1;
}

static int load_module_recursive(PieProgram *program, const char *module_path,
                                 LoadedModule *loaded, size_t *loaded_count,
                                 size_t loaded_capacity,
                                 PieDiagnosticBag *diag);

static int merge_module(PieProgram *dest, const PieProgram *src,
                        const char *module_path, PieDiagnosticBag *diag) {
  for (size_t i = 0; i < src->function_count; i++) {
    const PieFunction *func = &src->functions[i];
    if (!func->is_pub && !func->is_export)
      continue;
    if (function_exists(dest, func->name)) {
      pie_diag_errorf(diag, "duplicate function '%s' from module '%s'",
                      func->name, module_path);
      return 0;
    }
    PieFunction copy = deep_copy_function(func);
    if (!pie_program_push_function(dest, copy)) {
      pie_diag_error(diag, "out of memory while merging module function");
      return 0;
    }
  }
  for (size_t i = 0; i < src->struct_count; i++) {
    const PieStructDef *def = &src->structs[i];
    if (!def->is_pub && !def->is_export)
      continue;
    if (struct_exists(dest, def->name)) {
      pie_diag_errorf(diag, "duplicate struct '%s' from module '%s'", def->name,
                      module_path);
      return 0;
    }
    PieStructDef copy = deep_copy_struct_def(def);
    if (!pie_program_push_struct(dest, copy)) {
      pie_diag_error(diag, "out of memory while merging module struct");
      return 0;
    }
  }
  for (size_t i = 0; i < src->enum_count; i++) {
    const PieEnumDef *def = &src->enums[i];
    if (!def->is_pub && !def->is_export)
      continue;
    if (enum_exists(dest, def->name)) {

      if (strcmp(def->name, "Option") == 0 ||
          strcmp(def->name, "Result") == 0) {
        continue;
      }
      pie_diag_errorf(diag, "duplicate enum '%s' from module '%s'", def->name,
                      module_path);
      return 0;
    }
    PieEnumDef copy = deep_copy_enum_def(def);
    if (!pie_program_push_enum(dest, copy)) {
      pie_diag_error(diag, "out of memory while merging module enum");
      return 0;
    }
  }
  return 1;
}

static int load_module_recursive(PieProgram *program, const char *module_path,
                                 LoadedModule *loaded, size_t *loaded_count,
                                 size_t loaded_capacity,
                                 PieDiagnosticBag *diag) {
  if (is_already_loaded(loaded, *loaded_count, module_path)) {
    return 1;
  }
  if (*loaded_count >= loaded_capacity) {
    pie_diag_error(diag, "too many module imports (circular dependency?)");
    return 0;
  }

  PieSource source;
  if (!pie_source_read_file(module_path, &source, diag)) {
    return 0;
  }

  PieProgram module_program;
  memset(&module_program, 0, sizeof(module_program));
  if (!pie_parse_source(&source, &module_program, diag)) {
    pie_source_free(&source);
    return 0;
  }

  loaded[*loaded_count].path = ldup(module_path);
  (*loaded_count)++;

  for (size_t i = 0; i < module_program.require_count; i++) {
    const PieRequire *req = &module_program.requires[i];
    if (req->kind == PIE_REQUIRE_STD)
      continue;

    char *req_file_path = NULL;
    if (!resolve_module_path(req->path, module_path, &req_file_path)) {
      pie_diag_errorf(diag, "required module '%s' not found from '%s'",
                      req->path, module_path);
      pie_program_free(&module_program);
      pie_source_free(&source);
      return 0;
    }
    if (!load_module_recursive(program, req_file_path, loaded, loaded_count,
                               loaded_capacity, diag)) {
      free(req_file_path);
      pie_program_free(&module_program);
      pie_source_free(&source);
      return 0;
    }
    free(req_file_path);
  }

  if (!merge_module(program, &module_program, module_path, diag)) {
    pie_program_free(&module_program);
    pie_source_free(&source);
    return 0;
  }

  PieProgram *owned = (PieProgram *)malloc(sizeof(PieProgram));
  if (owned) {
    *owned = module_program;
    if (!pie_program_push_owned_module(program, owned)) {
      pie_program_free(owned);
      free(owned);
    }
  } else {
    pie_program_free(&module_program);
  }
  pie_source_free(&source);
  return 1;
}

int pie_load_modules(PieProgram *program, const char *source_path,
                     PieDiagnosticBag *diag) {
  size_t capacity = 64;
  LoadedModule *loaded = (LoadedModule *)calloc(capacity, sizeof(LoadedModule));
  if (!loaded) {
    pie_diag_error(diag, "out of memory while initializing module loader");
    return 0;
  }
  size_t loaded_count = 0;

  int ok = 1;
  for (size_t i = 0; i < program->require_count; i++) {
    const PieRequire *req = &program->requires[i];
    if (req->kind == PIE_REQUIRE_STD)
      continue;

    char *module_path = NULL;
    if (!resolve_module_path(req->path, source_path, &module_path)) {
      pie_diag_errorf(diag, "required module '%s' not found", req->path);
      ok = 0;
      break;
    }
    if (!load_module_recursive(program, module_path, loaded, &loaded_count,
                               capacity, diag)) {
      free(module_path);
      ok = 0;
      break;
    }
    free(module_path);
  }

  for (size_t i = 0; i < loaded_count; i++) {
    free(loaded[i].path);
  }
  free(loaded);
  return ok;
}
