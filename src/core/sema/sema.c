#include "pie/core/sema/sema.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct SemaSymbol {
  char *name;
  PieType type;
  int is_mut;
} SemaSymbol;

typedef struct SemaFunction {
  char *name;
  PieType return_type;
  PieType *param_types;
  size_t param_count;
  int is_unsafe;
  char **type_param_names;
  size_t type_param_count;
  char **type_param_constraints;
} SemaFunction;

struct PieSema {
  SemaSymbol *symbols;
  size_t symbol_count;
  size_t symbol_capacity;
  size_t *scope_marks;
  size_t scope_count;
  size_t scope_capacity;
  SemaFunction *functions;
  size_t function_count;
  size_t function_capacity;
  size_t loop_depth;
  size_t unsafe_depth;
  PieType current_return_type;
  PieDiagnosticBag *diag;
  const PieProgram *program;
  PieFunction *pending_mono_funcs;
  size_t pending_mono_count;
  size_t pending_mono_capacity;
};

static char *sema_strdup(const char *s) {
  size_t len = strlen(s);
  char *copy = (char *)malloc(len + 1);
  if (!copy) {
    return NULL;
  }
  memcpy(copy, s, len + 1);
  return copy;
}

static void sema_clear_symbols(PieSema *sema) {
  for (size_t i = 0; i < sema->symbol_count; i++) {
    free(sema->symbols[i].name);
    if (sema->symbols[i].type.kind == PIE_TYPE_STRUCT) {
      free(sema->symbols[i].type.struct_name);
    }
    if (sema->symbols[i].type.kind == PIE_TYPE_ENUM) {
      free(sema->symbols[i].type.enum_name);
    }
  }
  sema->symbol_count = 0;
  sema->scope_count = 0;
  sema->loop_depth = 0;
  sema->unsafe_depth = 0;
}

static void sema_free(PieSema *sema) {
  sema_clear_symbols(sema);
  for (size_t i = 0; i < sema->function_count; i++) {
    free(sema->functions[i].name);
    free(sema->functions[i].param_types);
  }
  free(sema->symbols);
  free(sema->scope_marks);
  free(sema->functions);
  sema->symbols = NULL;
  sema->scope_marks = NULL;
  sema->functions = NULL;
  sema->symbol_count = 0;
  sema->symbol_capacity = 0;
  sema->scope_count = 0;
  sema->scope_capacity = 0;
  sema->function_count = 0;
  sema->function_capacity = 0;
  for (size_t i = 0; i < sema->pending_mono_count; i++) {
    free(sema->pending_mono_funcs[i].name);
    for (size_t j = 0; j < sema->pending_mono_funcs[i].param_count; j++) {
      free(sema->pending_mono_funcs[i].param_names[j]);
    }
    free(sema->pending_mono_funcs[i].param_names);
    free(sema->pending_mono_funcs[i].param_types);
  }
  free(sema->pending_mono_funcs);
  sema->pending_mono_funcs = NULL;
  sema->pending_mono_count = 0;
  sema->pending_mono_capacity = 0;
}

static int sema_push_pending_mono(PieSema *sema, PieFunction func) {
  if (sema->pending_mono_count >= sema->pending_mono_capacity) {
    size_t new_cap =
        sema->pending_mono_capacity ? sema->pending_mono_capacity * 2 : 8;
    PieFunction *new_arr = (PieFunction *)realloc(
        sema->pending_mono_funcs, new_cap * sizeof(PieFunction));
    if (!new_arr)
      return 0;
    sema->pending_mono_funcs = new_arr;
    sema->pending_mono_capacity = new_cap;
  }
  sema->pending_mono_funcs[sema->pending_mono_count++] = func;
  return 1;
}

static void sema_flush_pending_mono(PieSema *sema) {
  PieProgram *prog = (PieProgram *)sema->program;
  for (size_t i = 0; i < sema->pending_mono_count; i++) {
    pie_program_push_function(prog, sema->pending_mono_funcs[i]);

    sema->pending_mono_funcs[i].name = NULL;
    sema->pending_mono_funcs[i].param_names = NULL;
    sema->pending_mono_funcs[i].param_types = NULL;
  }
  sema->pending_mono_count = 0;
}

static SemaSymbol *find_symbol_internal(PieSema *sema, const char *name) {
  for (size_t i = sema->symbol_count; i > 0; i--) {
    SemaSymbol *symbol = &sema->symbols[i - 1];
    if (strcmp(symbol->name, name) == 0) {
      return symbol;
    }
  }
  return NULL;
}

static size_t current_scope_start(PieSema *sema) {
  if (sema->scope_count == 0) {
    return 0;
  }
  return sema->scope_marks[sema->scope_count - 1];
}

static SemaSymbol *find_symbol_in_current_scope(PieSema *sema,
                                                const char *name) {
  size_t start = current_scope_start(sema);
  for (size_t i = sema->symbol_count; i > start; i--) {
    SemaSymbol *symbol = &sema->symbols[i - 1];
    if (strcmp(symbol->name, name) == 0) {
      return symbol;
    }
  }
  return NULL;
}

static SemaFunction *find_function_internal(PieSema *sema, const char *name) {
  for (size_t i = 0; i < sema->function_count; i++) {
    if (strcmp(sema->functions[i].name, name) == 0) {
      return &sema->functions[i];
    }
  }
  return NULL;
}

static int sema_register_mono_func(PieSema *sema, const char *name,
                                   PieType return_type, PieType *param_types,
                                   size_t param_count) {
  if (find_function_internal(sema, name)) {
    return 1;
  }
  if (sema->function_count == sema->function_capacity) {
    size_t next_cap = sema->function_capacity ? sema->function_capacity * 2 : 8;
    SemaFunction *next = (SemaFunction *)realloc(
        sema->functions, next_cap * sizeof(SemaFunction));
    if (!next)
      return 0;
    sema->functions = next;
    sema->function_capacity = next_cap;
  }
  SemaFunction *f = &sema->functions[sema->function_count++];
  memset(f, 0, sizeof(*f));
  f->name = sema_strdup(name);
  f->return_type = return_type;
  f->param_count = param_count;
  f->param_types = (PieType *)calloc(param_count, sizeof(PieType));
  for (size_t i = 0; i < param_count; i++) {
    f->param_types[i] = param_types[i];
  }
  return 1;
}

static PieType type_from_ast(PieAstType ast_type) {
  PieType type;
  memset(&type, 0, sizeof(type));
  switch (ast_type.kind) {
  case PIE_AST_TYPE_VOID:
    type.kind = PIE_TYPE_VOID;
    type.type_width = ast_type.width;
    return type;
  case PIE_AST_TYPE_INT:
    type.kind = PIE_TYPE_INT;
    type.type_width = ast_type.width;
    return type;
  case PIE_AST_TYPE_FLOAT:
    type.kind = PIE_TYPE_FLOAT;
    type.type_width = ast_type.width;
    return type;
  case PIE_AST_TYPE_CHAR:
    type.kind = PIE_TYPE_CHAR;
    type.type_width = ast_type.width;
    return type;
  case PIE_AST_TYPE_BYTE:
    type.kind = PIE_TYPE_BYTE;
    type.type_width = ast_type.width;
    return type;
  case PIE_AST_TYPE_BOOL:
    type.kind = PIE_TYPE_BOOL;
    type.type_width = ast_type.width;
    return type;
  case PIE_AST_TYPE_STRING:
    type.kind = PIE_TYPE_STRING;
    type.type_width = ast_type.width;
    return type;
  case PIE_AST_TYPE_REF:
    type.kind = PIE_TYPE_REF;
    type.type_width = ast_type.width;
    type.ref_inner_kind = type_from_ast(pie_ast_type(ast_type.ref_inner_kind,
                                                     ast_type.ref_inner_width))
                              .kind;
    type.ref_inner_width = ast_type.ref_inner_width;
    type.ref_inner_struct_name =
        ast_type.ref_inner_struct_name
            ? sema_strdup(ast_type.ref_inner_struct_name)
            : NULL;
    return type;
  case PIE_AST_TYPE_REF_MUT:
    type.kind = PIE_TYPE_REF_MUT;
    type.type_width = ast_type.width;
    type.ref_inner_kind = type_from_ast(pie_ast_type(ast_type.ref_inner_kind,
                                                     ast_type.ref_inner_width))
                              .kind;
    type.ref_inner_width = ast_type.ref_inner_width;
    type.ref_inner_struct_name =
        ast_type.ref_inner_struct_name
            ? sema_strdup(ast_type.ref_inner_struct_name)
            : NULL;
    return type;
  case PIE_AST_TYPE_RAW_PTR:
    type.kind = PIE_TYPE_RAW_PTR;
    type.type_width = ast_type.width;
    type.raw_pointee_kind =
        type_from_ast(
            pie_ast_type(ast_type.raw_pointee_kind, ast_type.raw_pointee_width))
            .kind;
    type.raw_pointee_width = ast_type.raw_pointee_width;
    return type;
  case PIE_AST_TYPE_STRUCT:
    type.kind = PIE_TYPE_STRUCT;
    type.struct_name =
        ast_type.struct_name ? sema_strdup(ast_type.struct_name) : NULL;
    type.type_width = ast_type.width;
    return type;
  case PIE_AST_TYPE_NULLABLE: {
    PieType inner = type_from_ast(pie_ast_type(ast_type.nullable_inner_kind,
                                               ast_type.nullable_inner_width));
    type.kind = PIE_TYPE_NULLABLE;
    type.nullable_inner_kind = inner.kind;
    type.nullable_inner_width = inner.type_width;
    return type;
  }
  case PIE_AST_TYPE_TUPLE: {
    type.kind = PIE_TYPE_TUPLE;
    type.tuple_element_count = ast_type.tuple_element_count;
    for (size_t i = 0; i < ast_type.tuple_element_count; i++) {
      PieType elem = type_from_ast(pie_ast_type(
          ast_type.tuple_element_kinds[i], ast_type.tuple_element_widths[i]));
      type.tuple_element_kinds[i] = elem.kind;
      type.tuple_element_widths[i] = elem.type_width;
    }
    return type;
  }
  case PIE_AST_TYPE_LIST: {
    PieType elem = type_from_ast(
        pie_ast_type(ast_type.list_element_kind, ast_type.list_element_width));
    type.kind = PIE_TYPE_LIST;
    type.list_element_kind = elem.kind;
    type.list_element_width = elem.type_width;
    return type;
  }
  case PIE_AST_TYPE_MAP: {
    PieType key = type_from_ast(
        pie_ast_type(ast_type.map_key_kind, ast_type.map_key_width));
    PieType value = type_from_ast(
        pie_ast_type(ast_type.map_value_kind, ast_type.map_value_width));
    type.kind = PIE_TYPE_MAP;
    type.map_key_kind = key.kind;
    type.map_key_width = key.type_width;
    type.map_value_kind = value.kind;
    type.map_value_width = value.type_width;
    return type;
  }
  case PIE_AST_TYPE_ENUM:
    type.kind = PIE_TYPE_ENUM;
    type.enum_name =
        ast_type.enum_name ? sema_strdup(ast_type.enum_name) : NULL;
    return type;
  case PIE_AST_TYPE_CLOSURE:
    type.kind = PIE_TYPE_CLOSURE;
    type.func_param_count = ast_type.func_param_count;
    type.func_return_kind = ast_type.func_return_kind;
    type.func_return_width = ast_type.func_return_width;
    if (ast_type.func_param_count > 0 && ast_type.func_param_kinds) {
      type.func_param_kinds =
          (PieTypeKind *)calloc(ast_type.func_param_count, sizeof(PieTypeKind));
      type.func_param_widths =
          (int *)calloc(ast_type.func_param_count, sizeof(int));
      if (type.func_param_kinds && type.func_param_widths) {
        for (size_t i = 0; i < ast_type.func_param_count; i++) {
          type.func_param_kinds[i] = ast_type.func_param_kinds[i];
          type.func_param_widths[i] = ast_type.func_param_widths[i];
        }
      }
    }
    return type;
  case PIE_AST_TYPE_THREAD:
    type.kind = PIE_TYPE_THREAD;
    return type;
  case PIE_AST_TYPE_MUTEX:
    type.kind = PIE_TYPE_MUTEX;
    return type;
  case PIE_AST_TYPE_CHANNEL:
    type.kind = PIE_TYPE_CHANNEL;
    return type;
  case PIE_AST_TYPE_INFER:
    type.kind = PIE_TYPE_ERROR;
    type.type_width = PIE_WIDTH_INFER;
    return type;
  }
  type.kind = PIE_TYPE_ERROR;
  type.type_width = PIE_WIDTH_INFER;
  return type;
}

static size_t abi_slots_for_type(PieType type) {
  if (type.kind == PIE_TYPE_STRING)
    return 2;
  if (type.kind == PIE_TYPE_REF && type.ref_inner_kind == PIE_TYPE_STRING)
    return 2;
  return 1;
}

static int api_declare_symbol(PieSema *sema, const char *name, PieType type,
                              int is_mut) {
  if (find_symbol_in_current_scope(sema, name)) {
    pie_diag_errorf(sema->diag, "symbol '%s' is already declared", name);
    return 0;
  }

  if (sema->symbol_count == sema->symbol_capacity) {
    size_t next_capacity =
        sema->symbol_capacity ? sema->symbol_capacity * 2 : 16;
    SemaSymbol *next = (SemaSymbol *)realloc(
        sema->symbols, next_capacity * sizeof(SemaSymbol));
    if (!next) {
      pie_diag_error(sema->diag, "out of memory while storing symbol");
      return 0;
    }
    sema->symbols = next;
    sema->symbol_capacity = next_capacity;
  }

  SemaSymbol *symbol = &sema->symbols[sema->symbol_count++];
  symbol->name = sema_strdup(name);
  if (!symbol->name) {
    pie_diag_error(sema->diag, "out of memory while storing symbol name");
    return 0;
  }
  symbol->type = type;
  if (type.kind == PIE_TYPE_STRUCT && type.struct_name) {
    symbol->type.struct_name = sema_strdup(type.struct_name);
  }
  if (type.kind == PIE_TYPE_ENUM && type.enum_name) {
    symbol->type.enum_name = sema_strdup(type.enum_name);
  }
  if (type.kind == PIE_TYPE_REF && type.ref_inner_struct_name) {
    symbol->type.ref_inner_struct_name =
        sema_strdup(type.ref_inner_struct_name);
  }
  if (type.kind == PIE_TYPE_REF_MUT && type.ref_inner_struct_name) {
    symbol->type.ref_inner_struct_name =
        sema_strdup(type.ref_inner_struct_name);
  }
  symbol->is_mut = is_mut;
  return 1;
}

static int api_find_symbol(PieSema *sema, const char *name,
                           PieSymbolInfo *out_symbol) {
  SemaSymbol *symbol = find_symbol_internal(sema, name);
  if (!symbol) {
    return 0;
  }
  out_symbol->type = symbol->type;
  out_symbol->is_mut = symbol->is_mut;
  return 1;
}

static int declare_function(PieSema *sema, const PieFunction *ast_function) {
  PieType return_type = type_from_ast(ast_function->return_type);
  const char *name = ast_function->name;
  if (find_function_internal(sema, name)) {
    pie_diag_errorf(sema->diag, "function '%s' is already declared", name);
    return 0;
  }

  size_t abi_slots = 0;
  for (size_t i = 0; i < ast_function->param_count; i++) {
    abi_slots +=
        abi_slots_for_type(type_from_ast(ast_function->param_types[i]));
  }
  if (abi_slots > 6) {
    pie_diag_errorf(
        sema->diag,
        "function '%s' uses %zu linux x64 argument register slot(s); "
        "stack-passed parameters are not implemented yet (limit 6)",
        name, abi_slots);
    return 0;
  }

  if (sema->function_count == sema->function_capacity) {
    size_t next_capacity =
        sema->function_capacity ? sema->function_capacity * 2 : 8;
    SemaFunction *next = (SemaFunction *)realloc(
        sema->functions, next_capacity * sizeof(SemaFunction));
    if (!next) {
      pie_diag_error(sema->diag, "out of memory while storing function");
      return 0;
    }
    sema->functions = next;
    sema->function_capacity = next_capacity;
  }

  SemaFunction *function = &sema->functions[sema->function_count++];
  memset(function, 0, sizeof(*function));
  function->name = sema_strdup(name);
  if (!function->name) {
    pie_diag_error(sema->diag, "out of memory while storing function name");
    return 0;
  }
  function->return_type = return_type;
  function->is_unsafe = ast_function->is_unsafe;
  function->param_count = ast_function->param_count;
  if (function->param_count) {
    function->param_types =
        (PieType *)calloc(function->param_count, sizeof(PieType));
    if (!function->param_types) {
      pie_diag_error(sema->diag,
                     "out of memory while storing function parameter types");
      return 0;
    }
    for (size_t i = 0; i < function->param_count; i++) {
      function->param_types[i] = type_from_ast(ast_function->param_types[i]);
    }
  }

  function->type_param_count = ast_function->type_param_count;
  if (function->type_param_count) {
    function->type_param_names =
        (char **)calloc(function->type_param_count, sizeof(char *));
    if (!function->type_param_names) {
      pie_diag_error(sema->diag,
                     "out of memory while storing function type parameters");
      return 0;
    }
    for (size_t i = 0; i < function->type_param_count; i++) {
      function->type_param_names[i] = sema_strdup(ast_function->type_params[i]);
      if (!function->type_param_names[i]) {
        pie_diag_error(
            sema->diag,
            "out of memory while storing function type parameter name");
        return 0;
      }
    }

    if (ast_function->type_param_constraints) {
      function->type_param_constraints =
          (char **)calloc(function->type_param_count, sizeof(char *));
      if (!function->type_param_constraints) {
        pie_diag_error(
            sema->diag,
            "out of memory while storing function type parameter constraints");
        return 0;
      }
      for (size_t i = 0; i < function->type_param_count; i++) {
        if (ast_function->type_param_constraints[i]) {
          function->type_param_constraints[i] =
              sema_strdup(ast_function->type_param_constraints[i]);
        }
      }
    }
  }

  return 1;
}

static int api_find_function(PieSema *sema, const char *name,
                             PieFunctionInfo *out_function) {
  SemaFunction *function = find_function_internal(sema, name);
  if (!function) {
    return 0;
  }
  out_function->return_type = function->return_type;
  out_function->param_types = function->param_types;
  out_function->param_count = function->param_count;
  out_function->is_unsafe = function->is_unsafe;
  out_function->type_param_names = (const char **)function->type_param_names;
  out_function->type_param_count = function->type_param_count;
  out_function->type_param_constraints =
      (const char **)function->type_param_constraints;
  return 1;
}

static const PieEnumDef *api_find_enum(PieSema *sema, const char *name) {
  if (!sema->program || !name) {
    return NULL;
  }
  for (size_t i = 0; i < sema->program->enum_count; i++) {
    if (strcmp(sema->program->enums[i].name, name) == 0) {
      return &sema->program->enums[i];
    }
  }
  return NULL;
}

static PieType api_current_return_type(PieSema *sema) {
  return sema->current_return_type;
}

static void api_error(PieSema *sema, const char *message) {
  pie_diag_error(sema->diag, message);
}

static void api_errorf(PieSema *sema, const char *fmt, ...) {
  char stack_buf[1024];
  va_list args;
  va_start(args, fmt);
  int needed = vsnprintf(stack_buf, sizeof(stack_buf), fmt, args);
  va_end(args);

  if (needed < 0) {
    pie_diag_error(sema->diag, "internal sema diagnostic formatting error");
    return;
  }
  if ((size_t)needed < sizeof(stack_buf)) {
    pie_diag_error(sema->diag, stack_buf);
    return;
  }

  char *heap_buf = (char *)malloc((size_t)needed + 1);
  if (!heap_buf) {
    pie_diag_error(sema->diag,
                   "out of memory while formatting sema diagnostic");
    return;
  }
  va_start(args, fmt);
  vsnprintf(heap_buf, (size_t)needed + 1, fmt, args);
  va_end(args);
  pie_diag_error(sema->diag, heap_buf);
  free(heap_buf);
}

static PieDiagnosticBag *api_diag(PieSema *sema) { return sema->diag; }

static const char *api_type_name(PieType type) {
  switch (type.kind) {
  case PIE_TYPE_ERROR:
    return "error";
  case PIE_TYPE_VOID:
    return "void";
  case PIE_TYPE_INT:
    if (type.type_width == PIE_WIDTH_8)
      return "int<8>";
    if (type.type_width == PIE_WIDTH_16)
      return "int<16>";
    if (type.type_width == PIE_WIDTH_32)
      return "int<32>";
    if (type.type_width == PIE_WIDTH_128)
      return "int<128>";
    if (type.type_width == PIE_WIDTH_WIDE)
      return "int<wide>";
    return "int";
  case PIE_TYPE_FLOAT:
    if (type.type_width == PIE_WIDTH_32)
      return "float<32>";
    if (type.type_width == PIE_WIDTH_128)
      return "float<128>";
    if (type.type_width == PIE_WIDTH_WIDE)
      return "float<wide>";
    return "float";
  case PIE_TYPE_CHAR:
    return "char";
  case PIE_TYPE_BYTE:
    return "byte";
  case PIE_TYPE_BOOL:
    return "bool";
  case PIE_TYPE_STRING:
    return "string";
  case PIE_TYPE_REF:
    return "&";
  case PIE_TYPE_REF_MUT:
    return "&mut";
  case PIE_TYPE_RAW_PTR:
    return "*raw";
  case PIE_TYPE_STRUCT:
    return type.struct_name ? type.struct_name : "struct";
  case PIE_TYPE_NULL:
    return "null";
  case PIE_TYPE_NULLABLE:
    return "?";
  case PIE_TYPE_TUPLE:
    return "tuple";
  case PIE_TYPE_LIST:
    return "list";
  case PIE_TYPE_MAP:
    return "map";
  case PIE_TYPE_ENUM:
    return type.enum_name ? type.enum_name : "enum";
  case PIE_TYPE_CLOSURE:
    return "closure";
  case PIE_TYPE_THREAD:
    return "thread";
  case PIE_TYPE_MUTEX:
    return "mutex";
  case PIE_TYPE_CHANNEL:
    return "channel";
  }
  return "unknown";
}

static PieSemaResult api_check_expr(PieSema *sema, const PieExpr *expr,
                                    PieType *out_type);
static PieSemaResult api_check_stmt(PieSema *sema, const PieStmt *stmt);
static PieSemaResult api_check_block(PieSema *sema, const PieProgram *program);
static int api_enter_scope(PieSema *sema);
static void api_leave_scope(PieSema *sema);
static void api_enter_loop(PieSema *sema);
static void api_leave_loop(PieSema *sema);
static int api_in_loop(PieSema *sema);
static void api_enter_unsafe(PieSema *sema);
static void api_leave_unsafe(PieSema *sema);
static int api_in_unsafe(PieSema *sema);

static void api_set_return_type(PieSema *sema, PieType type) {
  sema->current_return_type = type;
}

static const PieProgram *api_program(PieSema *sema) { return sema->program; }

static const PieStructDef *api_find_struct(PieSema *sema, const char *name) {
  if (!sema->program)
    return NULL;
  for (size_t i = 0; i < sema->program->struct_count; i++) {
    if (strcmp(sema->program->structs[i].name, name) == 0) {
      return &sema->program->structs[i];
    }
  }
  return NULL;
}

static int api_push_pending_mono(PieSema *sema, PieFunction func);

static int api_register_mono_func(PieSema *sema, const char *name,
                                  PieType return_type, PieType *param_types,
                                  size_t param_count) {
  return sema_register_mono_func(sema, name, return_type, param_types,
                                 param_count);
}

static const PieSemaApi PIE_SEMA_API = {
    .declare_symbol = api_declare_symbol,
    .find_symbol = api_find_symbol,
    .find_function = api_find_function,
    .find_struct = api_find_struct,
    .find_enum = api_find_enum,
    .current_return_type = api_current_return_type,
    .set_return_type = api_set_return_type,
    .check_expr = api_check_expr,
    .check_stmt = api_check_stmt,
    .check_block = api_check_block,
    .enter_scope = api_enter_scope,
    .leave_scope = api_leave_scope,
    .enter_loop = api_enter_loop,
    .leave_loop = api_leave_loop,
    .in_loop = api_in_loop,
    .enter_unsafe = api_enter_unsafe,
    .leave_unsafe = api_leave_unsafe,
    .in_unsafe = api_in_unsafe,
    .error = api_error,
    .errorf = api_errorf,
    .diag = api_diag,
    .type_name = api_type_name,
    .program = api_program,
    .push_pending_mono = api_push_pending_mono,
    .register_mono_func = api_register_mono_func,
};

static PieSemaContext make_context(PieSema *sema) {
  PieSemaContext ctx;
  ctx.sema = sema;
  ctx.api = &PIE_SEMA_API;
  return ctx;
}

static PieSemaResult api_check_expr(PieSema *sema, const PieExpr *expr,
                                    PieType *out_type) {
  const PieSemaHookRegistry *registry = pie_sema_hook_registry();
  PieSemaContext ctx = make_context(sema);

  for (size_t i = 0; i < registry->expr_hook_count; i++) {
    PieSemaResult result = registry->expr_hooks[i].check(&ctx, expr, out_type);
    if (result == PIE_SEMA_OK || result == PIE_SEMA_ERROR) {
      return result;
    }
  }

  api_error(sema, "no sema hook matched expression");
  out_type->kind = PIE_TYPE_ERROR;
  return PIE_SEMA_ERROR;
}

static PieSemaResult api_check_stmt(PieSema *sema, const PieStmt *stmt) {
  const PieSemaHookRegistry *registry = pie_sema_hook_registry();
  PieSemaContext ctx = make_context(sema);

  for (size_t i = 0; i < registry->stmt_hook_count; i++) {
    PieSemaResult result = registry->stmt_hooks[i].check(&ctx, stmt);
    if (result == PIE_SEMA_OK || result == PIE_SEMA_ERROR) {
      return result;
    }
  }

  api_error(sema, "no sema hook matched statement");
  return PIE_SEMA_ERROR;
}

static int api_enter_scope(PieSema *sema) {
  if (sema->scope_count == sema->scope_capacity) {
    size_t next_capacity = sema->scope_capacity ? sema->scope_capacity * 2 : 16;
    size_t *next =
        (size_t *)realloc(sema->scope_marks, next_capacity * sizeof(size_t));
    if (!next) {
      pie_diag_error(sema->diag, "out of memory while entering semantic scope");
      return 0;
    }
    sema->scope_marks = next;
    sema->scope_capacity = next_capacity;
  }
  sema->scope_marks[sema->scope_count++] = sema->symbol_count;
  return 1;
}

static void api_leave_scope(PieSema *sema) {
  if (sema->scope_count == 0) {
    return;
  }
  size_t mark = sema->scope_marks[--sema->scope_count];
  for (size_t i = mark; i < sema->symbol_count; i++) {
    free(sema->symbols[i].name);
    sema->symbols[i].name = NULL;
    if (sema->symbols[i].type.kind == PIE_TYPE_STRUCT) {
      free(sema->symbols[i].type.struct_name);
      sema->symbols[i].type.struct_name = NULL;
    }
    if (sema->symbols[i].type.kind == PIE_TYPE_ENUM) {
      free(sema->symbols[i].type.enum_name);
      sema->symbols[i].type.enum_name = NULL;
    }
  }
  sema->symbol_count = mark;
}

static PieSemaResult api_check_block(PieSema *sema, const PieProgram *program) {
  if (!program) {
    return PIE_SEMA_OK;
  }
  if (!api_enter_scope(sema)) {
    return PIE_SEMA_ERROR;
  }

  PieSemaResult result = PIE_SEMA_OK;
  for (size_t i = 0; i < program->stmt_count; i++) {
    if (api_check_stmt(sema, &program->stmts[i]) != PIE_SEMA_OK) {
      result = PIE_SEMA_ERROR;
      break;
    }
  }

  api_leave_scope(sema);
  return result;
}

static void api_enter_loop(PieSema *sema) { sema->loop_depth++; }

static void api_leave_loop(PieSema *sema) {
  if (sema->loop_depth > 0) {
    sema->loop_depth--;
  }
}

static int api_in_loop(PieSema *sema) { return sema->loop_depth > 0; }

static void api_enter_unsafe(PieSema *sema) { sema->unsafe_depth++; }

static void api_leave_unsafe(PieSema *sema) {
  if (sema->unsafe_depth > 0) {
    sema->unsafe_depth--;
  }
}

static int api_in_unsafe(PieSema *sema) { return sema->unsafe_depth > 0; }

static int api_push_pending_mono(PieSema *sema, PieFunction func) {
  return sema_push_pending_mono(sema, func);
}

int pie_sema_program(const PieProgram *program, PieDiagnosticBag *diag) {
  PieSema sema;
  memset(&sema, 0, sizeof(sema));
  sema.diag = diag;
  sema.current_return_type = type_from_ast(program->main_return_type);
  sema.program = program;

  int ok = 1;
  for (size_t i = 0; i < program->function_count; i++) {
    const PieFunction *function = &program->functions[i];
    if (!declare_function(&sema, function)) {
      ok = 0;
      break;
    }
  }

  if (ok) {
    sema.current_return_type.kind = PIE_TYPE_INT;
    for (size_t i = 0; ok && i < program->stmt_count; i++) {
      if (api_check_stmt(&sema, &program->stmts[i]) != PIE_SEMA_OK) {
        ok = 0;
      }
    }
  }

  for (size_t i = 0; ok && i < program->function_count; i++) {
    const PieFunction *function = &program->functions[i];

    if (function->type_param_count > 0) {
      continue;
    }

    sema.current_return_type = type_from_ast(function->return_type);
    if (!api_enter_scope(&sema)) {
      ok = 0;
      break;
    }
    if (function->is_unsafe) {
      api_enter_unsafe(&sema);
    }
    for (size_t j = 0; j < function->param_count; j++) {
      if (!api_declare_symbol(&sema, function->param_names[j],
                              type_from_ast(function->param_types[j]), 0)) {
        ok = 0;
        break;
      }
    }
    if (ok) {
      for (size_t j = 0; j < function->body->stmt_count; j++) {
        if (api_check_stmt(&sema, &function->body->stmts[j]) != PIE_SEMA_OK) {
          ok = 0;
          break;
        }
      }
    }
    if (function->is_unsafe) {
      api_leave_unsafe(&sema);
    }
    api_leave_scope(&sema);
  }

  if (ok) {
    sema_flush_pending_mono(&sema);
  }

  sema_free(&sema);
  return ok && !diag->has_error;
}
