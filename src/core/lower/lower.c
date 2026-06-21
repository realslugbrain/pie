#include "pie/core/lower/lower.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct LowerBinding {
  char *name;
  size_t local_id;
  int is_mut;
  PieIrTypeKind type;
  int type_width;
  PieIrTypeKind raw_pointee_type;
  int raw_pointee_width;
  PieIrTypeKind ref_inner_type;
  int ref_inner_width;
  char *struct_name;
  char *enum_name;
} LowerBinding;

typedef struct LowerFunction {
  char *name;
  PieIrTypeKind return_type;
  int return_type_width;
  PieIrTypeKind return_raw_pointee_type;
  int return_raw_pointee_width;
  PieIrTypeKind return_ref_inner_type;
  int return_ref_inner_width;
  PieIrTypeKind *param_types;
  int *param_type_widths;
  PieIrTypeKind *param_raw_pointee_types;
  int *param_raw_pointee_widths;
  size_t param_count;
  char **param_names;
  char **type_param_names;
  size_t type_param_count;
} LowerFunction;

struct PieLower {
  PieIrProgram *root_ir;
  PieIrProgram *current_ir;
  LowerBinding *bindings;
  size_t binding_count;
  size_t binding_capacity;
  size_t *scope_marks;
  size_t scope_count;
  size_t scope_capacity;
  LowerFunction *functions;
  size_t function_count;
  size_t function_capacity;
  int owns_functions;
  PieIrTypeKind current_return_type;
  PieDiagnosticBag *diag;
  const PieProgram *program;
  const PieProgram *current_body;
  PieIrProgram *parent_ir;
  char **closure_capture_names;
  PieIrTypeKind *closure_capture_types;
  size_t closure_capture_count;
};

static char *lower_strdup(const char *s) {
  size_t len = strlen(s);
  char *copy = (char *)malloc(len + 1);
  if (!copy) {
    return NULL;
  }
  memcpy(copy, s, len + 1);
  return copy;
}

static PieIrTypeKind ir_type_from_ast(PieAstTypeKind ast_type) {
  switch (ast_type) {
  case PIE_AST_TYPE_VOID:
    return PIE_IR_TYPE_VOID;
  case PIE_AST_TYPE_INT:
    return PIE_IR_TYPE_INT;
  case PIE_AST_TYPE_FLOAT:
    return PIE_IR_TYPE_FLOAT;
  case PIE_AST_TYPE_CHAR:
    return PIE_IR_TYPE_CHAR;
  case PIE_AST_TYPE_BYTE:
    return PIE_IR_TYPE_BYTE;
  case PIE_AST_TYPE_BOOL:
    return PIE_IR_TYPE_BOOL;
  case PIE_AST_TYPE_STRING:
    return PIE_IR_TYPE_STRING;
  case PIE_AST_TYPE_REF:
    return PIE_IR_TYPE_REF;
  case PIE_AST_TYPE_REF_MUT:
    return PIE_IR_TYPE_REF_MUT;
  case PIE_AST_TYPE_RAW_PTR:
    return PIE_IR_TYPE_RAW_PTR;
  case PIE_AST_TYPE_STRUCT:
    return PIE_IR_TYPE_STRUCT;
  case PIE_AST_TYPE_NULLABLE:
    return PIE_IR_TYPE_NULLABLE;
  case PIE_AST_TYPE_TUPLE:
    return PIE_IR_TYPE_TUPLE;
  case PIE_AST_TYPE_LIST:
    return PIE_IR_TYPE_LIST;
  case PIE_AST_TYPE_MAP:
    return PIE_IR_TYPE_MAP;
  case PIE_AST_TYPE_ENUM:
    return PIE_IR_TYPE_ENUM;
  case PIE_AST_TYPE_CLOSURE:
    return PIE_IR_TYPE_CLOSURE;
  case PIE_AST_TYPE_THREAD:
    return PIE_IR_TYPE_THREAD;
  case PIE_AST_TYPE_MUTEX:
    return PIE_IR_TYPE_MUTEX;
  case PIE_AST_TYPE_CHANNEL:
    return PIE_IR_TYPE_CHANNEL;
  case PIE_AST_TYPE_INFER:
    return PIE_IR_TYPE_UNKNOWN;
  }
  return PIE_IR_TYPE_UNKNOWN;
}

static int ir_type_width_from_ast(PieAstType ast_type) {
  return ast_type.width;
}

static void lower_free(PieLower *lower) {
  for (size_t i = 0; i < lower->binding_count; i++) {
    free(lower->bindings[i].name);
    free(lower->bindings[i].struct_name);
    free(lower->bindings[i].enum_name);
  }
  if (lower->owns_functions) {
    for (size_t i = 0; i < lower->function_count; i++) {
      free(lower->functions[i].name);
      free(lower->functions[i].param_types);
      free(lower->functions[i].param_type_widths);
      free(lower->functions[i].param_raw_pointee_types);
      free(lower->functions[i].param_raw_pointee_widths);
    }
    free(lower->functions);
  }
  free(lower->bindings);
  free(lower->scope_marks);
  lower->bindings = NULL;
  lower->scope_marks = NULL;
  lower->functions = NULL;
  lower->binding_count = 0;
  lower->binding_capacity = 0;
  lower->scope_count = 0;
  lower->scope_capacity = 0;
  lower->function_count = 0;
  lower->function_capacity = 0;
  lower->owns_functions = 0;
}

static size_t current_scope_start(PieLower *lower) {
  if (lower->scope_count == 0) {
    return 0;
  }
  return lower->scope_marks[lower->scope_count - 1];
}

static LowerBinding *find_binding_internal(PieLower *lower, const char *name) {
  for (size_t i = lower->binding_count; i > 0; i--) {
    LowerBinding *binding = &lower->bindings[i - 1];
    if (strcmp(binding->name, name) == 0) {
      return binding;
    }
  }
  return NULL;
}

static LowerBinding *find_binding_in_current_scope(PieLower *lower,
                                                   const char *name) {
  size_t start = current_scope_start(lower);
  for (size_t i = lower->binding_count; i > start; i--) {
    LowerBinding *binding = &lower->bindings[i - 1];
    if (strcmp(binding->name, name) == 0) {
      return binding;
    }
  }
  return NULL;
}

static int lower_bind_local(PieLower *lower, const char *name, size_t local_id,
                            int is_mut, PieIrTypeKind type, int type_width,
                            PieIrTypeKind raw_pointee_type,
                            int raw_pointee_width, PieIrTypeKind ref_inner_type,
                            int ref_inner_width, const char *struct_name,
                            const char *enum_name) {
  if (find_binding_in_current_scope(lower, name)) {
    pie_diag_errorf(lower->diag,
                    "local '%s' is already declared during lowering", name);
    return 0;
  }

  if (lower->binding_count == lower->binding_capacity) {
    size_t next_capacity =
        lower->binding_capacity ? lower->binding_capacity * 2 : 16;
    LowerBinding *next = (LowerBinding *)realloc(
        lower->bindings, next_capacity * sizeof(LowerBinding));
    if (!next) {
      pie_diag_error(lower->diag,
                     "out of memory while storing lowered local binding");
      return 0;
    }
    lower->bindings = next;
    lower->binding_capacity = next_capacity;
  }

  LowerBinding *binding = &lower->bindings[lower->binding_count++];
  memset(binding, 0, sizeof(*binding));
  binding->name = lower_strdup(name);
  if (!binding->name) {
    pie_diag_error(lower->diag,
                   "out of memory while storing lowered local binding name");
    lower->binding_count--;
    return 0;
  }
  binding->local_id = local_id;
  binding->is_mut = is_mut;
  binding->type = type;
  binding->type_width = type_width;
  binding->raw_pointee_type = raw_pointee_type;
  binding->raw_pointee_width = raw_pointee_width;
  binding->ref_inner_type = ref_inner_type;
  binding->ref_inner_width = ref_inner_width;
  binding->struct_name = struct_name ? lower_strdup(struct_name) : NULL;
  binding->enum_name = enum_name ? lower_strdup(enum_name) : NULL;
  return 1;
}

static LowerFunction *find_function_internal(PieLower *lower,
                                             const char *name) {
  for (size_t i = 0; i < lower->function_count; i++) {
    if (strcmp(lower->functions[i].name, name) == 0) {
      return &lower->functions[i];
    }
  }
  return NULL;
}

static int lower_declare_function(PieLower *lower,
                                  const PieFunction *function) {
  if (find_function_internal(lower, function->name)) {
    pie_diag_errorf(lower->diag,
                    "function '%s' is already declared during lowering",
                    function->name);
    return 0;
  }

  if (lower->function_count == lower->function_capacity) {
    size_t next_capacity =
        lower->function_capacity ? lower->function_capacity * 2 : 8;
    LowerFunction *next = (LowerFunction *)realloc(
        lower->functions, next_capacity * sizeof(LowerFunction));
    if (!next) {
      pie_diag_error(
          lower->diag,
          "out of memory while storing lowered function declaration");
      return 0;
    }
    lower->functions = next;
    lower->function_capacity = next_capacity;
  }

  LowerFunction *lower_function = &lower->functions[lower->function_count++];
  memset(lower_function, 0, sizeof(*lower_function));
  lower_function->name = lower_strdup(function->name);
  if (!lower_function->name) {
    pie_diag_error(lower->diag,
                   "out of memory while storing lowered function name");
    lower->function_count--;
    return 0;
  }
  lower_function->return_type = ir_type_from_ast(function->return_type.kind);
  lower_function->return_type_width =
      ir_type_width_from_ast(function->return_type);
  lower_function->return_raw_pointee_type =
      ir_type_from_ast(function->return_type.raw_pointee_kind);
  lower_function->return_raw_pointee_width =
      function->return_type.raw_pointee_width;
  if (function->return_type.kind == PIE_AST_TYPE_REF ||
      function->return_type.kind == PIE_AST_TYPE_REF_MUT) {
    lower_function->return_ref_inner_type =
        ir_type_from_ast(function->return_type.ref_inner_kind);
    lower_function->return_ref_inner_width =
        function->return_type.ref_inner_width;
  } else {
    lower_function->return_ref_inner_type = PIE_IR_TYPE_UNKNOWN;
    lower_function->return_ref_inner_width = PIE_WIDTH_INFER;
  }
  lower_function->param_count = function->param_count;
  if (function->param_count) {
    lower_function->param_types =
        (PieIrTypeKind *)calloc(function->param_count, sizeof(PieIrTypeKind));
    lower_function->param_type_widths =
        (int *)calloc(function->param_count, sizeof(int));
    lower_function->param_raw_pointee_types =
        (PieIrTypeKind *)calloc(function->param_count, sizeof(PieIrTypeKind));
    lower_function->param_raw_pointee_widths =
        (int *)calloc(function->param_count, sizeof(int));
    if (!lower_function->param_types || !lower_function->param_type_widths ||
        !lower_function->param_raw_pointee_types ||
        !lower_function->param_raw_pointee_widths) {
      free(lower_function->name);
      free(lower_function->param_types);
      free(lower_function->param_type_widths);
      free(lower_function->param_raw_pointee_types);
      free(lower_function->param_raw_pointee_widths);
      memset(lower_function, 0, sizeof(*lower_function));
      pie_diag_error(lower->diag,
                     "out of memory while storing lowered function parameters");
      return 0;
    }
    for (size_t i = 0; i < function->param_count; i++) {
      lower_function->param_types[i] =
          ir_type_from_ast(function->param_types[i].kind);
      lower_function->param_type_widths[i] =
          ir_type_width_from_ast(function->param_types[i]);
      lower_function->param_raw_pointee_types[i] =
          ir_type_from_ast(function->param_types[i].raw_pointee_kind);
      lower_function->param_raw_pointee_widths[i] =
          function->param_types[i].raw_pointee_width;
    }
  }

  if (function->param_count && function->param_names) {
    lower_function->param_names =
        (char **)calloc(function->param_count, sizeof(char *));
    if (lower_function->param_names) {
      for (size_t i = 0; i < function->param_count; i++) {
        lower_function->param_names[i] = lower_strdup(function->param_names[i]);
      }
    }
  }

  lower_function->type_param_count = function->type_param_count;
  if (function->type_param_count && function->type_params) {
    lower_function->type_param_names =
        (char **)calloc(function->type_param_count, sizeof(char *));
    if (lower_function->type_param_names) {
      for (size_t i = 0; i < function->type_param_count; i++) {
        lower_function->type_param_names[i] =
            lower_strdup(function->type_params[i]);
      }
    }
  }

  return 1;
}

static PieIrProgram *api_ir(PieLower *lower) { return lower->current_ir; }

static int api_declare_local(PieLower *lower, const char *name, int is_mut,
                             PieIrTypeKind type, int type_width,
                             PieIrTypeKind raw_pointee_type,
                             int raw_pointee_width,
                             PieIrTypeKind ref_inner_type, int ref_inner_width,
                             const char *struct_name, const char *enum_name,
                             size_t *out_id) {
  if (find_binding_in_current_scope(lower, name)) {
    pie_diag_errorf(lower->diag,
                    "local '%s' is already declared during lowering", name);
    return 0;
  }
  if (type == PIE_IR_TYPE_NULLABLE) {
    if (!pie_ir_program_add_nullable_local(lower->root_ir, name, is_mut,
                                           PIE_IR_TYPE_UNKNOWN, 0, out_id)) {
      pie_diag_error(lower->diag, "out of memory while lowering local");
      return 0;
    }
  } else {
    if (!pie_ir_program_add_raw_typed_local(lower->root_ir, name, is_mut, type,
                                            type_width, raw_pointee_type,
                                            raw_pointee_width, out_id)) {
      pie_diag_error(lower->diag, "out of memory while lowering local");
      return 0;
    }
  }

  if (struct_name) {
    lower->root_ir->locals[*out_id].struct_name = lower_strdup(struct_name);
  }
  if (enum_name) {
    lower->root_ir->locals[*out_id].enum_name = lower_strdup(enum_name);
  }

  return lower_bind_local(lower, name, *out_id, is_mut, type, type_width,
                          raw_pointee_type, raw_pointee_width, ref_inner_type,
                          ref_inner_width, struct_name, enum_name);
}

static int
api_find_local(PieLower *lower, const char *name, size_t *out_id,
               int *out_is_mut, PieIrTypeKind *out_type, int *out_type_width,
               PieIrTypeKind *out_raw_pointee_type, int *out_raw_pointee_width,
               PieIrTypeKind *out_ref_inner_type, int *out_ref_inner_width,
               const char **out_struct_name, const char **out_enum_name) {
  LowerBinding *binding = find_binding_internal(lower, name);
  if (!binding) {
    return 0;
  }
  *out_id = binding->local_id;
  if (out_is_mut) {
    *out_is_mut = binding->is_mut;
  }
  if (out_type) {
    *out_type = binding->type;
  }
  if (out_type_width) {
    *out_type_width = binding->type_width;
  }
  if (out_raw_pointee_type) {
    *out_raw_pointee_type = binding->raw_pointee_type;
  }
  if (out_raw_pointee_width) {
    *out_raw_pointee_width = binding->raw_pointee_width;
  }
  if (out_ref_inner_type) {
    *out_ref_inner_type = binding->ref_inner_type;
  }
  if (out_ref_inner_width) {
    *out_ref_inner_width = binding->ref_inner_width;
  }
  if (out_struct_name) {
    *out_struct_name = binding->struct_name;
  }
  if (out_enum_name) {
    *out_enum_name = binding->enum_name;
  }
  return 1;
}

static int api_find_function(PieLower *lower, const char *name,
                             PieLowerFunctionInfo *out_function) {
  LowerFunction *function = find_function_internal(lower, name);
  if (!function) {
    return 0;
  }
  out_function->return_type = function->return_type;
  out_function->return_type_width = function->return_type_width;
  out_function->return_raw_pointee_type = function->return_raw_pointee_type;
  out_function->return_raw_pointee_width = function->return_raw_pointee_width;
  out_function->return_ref_inner_type = function->return_ref_inner_type;
  out_function->return_ref_inner_width = function->return_ref_inner_width;
  out_function->param_types = function->param_types;
  out_function->param_type_widths = function->param_type_widths;
  out_function->param_raw_pointee_types = function->param_raw_pointee_types;
  out_function->param_raw_pointee_widths = function->param_raw_pointee_widths;
  out_function->param_count = function->param_count;
  out_function->param_names = (const char **)function->param_names;
  out_function->type_param_names = (const char **)function->type_param_names;
  out_function->type_param_count = function->type_param_count;
  return 1;
}

static PieIrTypeKind api_current_return_type(PieLower *lower) {
  return lower->current_return_type;
}

static int api_push_stmt(PieLower *lower, PieIrStmt stmt) {
  if (!pie_ir_program_push_stmt(lower->current_ir, stmt)) {
    pie_diag_error(lower->diag, "out of memory while lowering statement");
    return 0;
  }
  return 1;
}

static void api_error(PieLower *lower, const char *message) {
  pie_diag_error(lower->diag, message);
}

static void api_errorf(PieLower *lower, const char *fmt, ...) {
  char stack_buf[1024];
  va_list args;
  va_start(args, fmt);
  int needed = vsnprintf(stack_buf, sizeof(stack_buf), fmt, args);
  va_end(args);

  if (needed < 0) {
    pie_diag_error(lower->diag, "internal lower diagnostic formatting error");
    return;
  }
  if ((size_t)needed < sizeof(stack_buf)) {
    pie_diag_error(lower->diag, stack_buf);
    return;
  }

  char *heap_buf = (char *)malloc((size_t)needed + 1);
  if (!heap_buf) {
    pie_diag_error(lower->diag,
                   "out of memory while formatting lower diagnostic");
    return;
  }
  va_start(args, fmt);
  vsnprintf(heap_buf, (size_t)needed + 1, fmt, args);
  va_end(args);
  pie_diag_error(lower->diag, heap_buf);
  free(heap_buf);
}

static PieDiagnosticBag *api_diag(PieLower *lower) { return lower->diag; }

static PieLowerResult api_lower_expr(PieLower *lower, const PieExpr *expr,
                                     PieIrExpr **out_expr);
static PieLowerResult api_lower_stmt(PieLower *lower, const PieStmt *stmt);
static int api_lower_block(PieLower *lower, const PieProgram *program,
                           PieIrProgram *out_ir);
static int api_enter_scope(PieLower *lower);
static void api_leave_scope(PieLower *lower);

static int api_find_variant_tag(PieLower *lower, const char *enum_name,
                                const char *variant_name, int *out_tag) {
  if (!enum_name || !variant_name) {
    return 0;
  }
  for (int attempt = 0; attempt < 2; attempt++) {
    const PieProgram *prog =
        (attempt == 0) ? lower->current_body : lower->program;
    if (!prog) {
      continue;
    }
    for (size_t e = 0; e < prog->enum_count; e++) {
      const PieEnumDef *def = &prog->enums[e];
      if (strcmp(def->name, enum_name) == 0) {
        for (size_t v = 0; v < def->variant_count; v++) {
          if (strcmp(def->variants[v].name, variant_name) == 0) {
            *out_tag = (int)v;
            return 1;
          }
        }
        return 0;
      }
    }
  }
  return 0;
}

static int api_find_capture(PieLower *lower, const char *name,
                            PieIrTypeKind *out_type, int *out_type_width,
                            size_t *out_env_offset) {
  if (!lower->closure_capture_names || !name)
    return 0;
  for (size_t i = 0; i < lower->closure_capture_count; i++) {
    if (lower->closure_capture_names[i] &&
        strcmp(lower->closure_capture_names[i], name) == 0) {
      *out_type = lower->closure_capture_types[i];
      *out_type_width = PIE_WIDTH_INFER;
      *out_env_offset = i * 8;
      return 1;
    }
  }
  return 0;
}

static const PieProgram *api_program(PieLower *lower) {
  return lower->current_body ? lower->current_body : lower->program;
}

static void api_set_closure_captures(PieLower *lower, char **names,
                                     PieIrTypeKind *types, size_t count) {
  for (size_t i = 0; i < lower->closure_capture_count; i++) {
    free(lower->closure_capture_names[i]);
  }
  free(lower->closure_capture_names);
  free(lower->closure_capture_types);

  lower->closure_capture_count = count;
  if (count == 0) {
    lower->closure_capture_names = NULL;
    lower->closure_capture_types = NULL;
    return;
  }

  lower->closure_capture_names = (char **)calloc(count, sizeof(char *));
  lower->closure_capture_types =
      (PieIrTypeKind *)calloc(count, sizeof(PieIrTypeKind));
  for (size_t i = 0; i < count; i++) {
    size_t len = strlen(names[i]);
    lower->closure_capture_names[i] = (char *)malloc(len + 1);
    if (lower->closure_capture_names[i]) {
      memcpy(lower->closure_capture_names[i], names[i], len + 1);
    }
    lower->closure_capture_types[i] = types[i];
  }
}

static int api_declare_capture(PieLower *lower, const char *name, int is_mut,
                               PieIrTypeKind type, int type_width,
                               size_t local_id) {
  if (find_binding_in_current_scope(lower, name)) {
    pie_diag_errorf(lower->diag,
                    "capture '%s' is already declared during lowering", name);
    return 0;
  }
  return lower_bind_local(lower, name, local_id, is_mut, type, type_width,
                          PIE_IR_TYPE_UNKNOWN, PIE_WIDTH_INFER,
                          PIE_IR_TYPE_UNKNOWN, PIE_WIDTH_INFER, NULL, NULL);
}

static const PieEnumDef *api_find_enum(PieLower *lower, const char *name) {
  if (!name)
    return NULL;
  for (int attempt = 0; attempt < 2; attempt++) {
    const PieProgram *prog =
        (attempt == 0) ? lower->current_body : lower->program;
    if (!prog)
      continue;
    for (size_t i = 0; i < prog->enum_count; i++) {
      if (strcmp(prog->enums[i].name, name) == 0) {
        return &prog->enums[i];
      }
    }
  }
  return NULL;
}

static const PieStructDef *api_find_struct(PieLower *lower, const char *name) {
  if (!name)
    return NULL;
  PieIrProgram *ir = lower->root_ir;
  if (ir) {
    for (size_t i = 0; i < ir->struct_count; i++) {
      if (strcmp(ir->structs[i].name, name) == 0) {
        return &ir->structs[i];
      }
    }
  }
  if (lower->parent_ir) {
    for (size_t i = 0; i < lower->parent_ir->struct_count; i++) {
      if (strcmp(lower->parent_ir->structs[i].name, name) == 0) {
        return &lower->parent_ir->structs[i];
      }
    }
  }
  return NULL;
}

static void api_set_current_ir(PieLower *lower, PieIrProgram *ir) {
  lower->current_ir = ir;
}

static PieIrProgram *api_current_ir(PieLower *lower) {
  return lower->current_ir;
}

static void api_set_current_body(PieLower *lower, const PieProgram *body) {
  lower->current_body = body;
}

static const PieProgram *api_current_body(PieLower *lower) {
  return lower->current_body;
}

static void api_set_root_ir(PieLower *lower, PieIrProgram *ir) {
  lower->root_ir = ir;
}

static PieIrProgram *api_root_ir(PieLower *lower) { return lower->root_ir; }

static const PieLowerApi PIE_LOWER_API = {
    .ir = api_ir,
    .declare_local = api_declare_local,
    .find_local = api_find_local,
    .find_function = api_find_function,
    .find_struct = api_find_struct,
    .find_enum = api_find_enum,
    .find_variant_tag = api_find_variant_tag,
    .program = api_program,
    .current_return_type = api_current_return_type,
    .push_stmt = api_push_stmt,
    .lower_expr = api_lower_expr,
    .lower_stmt = api_lower_stmt,
    .lower_block = api_lower_block,
    .enter_scope = api_enter_scope,
    .leave_scope = api_leave_scope,
    .error = api_error,
    .errorf = api_errorf,
    .diag = api_diag,
    .find_capture = api_find_capture,
    .set_closure_captures = api_set_closure_captures,
    .declare_capture = api_declare_capture,
    .set_current_ir = api_set_current_ir,
    .current_ir = api_current_ir,
    .set_root_ir = api_set_root_ir,
    .root_ir = api_root_ir,
    .set_current_body = api_set_current_body,
    .current_body = api_current_body};

static PieLowerContext make_context(PieLower *lower) {
  PieLowerContext ctx;
  ctx.lower = lower;
  ctx.api = &PIE_LOWER_API;
  return ctx;
}

static PieLowerResult api_lower_expr(PieLower *lower, const PieExpr *expr,
                                     PieIrExpr **out_expr) {
  const PieLowerHookRegistry *registry = pie_lower_hook_registry();
  PieLowerContext ctx = make_context(lower);
  *out_expr = NULL;

  for (size_t i = 0; i < registry->expr_hook_count; i++) {
    PieLowerResult result = registry->expr_hooks[i].lower(&ctx, expr, out_expr);
    if (result == PIE_LOWER_OK || result == PIE_LOWER_ERROR) {
      return result;
    }
  }

  api_error(lower, "no lower hook matched expression");
  return PIE_LOWER_ERROR;
}

static PieLowerResult api_lower_stmt(PieLower *lower, const PieStmt *stmt) {
  const PieLowerHookRegistry *registry = pie_lower_hook_registry();
  PieLowerContext ctx = make_context(lower);

  for (size_t i = 0; i < registry->stmt_hook_count; i++) {
    PieLowerResult result = registry->stmt_hooks[i].lower(&ctx, stmt);
    if (result == PIE_LOWER_OK || result == PIE_LOWER_ERROR) {
      return result;
    }
  }

  api_error(lower, "no lower hook matched statement");
  return PIE_LOWER_ERROR;
}

static int api_lower_block(PieLower *lower, const PieProgram *program,
                           PieIrProgram *out_ir) {
  pie_ir_program_init(out_ir);
  if (!api_enter_scope(lower)) {
    return 0;
  }

  PieIrProgram *previous = lower->current_ir;
  const PieProgram *prev_body = lower->current_body;
  lower->current_ir = out_ir;
  lower->current_body = program;

  int ok = 1;
  for (size_t i = 0; i < program->stmt_count; i++) {
    if (api_lower_stmt(lower, &program->stmts[i]) != PIE_LOWER_OK) {
      ok = 0;
      break;
    }
  }

  lower->current_ir = previous;
  lower->current_body = prev_body;
  api_leave_scope(lower);
  return ok && !lower->diag->has_error;
}

static int api_enter_scope(PieLower *lower) {
  if (lower->scope_count == lower->scope_capacity) {
    size_t next_capacity =
        lower->scope_capacity ? lower->scope_capacity * 2 : 16;
    size_t *next =
        (size_t *)realloc(lower->scope_marks, next_capacity * sizeof(size_t));
    if (!next) {
      pie_diag_error(lower->diag, "out of memory while entering lower scope");
      return 0;
    }
    lower->scope_marks = next;
    lower->scope_capacity = next_capacity;
  }
  lower->scope_marks[lower->scope_count++] = lower->binding_count;
  return 1;
}

static void api_leave_scope(PieLower *lower) {
  if (lower->scope_count == 0) {
    return;
  }
  size_t mark = lower->scope_marks[--lower->scope_count];
  for (size_t i = mark; i < lower->binding_count; i++) {
    free(lower->bindings[i].name);
    lower->bindings[i].name = NULL;
  }
  lower->binding_count = mark;
}

int pie_lower_program(const PieProgram *program, PieIrProgram *out_ir,
                      PieDiagnosticBag *diag) {
  PieLower lower;
  memset(&lower, 0, sizeof(lower));
  pie_ir_program_init(out_ir);
  lower.root_ir = out_ir;
  lower.current_ir = out_ir;
  lower.diag = diag;
  lower.owns_functions = 1;
  lower.current_return_type = ir_type_from_ast(program->main_return_type.kind);
  lower.program = program;

  int ok = 1;
  for (size_t i = 0; i < program->function_count; i++) {
    if (!lower_declare_function(&lower, &program->functions[i])) {
      ok = 0;
      break;
    }
  }

  for (size_t i = 0; ok && i < program->stmt_count; i++) {
    if (api_lower_stmt(&lower, &program->stmts[i]) != PIE_LOWER_OK) {
      ok = 0;
      break;
    }
  }

  for (size_t i = 0; i < program->function_count; i++) {
    if (!ok) {
      break;
    }
    const PieFunction *function = &program->functions[i];

    if (function->type_param_count > 0) {
      continue;
    }

    PieIrFunction ir_function;
    memset(&ir_function, 0, sizeof(ir_function));
    ir_function.name = lower_strdup(function->name);
    ir_function.return_type = ir_type_from_ast(function->return_type.kind);
    ir_function.return_type_width =
        ir_type_width_from_ast(function->return_type);
    ir_function.return_raw_pointee_type =
        ir_type_from_ast(function->return_type.raw_pointee_kind);
    ir_function.return_raw_pointee_width =
        function->return_type.raw_pointee_width;
    if (function->return_type.kind == PIE_AST_TYPE_REF ||
        function->return_type.kind == PIE_AST_TYPE_REF_MUT) {
      ir_function.return_ref_inner_type =
          ir_type_from_ast(function->return_type.ref_inner_kind);
      ir_function.return_ref_inner_width =
          function->return_type.ref_inner_width;
    } else {
      ir_function.return_ref_inner_type = PIE_IR_TYPE_UNKNOWN;
      ir_function.return_ref_inner_width = PIE_WIDTH_INFER;
    }
    ir_function.body = (PieIrProgram *)malloc(sizeof(PieIrProgram));
    ir_function.param_count = function->param_count;
    if (ir_function.param_count) {
      ir_function.param_names =
          (char **)calloc(ir_function.param_count, sizeof(char *));
      ir_function.param_types = (PieIrTypeKind *)calloc(ir_function.param_count,
                                                        sizeof(PieIrTypeKind));
      ir_function.param_type_widths =
          (int *)calloc(ir_function.param_count, sizeof(int));
      ir_function.param_raw_pointee_types = (PieIrTypeKind *)calloc(
          ir_function.param_count, sizeof(PieIrTypeKind));
      ir_function.param_raw_pointee_widths =
          (int *)calloc(ir_function.param_count, sizeof(int));
      ir_function.param_nullable_inner_types = (PieIrTypeKind *)calloc(
          ir_function.param_count, sizeof(PieIrTypeKind));
      ir_function.param_nullable_inner_widths =
          (int *)calloc(ir_function.param_count, sizeof(int));
      ir_function.param_ref_inner_types = (PieIrTypeKind *)calloc(
          ir_function.param_count, sizeof(PieIrTypeKind));
      ir_function.param_ref_inner_widths =
          (int *)calloc(ir_function.param_count, sizeof(int));
      ir_function.param_local_ids =
          (size_t *)calloc(ir_function.param_count, sizeof(size_t));
    }
    if (!ir_function.name || !ir_function.body ||
        (ir_function.param_count &&
         (!ir_function.param_names || !ir_function.param_types ||
          !ir_function.param_type_widths ||
          !ir_function.param_raw_pointee_types ||
          !ir_function.param_raw_pointee_widths ||
          !ir_function.param_nullable_inner_types ||
          !ir_function.param_nullable_inner_widths ||
          !ir_function.param_ref_inner_types ||
          !ir_function.param_ref_inner_widths ||
          !ir_function.param_local_ids))) {
      free(ir_function.name);
      free(ir_function.param_names);
      free(ir_function.param_types);
      free(ir_function.param_type_widths);
      free(ir_function.param_raw_pointee_types);
      free(ir_function.param_raw_pointee_widths);
      free(ir_function.param_nullable_inner_types);
      free(ir_function.param_nullable_inner_widths);
      free(ir_function.param_ref_inner_types);
      free(ir_function.param_ref_inner_widths);
      free(ir_function.param_local_ids);
      free(ir_function.body);
      pie_diag_error(diag, "out of memory while lowering function");
      ok = 0;
      break;
    }
    pie_ir_program_init(ir_function.body);
    PieLower function_lower;
    memset(&function_lower, 0, sizeof(function_lower));
    function_lower.root_ir = ir_function.body;
    function_lower.current_ir = ir_function.body;
    function_lower.diag = diag;
    function_lower.functions = lower.functions;
    function_lower.function_count = lower.function_count;
    function_lower.function_capacity = lower.function_capacity;
    function_lower.owns_functions = 0;
    function_lower.current_return_type = ir_function.return_type;
    function_lower.program = lower.program;
    function_lower.current_body = function->body;
    function_lower.parent_ir = out_ir;

    for (size_t j = 0; j < lower.binding_count; j++) {
      lower_bind_local(
          &function_lower, lower.bindings[j].name, lower.bindings[j].local_id,
          lower.bindings[j].is_mut, lower.bindings[j].type,
          lower.bindings[j].type_width, lower.bindings[j].raw_pointee_type,
          lower.bindings[j].raw_pointee_width, lower.bindings[j].ref_inner_type,
          lower.bindings[j].ref_inner_width, lower.bindings[j].struct_name,
          lower.bindings[j].enum_name);
    }

    if (!api_enter_scope(&function_lower)) {
      ok = 0;
      break;
    }

    for (size_t j = 0; j < ir_function.param_count; j++) {
      ir_function.param_names[j] = lower_strdup(function->param_names[j]);
      ir_function.param_types[j] =
          ir_type_from_ast(function->param_types[j].kind);
      ir_function.param_type_widths[j] =
          ir_type_width_from_ast(function->param_types[j]);
      ir_function.param_raw_pointee_types[j] =
          ir_type_from_ast(function->param_types[j].raw_pointee_kind);
      ir_function.param_raw_pointee_widths[j] =
          function->param_types[j].raw_pointee_width;
      ir_function.param_nullable_inner_types[j] =
          ir_type_from_ast(function->param_types[j].nullable_inner_kind);
      ir_function.param_nullable_inner_widths[j] =
          function->param_types[j].nullable_inner_width;
      if (function->param_types[j].kind == PIE_AST_TYPE_REF ||
          function->param_types[j].kind == PIE_AST_TYPE_REF_MUT) {
        ir_function.param_ref_inner_types[j] =
            ir_type_from_ast(function->param_types[j].ref_inner_kind);
        ir_function.param_ref_inner_widths[j] =
            function->param_types[j].ref_inner_width;
      } else {
        ir_function.param_ref_inner_types[j] = PIE_IR_TYPE_UNKNOWN;
        ir_function.param_ref_inner_widths[j] = PIE_WIDTH_INFER;
      }

      const char *struct_name =
          (function->param_types[j].kind == PIE_AST_TYPE_STRUCT)
              ? function->param_types[j].struct_name
              : NULL;
      if (!struct_name &&
          (function->param_types[j].kind == PIE_AST_TYPE_REF ||
           function->param_types[j].kind == PIE_AST_TYPE_REF_MUT)) {
        struct_name = function->param_types[j].ref_inner_struct_name;
      }
      const char *enum_name =
          (function->param_types[j].kind == PIE_AST_TYPE_ENUM)
              ? function->param_types[j].enum_name
              : NULL;

      if (!ir_function.param_names[j] ||
          !api_declare_local(&function_lower, function->param_names[j], 0,
                             ir_function.param_types[j],
                             ir_function.param_type_widths[j],
                             ir_function.param_raw_pointee_types[j],
                             ir_function.param_raw_pointee_widths[j],
                             ir_function.param_ref_inner_types[j],
                             ir_function.param_ref_inner_widths[j], struct_name,
                             enum_name, &ir_function.param_local_ids[j])) {
        pie_diag_error(diag, "out of memory while lowering function parameter");
        ok = 0;
        break;
      }
    }
    if (!ok) {
      for (size_t j = 0; j < ir_function.param_count; j++) {
        free(ir_function.param_names[j]);
      }
      free(ir_function.name);
      free(ir_function.param_names);
      free(ir_function.param_types);
      free(ir_function.param_type_widths);
      free(ir_function.param_raw_pointee_types);
      free(ir_function.param_raw_pointee_widths);
      free(ir_function.param_nullable_inner_types);
      free(ir_function.param_nullable_inner_widths);
      free(ir_function.param_ref_inner_types);
      free(ir_function.param_ref_inner_widths);
      free(ir_function.param_local_ids);
      pie_ir_program_free(ir_function.body);
      free(ir_function.body);
      lower_free(&function_lower);
      break;
    }

    for (size_t j = 0; j < function->body->stmt_count; j++) {
      if (api_lower_stmt(&function_lower, &function->body->stmts[j]) !=
          PIE_LOWER_OK) {
        ok = 0;
        break;
      }
    }
    if (!ok) {
      for (size_t j = 0; j < ir_function.param_count; j++) {
        free(ir_function.param_names[j]);
      }
      free(ir_function.name);
      free(ir_function.param_names);
      free(ir_function.param_types);
      free(ir_function.param_type_widths);
      free(ir_function.param_raw_pointee_types);
      free(ir_function.param_raw_pointee_widths);
      free(ir_function.param_nullable_inner_types);
      free(ir_function.param_nullable_inner_widths);
      free(ir_function.param_ref_inner_types);
      free(ir_function.param_ref_inner_widths);
      free(ir_function.param_local_ids);
      pie_ir_program_free(ir_function.body);
      free(ir_function.body);
      lower_free(&function_lower);
      break;
    }
    if (!pie_ir_program_push_function(out_ir, ir_function)) {
      for (size_t j = 0; j < ir_function.param_count; j++) {
        free(ir_function.param_names[j]);
      }
      free(ir_function.name);
      free(ir_function.param_names);
      free(ir_function.param_types);
      free(ir_function.param_type_widths);
      free(ir_function.param_raw_pointee_types);
      free(ir_function.param_raw_pointee_widths);
      free(ir_function.param_nullable_inner_types);
      free(ir_function.param_nullable_inner_widths);
      free(ir_function.param_ref_inner_types);
      free(ir_function.param_ref_inner_widths);
      free(ir_function.param_local_ids);
      pie_ir_program_free(ir_function.body);
      free(ir_function.body);
      pie_diag_error(diag, "out of memory while storing lowered function");
      ok = 0;
      lower_free(&function_lower);
      break;
    }
    lower_free(&function_lower);
  }

  lower_free(&lower);
  return ok && !diag->has_error;
}
