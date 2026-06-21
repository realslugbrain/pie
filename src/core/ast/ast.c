#include "pie/core/ast/ast.h"

#include <stdlib.h>
#include <string.h>

static char *pie_ast_strdup(const char *s) {
  size_t len = strlen(s);
  char *copy = (char *)malloc(len + 1);
  if (!copy) {
    return NULL;
  }
  memcpy(copy, s, len + 1);
  return copy;
}

void pie_program_init(PieProgram *program) {
  program->main_return_type = pie_ast_type_simple(PIE_AST_TYPE_INT);
  program->has_main = 0;
  program->stmts = NULL;
  program->stmt_count = 0;
  program->stmt_capacity = 0;
  program->functions = NULL;
  program->function_count = 0;
  program->function_capacity = 0;
  program->structs = NULL;
  program->struct_count = 0;
  program->struct_capacity = 0;
  program->requires = NULL;
  program->require_count = 0;
  program->require_capacity = 0;
  program->enums = NULL;
  program->enum_count = 0;
  program->enum_capacity = 0;
  program->consts = NULL;
  program->const_count = 0;
  program->const_capacity = 0;
  program->owned_modules = NULL;
  program->owned_module_count = 0;
  program->owned_module_capacity = 0;
}

void pie_program_free(PieProgram *program) {
  for (size_t i = 0; i < program->stmt_count; i++) {
    PieStmt *stmt = &program->stmts[i];
    free(stmt->name);
    pie_expr_free(stmt->target);
    pie_expr_free(stmt->expr);
    for (size_t j = 0; j < stmt->arg_count; j++) {
      free(stmt->args[j].text);
      pie_expr_free(stmt->args[j].expr);
    }
    free(stmt->args);
    for (size_t j = 0; j < stmt->multi_count; j++) {
      free(stmt->multi_names[j]);
      pie_expr_free(stmt->multi_exprs[j]);
    }
    free(stmt->multi_names);
    free(stmt->multi_exprs);

    free(stmt->type_annotation.struct_name);

    if (stmt->struct_def) {
      free(stmt->struct_def->name);
      for (size_t j = 0; j < stmt->struct_def->field_count; j++) {
        free(stmt->struct_def->fields[j].name);
        free(stmt->struct_def->fields[j].type.struct_name);
      }
      free(stmt->struct_def->fields);
      free(stmt->struct_def);
    }

    if (stmt->enum_def) {
      free(stmt->enum_def->name);
      for (size_t j = 0; j < stmt->enum_def->variant_count; j++) {
        free(stmt->enum_def->variants[j].name);
        free(stmt->enum_def->variants[j].payload_kinds);
        free(stmt->enum_def->variants[j].payload_widths);
      }
      free(stmt->enum_def);
    }

    pie_expr_free(stmt->field_target);
    pie_expr_free(stmt->index_target);
    pie_expr_free(stmt->index_expr);
    if (stmt->then_branch) {
      pie_program_free(stmt->then_branch);
      free(stmt->then_branch);
    }
    if (stmt->else_branch) {
      pie_program_free(stmt->else_branch);
      free(stmt->else_branch);
    }
    free(stmt->for_var_name);
    pie_expr_free(stmt->for_start);
    pie_expr_free(stmt->for_end);
    for (size_t j = 0; j < stmt->match_case_count; j++) {
      free(stmt->match_case_names[j]);
      if (stmt->match_case_bodies && stmt->match_case_bodies[j]) {
        pie_program_free(stmt->match_case_bodies[j]);
        free(stmt->match_case_bodies[j]);
      }
      if (stmt->match_case_bindings && stmt->match_case_bindings[j]) {
        for (size_t k = 0; k < stmt->match_case_binding_counts[j]; k++) {
          free(stmt->match_case_bindings[j][k]);
        }
        free(stmt->match_case_bindings[j]);
      }
    }
    free(stmt->match_case_bodies);
    if (stmt->match_default) {
      pie_program_free(stmt->match_default);
      free(stmt->match_default);
    }
    free(stmt->match_case_names);
    free(stmt->match_case_bindings);
    free(stmt->match_case_binding_counts);
  }
  for (size_t i = 0; i < program->function_count; i++) {
    PieFunction *function = &program->functions[i];
    free(function->name);
    for (size_t j = 0; j < function->param_count; j++) {
      free(function->param_names[j]);
    }
    free(function->param_names);
    free(function->param_types);
    if (function->body && !function->borrowed_body) {
      pie_program_free(function->body);
      free(function->body);
    }
  }
  free(program->stmts);
  free(program->functions);
  for (size_t i = 0; i < program->struct_count; i++) {
    PieStructDef *def = &program->structs[i];
    free(def->name);
    for (size_t j = 0; j < def->field_count; j++) {
      free(def->fields[j].name);
      free(def->fields[j].type.struct_name);
    }
    free(def->fields);
  }
  free(program->structs);
  for (size_t i = 0; i < program->enum_count; i++) {
    PieEnumDef *def = &program->enums[i];
    free(def->name);
    for (size_t j = 0; j < def->variant_count; j++) {
      free(def->variants[j].name);
      free(def->variants[j].payload_kinds);
      free(def->variants[j].payload_widths);
    }
  }
  free(program->enums);
  for (size_t i = 0; i < program->require_count; i++) {
    free(program->requires[i].path);
  }
  free(program->requires);
  for (size_t i = 0; i < program->owned_module_count; i++) {
    pie_program_free(program->owned_modules[i]);
    free(program->owned_modules[i]);
  }
  free(program->owned_modules);
  pie_program_init(program);
}

int pie_program_push_stmt(PieProgram *program, PieStmt stmt) {
  if (program->stmt_count == program->stmt_capacity) {
    size_t next_capacity =
        program->stmt_capacity ? program->stmt_capacity * 2 : 16;
    PieStmt *next =
        (PieStmt *)realloc(program->stmts, next_capacity * sizeof(PieStmt));
    if (!next) {
      return 0;
    }
    program->stmts = next;
    program->stmt_capacity = next_capacity;
  }
  program->stmts[program->stmt_count++] = stmt;
  return 1;
}

int pie_program_push_function(PieProgram *program, PieFunction function) {
  if (program->function_count == program->function_capacity) {
    size_t next_capacity =
        program->function_capacity ? program->function_capacity * 2 : 8;
    PieFunction *next = (PieFunction *)realloc(
        program->functions, next_capacity * sizeof(PieFunction));
    if (!next) {
      return 0;
    }
    program->functions = next;
    program->function_capacity = next_capacity;
  }
  program->functions[program->function_count++] = function;
  return 1;
}

int pie_program_push_owned_module(PieProgram *program, PieProgram *module) {
  if (program->owned_module_count == program->owned_module_capacity) {
    size_t next_capacity =
        program->owned_module_capacity ? program->owned_module_capacity * 2 : 8;
    PieProgram **next = (PieProgram **)realloc(
        program->owned_modules, next_capacity * sizeof(PieProgram *));
    if (!next) {
      return 0;
    }
    program->owned_modules = next;
    program->owned_module_capacity = next_capacity;
  }
  program->owned_modules[program->owned_module_count++] = module;
  return 1;
}

PieExpr *pie_expr_int(long long value) {
  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_EXPR_INT;
  expr->int_value = value;
  return expr;
}

PieExpr *pie_expr_float(double value) {
  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_EXPR_FLOAT;
  expr->float_value = value;
  return expr;
}

PieExpr *pie_expr_char(unsigned int value) {
  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_EXPR_CHAR;
  expr->char_value = value;
  return expr;
}

PieExpr *pie_expr_bool(int value) {
  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_EXPR_BOOL;
  expr->bool_value = value ? 1 : 0;
  return expr;
}

PieExpr *pie_expr_null(void) {
  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_EXPR_NULL;
  return expr;
}

PieExpr *pie_expr_tuple(size_t element_count) {
  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_EXPR_TUPLE;
  if (element_count > 0) {
    expr->tuple_elements = (PieExpr **)calloc(element_count, sizeof(PieExpr *));
    if (!expr->tuple_elements) {
      free(expr);
      return NULL;
    }
  }
  expr->tuple_element_count = element_count;
  return expr;
}

int pie_expr_tuple_add_element(PieExpr *tuple, PieExpr *element) {
  if (!tuple || tuple->kind != PIE_EXPR_TUPLE) {
    return 0;
  }
  size_t new_count = tuple->tuple_element_count + 1;
  PieExpr **new_elements =
      (PieExpr **)realloc(tuple->tuple_elements, new_count * sizeof(PieExpr *));
  if (!new_elements) {
    return 0;
  }
  new_elements[new_count - 1] = element;
  tuple->tuple_elements = new_elements;
  tuple->tuple_element_count = new_count;
  return 1;
}

PieExpr *pie_expr_list(size_t element_count) {
  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_EXPR_LIST;
  if (element_count > 0) {
    expr->list_elements = (PieExpr **)calloc(element_count, sizeof(PieExpr *));
    if (!expr->list_elements) {
      free(expr);
      return NULL;
    }
  }
  expr->list_element_count = element_count;
  return expr;
}

int pie_expr_list_add_element(PieExpr *list, PieExpr *element) {
  if (!list || list->kind != PIE_EXPR_LIST) {
    return 0;
  }
  size_t new_count = list->list_element_count + 1;
  PieExpr **new_elements =
      (PieExpr **)realloc(list->list_elements, new_count * sizeof(PieExpr *));
  if (!new_elements) {
    return 0;
  }
  new_elements[new_count - 1] = element;
  list->list_elements = new_elements;
  list->list_element_count = new_count;
  return 1;
}

PieExpr *pie_expr_index(PieExpr *object, PieExpr *index) {
  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_EXPR_INDEX;
  expr->index_object = object;
  expr->index_expr = index;
  return expr;
}

PieExpr *pie_expr_variant(const char *enum_name, const char *variant_name) {
  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_EXPR_VARIANT;
  if (enum_name) {
    expr->enum_name = pie_ast_strdup(enum_name);
  }
  if (variant_name) {
    expr->variant_name = pie_ast_strdup(variant_name);
  }
  return expr;
}

PieExpr *pie_expr_cast(PieExpr *inner, PieAstTypeKind target_kind,
                       int target_width) {
  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_EXPR_CAST;
  expr->cast_inner = inner;
  expr->cast_target_kind = target_kind;
  expr->cast_target_width = target_width;
  return expr;
}

PieExpr *pie_expr_range(PieExpr *start, PieExpr *end, int inclusive) {
  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_EXPR_RANGE;
  expr->range_start = start;
  expr->range_end = end;
  expr->range_inclusive = inclusive;
  return expr;
}

PieExpr *pie_expr_ternary(PieExpr *cond, PieExpr *true_expr,
                          PieExpr *false_expr) {
  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_EXPR_TERNARY;
  expr->left = cond;
  expr->right = true_expr;
  expr->ternary_false = false_expr;
  return expr;
}

PieExpr *pie_expr_map(void) {
  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_EXPR_MAP;
  return expr;
}

int pie_expr_map_add(PieExpr *map, PieExpr *key, PieExpr *value) {
  if (!map || !key || !value) {
    return 0;
  }
  size_t new_count = map->map_entry_count + 1;
  PieExpr **new_keys =
      (PieExpr **)realloc(map->map_keys, new_count * sizeof(PieExpr *));
  if (!new_keys) {
    return 0;
  }
  map->map_keys = new_keys;

  PieExpr **new_values =
      (PieExpr **)realloc(map->map_values, new_count * sizeof(PieExpr *));
  if (!new_values) {
    return 0;
  }
  map->map_values = new_values;

  map->map_keys[map->map_entry_count] = key;
  map->map_values[map->map_entry_count] = value;
  map->map_entry_count = new_count;
  return 1;
}

PieExpr *pie_expr_match(PieExpr *target) {
  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_EXPR_MATCH;
  expr->match_expr_target = target;
  return expr;
}

PieExpr *pie_expr_closure(void) {
  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_EXPR_CLOSURE;
  return expr;
}

PieExpr *pie_expr_method_call(PieExpr *object, const char *method_name) {
  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_EXPR_METHOD_CALL;
  expr->left = object;
  if (method_name) {
    expr->method_name = pie_ast_strdup(method_name);
  }
  return expr;
}

int pie_expr_method_call_add_arg(PieExpr *call, PieExpr *arg) {
  if (!call || !arg)
    return 0;
  if (call->call_arg_count >= call->call_arg_capacity) {
    size_t new_cap = call->call_arg_capacity ? call->call_arg_capacity * 2 : 4;
    PieCallArg *new_args =
        (PieCallArg *)realloc(call->call_args, new_cap * sizeof(PieCallArg));
    if (!new_args)
      return 0;
    call->call_args = new_args;
    call->call_arg_capacity = new_cap;
  }
  call->call_args[call->call_arg_count].expr = arg;
  call->call_arg_count++;
  return 1;
}

PieExpr *pie_expr_thread_call(PieThreadOp op) {
  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr)
    return NULL;
  expr->kind = PIE_EXPR_THREAD_CALL;
  expr->thread_op = op;
  return expr;
}

int pie_expr_thread_call_add_arg(PieExpr *call, PieExpr *arg) {
  return pie_expr_method_call_add_arg(call, arg);
}

PieExpr *pie_expr_string(const char *value, size_t len) {
  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_EXPR_STRING;
  expr->string_value = (char *)malloc(len ? len : 1);
  if (!expr->string_value) {
    free(expr);
    return NULL;
  }
  if (len) {
    memcpy(expr->string_value, value, len);
  }
  expr->string_len = len;
  return expr;
}

PieExpr *pie_expr_var(const char *name) {
  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_EXPR_VAR;
  expr->name = pie_ast_strdup(name);
  if (!expr->name) {
    free(expr);
    return NULL;
  }
  return expr;
}

PieExpr *pie_expr_call(const char *name) {
  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_EXPR_CALL;
  expr->call_name = pie_ast_strdup(name);
  if (!expr->call_name) {
    free(expr);
    return NULL;
  }
  return expr;
}

int pie_expr_call_add_arg(PieExpr *call, PieExpr *arg) {
  if (call->call_arg_count == call->call_arg_capacity) {
    size_t next_capacity =
        call->call_arg_capacity ? call->call_arg_capacity * 2 : 4;
    PieCallArg *next = (PieCallArg *)realloc(
        call->call_args, next_capacity * sizeof(PieCallArg));
    if (!next) {
      return 0;
    }
    call->call_args = next;
    call->call_arg_capacity = next_capacity;
  }
  call->call_args[call->call_arg_count++].expr = arg;
  return 1;
}

PieExpr *pie_expr_binary(char op, PieExpr *left, PieExpr *right) {
  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_EXPR_BINARY;
  expr->op = op;
  expr->op_text[0] = op;
  expr->op_text[1] = '\0';
  expr->left = left;
  expr->right = right;
  return expr;
}

PieExpr *pie_expr_binary_op(const char *op, PieExpr *left, PieExpr *right) {
  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_EXPR_BINARY;
  expr->op = op[0];
  strncpy(expr->op_text, op, sizeof(expr->op_text) - 1);
  expr->left = left;
  expr->right = right;
  return expr;
}

PieExpr *pie_expr_unary(char op, PieExpr *inner) {
  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_EXPR_UNARY;
  expr->op = op;
  expr->op_text[0] = op;
  expr->op_text[1] = '\0';
  expr->right = inner;
  return expr;
}

PieExpr *pie_expr_unary_op(const char *op, PieExpr *inner) {
  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_EXPR_UNARY;
  expr->op = op[0];
  strncpy(expr->op_text, op, sizeof(expr->op_text) - 1);
  expr->right = inner;
  return expr;
}

PieExpr *pie_expr_postfix(const char *op, PieExpr *inner) {
  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_EXPR_POSTFIX;
  expr->op = op[0];
  strncpy(expr->op_text, op, sizeof(expr->op_text) - 1);
  expr->right = inner;
  return expr;
}

PieExpr *pie_expr_try(PieExpr *inner) {
  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_EXPR_TRY;
  expr->right = inner;
  return expr;
}

void pie_expr_free(PieExpr *expr) {
  if (!expr) {
    return;
  }
  free(expr->string_value);
  free(expr->name);
  free(expr->call_name);
  free(expr->new_type_name);
  free(expr->field_name);
  free(expr->method_name);
  for (size_t i = 0; i < expr->call_arg_count; i++) {
    pie_expr_free(expr->call_args[i].expr);
  }
  free(expr->call_args);
  pie_expr_free(expr->left);
  pie_expr_free(expr->right);
  for (size_t i = 0; i < expr->tuple_element_count; i++) {
    pie_expr_free(expr->tuple_elements[i]);
  }
  free(expr->tuple_elements);
  for (size_t i = 0; i < expr->list_element_count; i++) {
    pie_expr_free(expr->list_elements[i]);
  }
  free(expr->list_elements);
  pie_expr_free(expr->index_object);
  pie_expr_free(expr->index_expr);
  free(expr->enum_name);
  free(expr->variant_name);
  pie_expr_free(expr->cast_inner);
  pie_expr_free(expr->ternary_false);
  pie_expr_free(expr->range_start);
  pie_expr_free(expr->range_end);
  for (size_t i = 0; i < expr->map_entry_count; i++) {
    pie_expr_free(expr->map_keys[i]);
    pie_expr_free(expr->map_values[i]);
  }
  free(expr->map_keys);
  free(expr->map_values);
  pie_expr_free(expr->match_expr_target);
  for (size_t i = 0; i < expr->match_expr_case_count; i++) {
    free(expr->match_expr_case_names[i]);
    for (size_t j = 0; j < expr->match_expr_case_binding_counts[i]; j++) {
      free(expr->match_expr_case_bindings[i][j]);
    }
    free(expr->match_expr_case_bindings[i]);
  }
  free(expr->match_expr_case_names);
  free(expr->match_expr_case_bindings);
  free(expr->match_expr_case_binding_counts);
  if (expr->closure_body) {
    pie_program_free(expr->closure_body);
    free(expr->closure_body);
  }
  for (size_t i = 0; i < expr->closure_param_count; i++) {
    free(expr->closure_param_names[i]);
    free(expr->closure_param_types[i].struct_name);
  }
  free(expr->closure_param_names);
  free(expr->closure_param_types);

  free(expr->closure_return_type.struct_name);

  for (size_t i = 0; i < expr->closure_capture_count; i++) {
    free(expr->closure_capture_names[i]);
  }
  free(expr->closure_capture_names);
  free(expr->closure_capture_types);
  pie_expr_free(expr->if_condition);
  pie_expr_free(expr->if_then);
  pie_expr_free(expr->if_else);
  free(expr);
}

typedef struct PieNewArg {
  char *field_name;
  PieExpr *value;
} PieNewArg;

PieExpr *pie_expr_new(const char *type_name) {
  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_EXPR_NEW;
  expr->new_type_name = pie_ast_strdup(type_name);
  if (!expr->new_type_name) {
    free(expr);
    return NULL;
  }
  return expr;
}

int pie_expr_new_add_arg(PieExpr *new_expr, const char *field_name,
                         PieExpr *value) {
  if (new_expr->call_arg_count == new_expr->call_arg_capacity) {
    size_t next_capacity =
        new_expr->call_arg_capacity ? new_expr->call_arg_capacity * 2 : 4;
    PieCallArg *next = (PieCallArg *)realloc(
        new_expr->call_args, next_capacity * sizeof(PieCallArg));
    if (!next) {
      return 0;
    }
    new_expr->call_args = next;
    new_expr->call_arg_capacity = next_capacity;
  }
  PieExpr *named = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!named) {
    return 0;
  }
  named->kind = PIE_EXPR_VAR;
  named->name = pie_ast_strdup(field_name);
  named->right = value;
  new_expr->call_args[new_expr->call_arg_count].expr = named;
  new_expr->call_arg_count++;
  return 1;
}

PieExpr *pie_expr_field(PieExpr *object, const char *field_name) {
  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_EXPR_FIELD;
  expr->left = object;
  expr->field_name = pie_ast_strdup(field_name);
  if (!expr->field_name) {
    pie_expr_free(expr);
    return NULL;
  }
  return expr;
}

int pie_program_push_struct(PieProgram *program, PieStructDef def) {
  if (program->struct_count == program->struct_capacity) {
    size_t next_capacity =
        program->struct_capacity ? program->struct_capacity * 2 : 8;
    PieStructDef *next = (PieStructDef *)realloc(
        program->structs, next_capacity * sizeof(PieStructDef));
    if (!next) {
      return 0;
    }
    program->structs = next;
    program->struct_capacity = next_capacity;
  }
  program->structs[program->struct_count++] = def;
  return 1;
}

int pie_program_push_enum(PieProgram *program, PieEnumDef def) {
  if (program->enum_count == program->enum_capacity) {
    size_t next_capacity =
        program->enum_capacity ? program->enum_capacity * 2 : 8;
    PieEnumDef *next = (PieEnumDef *)realloc(
        program->enums, next_capacity * sizeof(PieEnumDef));
    if (!next) {
      return 0;
    }
    program->enums = next;
    program->enum_capacity = next_capacity;
  }
  program->enums[program->enum_count++] = def;
  return 1;
}

int pie_program_push_const(PieProgram *program, PieConstDef def) {
  if (program->const_count == program->const_capacity) {
    size_t next_capacity =
        program->const_capacity ? program->const_capacity * 2 : 8;
    PieConstDef *next = (PieConstDef *)realloc(
        program->consts, next_capacity * sizeof(PieConstDef));
    if (!next) {
      return 0;
    }
    program->consts = next;
    program->const_capacity = next_capacity;
  }
  program->consts[program->const_count++] = def;
  return 1;
}

const PieConstDef *pie_program_find_const(const PieProgram *program,
                                          const char *name) {
  for (size_t i = 0; i < program->const_count; i++) {
    if (strcmp(program->consts[i].name, name) == 0) {
      return &program->consts[i];
    }
  }
  return NULL;
}

const PieEnumDef *pie_program_find_enum(const PieProgram *program,
                                        const char *name) {
  for (size_t i = 0; i < program->enum_count; i++) {
    if (strcmp(program->enums[i].name, name) == 0) {
      return &program->enums[i];
    }
  }
  return NULL;
}

const PieStructDef *pie_program_find_struct(const PieProgram *program,
                                            const char *name) {
  for (size_t i = 0; i < program->struct_count; i++) {
    if (strcmp(program->structs[i].name, name) == 0) {
      return &program->structs[i];
    }
  }
  return NULL;
}

int pie_program_push_require(PieProgram *program, const char *path,
                             PieRequireKind kind) {
  if (program->require_count == program->require_capacity) {
    size_t next_capacity =
        program->require_capacity ? program->require_capacity * 2 : 8;
    PieRequire *next = (PieRequire *)realloc(
        program->requires, next_capacity * sizeof(PieRequire));
    if (!next) {
      return 0;
    }
    program->requires = next;
    program->require_capacity = next_capacity;
  }

  PieRequire *require = &program->requires[program->require_count];
  require->path = pie_ast_strdup(path);
  if (!require->path) {
    return 0;
  }
  require->kind = kind;
  program->require_count++;
  return 1;
}
PieExpr *pie_expr_maybe(void) {
  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr)
    return NULL;
  expr->kind = PIE_EXPR_MAYBE;
  return expr;
}
