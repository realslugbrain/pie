#include "pie/core/ir/ir.h"

#include <stdlib.h>
#include <string.h>

static char *ir_strdup(const char *s) {
  size_t len = strlen(s);
  char *copy = (char *)malloc(len + 1);
  if (!copy) {
    return NULL;
  }
  memcpy(copy, s, len + 1);
  return copy;
}

void pie_ir_program_init(PieIrProgram *program) {
  program->locals = NULL;
  program->local_count = 0;
  program->local_capacity = 0;
  program->stmts = NULL;
  program->stmt_count = 0;
  program->stmt_capacity = 0;
  program->functions = NULL;
  program->function_count = 0;
  program->function_capacity = 0;
  program->structs = NULL;
  program->struct_count = 0;
  program->struct_capacity = 0;
}

void pie_ir_expr_free(PieIrExpr *expr) {
  if (!expr) {
    return;
  }
  free(expr->string_value);
  free(expr->call_name);
  for (size_t i = 0; i < expr->call_arg_count; i++) {
    pie_ir_expr_free(expr->call_args[i].expr);
  }
  free(expr->call_args);
  pie_ir_expr_free(expr->left);
  pie_ir_expr_free(expr->right);
  for (size_t i = 0; i < expr->tuple_element_count; i++) {
    pie_ir_expr_free(expr->tuple_elements[i]);
  }
  free(expr->tuple_elements);
  for (size_t i = 0; i < expr->list_element_count; i++) {
    pie_ir_expr_free(expr->list_elements[i]);
  }
  free(expr->list_elements);
  free(expr->enum_name);
  free(expr->variant_name);
  free(expr->method_name);
  for (size_t i = 0; i < expr->closure_captured_count; i++) {
    free(expr->closure_captured_names[i]);
  }
  free(expr->closure_captured_names);
  free(expr->closure_capture_types);
  free(expr->closure_capture_outer_ids);

  if (expr->kind == PIE_IR_EXPR_MATCH) {
    pie_ir_expr_free(expr->match_expr_target);
    for (size_t i = 0; i < expr->match_expr_case_count; i++) {
      free(expr->match_expr_case_names[i]);
      if (expr->match_expr_case_bodies[i]) {
        pie_ir_program_free(expr->match_expr_case_bodies[i]);
        free(expr->match_expr_case_bodies[i]);
      }
      if (expr->match_expr_case_bindings && expr->match_expr_case_bindings[i]) {
        for (size_t j = 0; j < expr->match_expr_case_binding_counts[i]; j++) {
          free(expr->match_expr_case_bindings[i][j]);
        }
        free(expr->match_expr_case_bindings[i]);
      }
      if (expr->match_expr_case_binding_ids) {
        free(expr->match_expr_case_binding_ids[i]);
      }
    }
    free(expr->match_expr_case_names);
    free(expr->match_expr_case_bodies);
    free(expr->match_expr_case_bindings);
    free(expr->match_expr_case_binding_ids);
    free(expr->match_expr_case_binding_counts);
    free(expr->match_expr_case_tags);
    if (expr->match_expr_value_exprs) {
      for (size_t i = 0; i < expr->match_expr_case_count; i++) {
        if (expr->match_expr_value_exprs[i]) {
          pie_ir_expr_free(expr->match_expr_value_exprs[i]);
        }
      }
      free(expr->match_expr_value_exprs);
    }
    if (expr->match_expr_default_value) {
      pie_ir_expr_free(expr->match_expr_default_value);
    }
    if (expr->match_expr_default) {
      pie_ir_program_free(expr->match_expr_default);
      free(expr->match_expr_default);
    }
  }

  pie_ir_expr_free(expr->cast_inner);
  pie_ir_expr_free(expr->ternary_false);
  pie_ir_expr_free(expr->if_condition);
  pie_ir_expr_free(expr->if_then);
  pie_ir_expr_free(expr->if_else);

  if (expr->kind == PIE_IR_EXPR_FORMAT) {
    pie_ir_expr_free(expr->format_template);
    for (size_t i = 0; i < expr->format_arg_count; i++) {
      pie_ir_expr_free(expr->format_args[i]);
    }
    free(expr->format_args);
  }

  free(expr);
}

void pie_ir_program_free(PieIrProgram *program) {
  for (size_t i = 0; i < program->local_count; i++) {
    free(program->locals[i].name);
  }
  for (size_t i = 0; i < program->stmt_count; i++) {
    PieIrStmt *stmt = &program->stmts[i];
    pie_ir_expr_free(stmt->target);
    pie_ir_expr_free(stmt->expr);
    for (size_t j = 0; j < stmt->arg_count; j++) {
      free(stmt->args[j].text);
      pie_ir_expr_free(stmt->args[j].expr);
    }
    free(stmt->args);
    if (stmt->then_branch) {
      pie_ir_program_free(stmt->then_branch);
      free(stmt->then_branch);
    }
    if (stmt->else_branch) {
      pie_ir_program_free(stmt->else_branch);
      free(stmt->else_branch);
    }

    pie_ir_expr_free(stmt->field_target);
    pie_ir_expr_free(stmt->index_target);
    pie_ir_expr_free(stmt->index_expr);
    free(stmt->label_name);

    if (stmt->kind == PIE_IR_STMT_ASSERT) {
      pie_ir_expr_free(stmt->assert_cond);
    }
    if (stmt->kind == PIE_IR_STMT_ASSERT_EQ) {
      pie_ir_expr_free(stmt->assert_left);
      pie_ir_expr_free(stmt->assert_right);
    }

    if (stmt->kind == PIE_IR_STMT_MATCH) {
      pie_ir_expr_free(stmt->match_target);
      for (size_t j = 0; j < stmt->match_case_count; j++) {
        free(stmt->match_case_names[j]);
        if (stmt->match_case_bodies[j]) {
          pie_ir_program_free(stmt->match_case_bodies[j]);
          free(stmt->match_case_bodies[j]);
        }
        if (stmt->match_case_bindings && stmt->match_case_bindings[j]) {
          for (size_t k = 0; k < stmt->match_case_binding_counts[j]; k++) {
            free(stmt->match_case_bindings[j][k]);
          }
          free(stmt->match_case_bindings[j]);
        }
        if (stmt->match_case_binding_ids) {
          free(stmt->match_case_binding_ids[j]);
        }
      }
      free(stmt->match_case_names);
      free(stmt->match_case_bodies);
      free(stmt->match_case_bindings);
      free(stmt->match_case_binding_ids);
      free(stmt->match_case_binding_counts);
      free(stmt->match_case_tags);
      if (stmt->match_default) {
        pie_ir_program_free(stmt->match_default);
        free(stmt->match_default);
      }
    }
  }
  for (size_t i = 0; i < program->function_count; i++) {
    PieIrFunction *function = &program->functions[i];
    free(function->name);
    for (size_t j = 0; j < function->param_count; j++) {
      free(function->param_names[j]);
    }
    free(function->param_names);
    free(function->param_types);
    free(function->param_type_widths);
    free(function->param_raw_pointee_types);
    free(function->param_raw_pointee_widths);
    free(function->param_local_ids);
    if (function->body) {
      pie_ir_program_free(function->body);
      free(function->body);
    }
  }
  free(program->locals);
  free(program->stmts);
  free(program->functions);
  pie_ir_program_init(program);
}

int pie_ir_program_add_raw_typed_local(PieIrProgram *program, const char *name,
                                       int is_mut, PieIrTypeKind type,
                                       int type_width,
                                       PieIrTypeKind raw_pointee_type,
                                       int raw_pointee_width, size_t *out_id) {
  if (program->local_count == program->local_capacity) {
    size_t next_capacity =
        program->local_capacity ? program->local_capacity * 2 : 16;
    PieIrLocal *next = (PieIrLocal *)realloc(
        program->locals, next_capacity * sizeof(PieIrLocal));
    if (!next) {
      return 0;
    }
    program->locals = next;
    program->local_capacity = next_capacity;
  }

  PieIrLocal *local = &program->locals[program->local_count];
  local->name = ir_strdup(name);
  if (!local->name) {
    return 0;
  }
  local->is_mut = is_mut;
  local->type = type;
  local->type_width = type_width;
  local->raw_pointee_type = raw_pointee_type;
  local->raw_pointee_width = raw_pointee_width;
  local->nullable_inner_type = PIE_IR_TYPE_UNKNOWN;
  local->nullable_inner_width = 0;
  local->ref_inner_type = PIE_IR_TYPE_UNKNOWN;
  local->ref_inner_width = 0;
  *out_id = program->local_count;
  program->local_count++;
  return 1;
}

int pie_ir_program_add_nullable_local(PieIrProgram *program, const char *name,
                                      int is_mut, PieIrTypeKind inner_type,
                                      int inner_width, size_t *out_id) {
  if (program->local_count == program->local_capacity) {
    size_t next_capacity =
        program->local_capacity ? program->local_capacity * 2 : 16;
    PieIrLocal *next = (PieIrLocal *)realloc(
        program->locals, next_capacity * sizeof(PieIrLocal));
    if (!next) {
      return 0;
    }
    program->locals = next;
    program->local_capacity = next_capacity;
  }

  PieIrLocal *local = &program->locals[program->local_count];
  local->name = ir_strdup(name);
  if (!local->name) {
    return 0;
  }
  local->is_mut = is_mut;
  local->type = PIE_IR_TYPE_NULLABLE;
  local->type_width = 0;
  local->raw_pointee_type = PIE_IR_TYPE_UNKNOWN;
  local->raw_pointee_width = 0;
  local->nullable_inner_type = inner_type;
  local->nullable_inner_width = inner_width;
  *out_id = program->local_count;
  program->local_count++;
  return 1;
}

int pie_ir_program_add_typed_local(PieIrProgram *program, const char *name,
                                   int is_mut, PieIrTypeKind type,
                                   int type_width, size_t *out_id) {
  return pie_ir_program_add_raw_typed_local(program, name, is_mut, type,
                                            type_width, PIE_IR_TYPE_UNKNOWN,
                                            PIE_WIDTH_INFER, out_id);
}

int pie_ir_program_add_local(PieIrProgram *program, const char *name,
                             int is_mut, size_t *out_id) {
  return pie_ir_program_add_typed_local(program, name, is_mut,
                                        PIE_IR_TYPE_UNKNOWN, 0, out_id);
}

int pie_ir_program_find_local(const PieIrProgram *program, const char *name,
                              size_t *out_id, int *out_is_mut,
                              PieIrTypeKind *out_type, int *out_type_width,
                              PieIrTypeKind *out_raw_pointee_type,
                              int *out_raw_pointee_width) {
  for (size_t i = 0; i < program->local_count; i++) {
    if (strcmp(program->locals[i].name, name) == 0) {
      *out_id = i;
      if (out_is_mut) {
        *out_is_mut = program->locals[i].is_mut;
      }
      if (out_type) {
        *out_type = program->locals[i].type;
      }
      if (out_type_width) {
        *out_type_width = program->locals[i].type_width;
      }
      if (out_raw_pointee_type) {
        *out_raw_pointee_type = program->locals[i].raw_pointee_type;
      }
      if (out_raw_pointee_width) {
        *out_raw_pointee_width = program->locals[i].raw_pointee_width;
      }
      return 1;
    }
  }
  return 0;
}

int pie_ir_program_push_stmt(PieIrProgram *program, PieIrStmt stmt) {
  if (program->stmt_count == program->stmt_capacity) {
    size_t next_capacity =
        program->stmt_capacity ? program->stmt_capacity * 2 : 16;
    PieIrStmt *next =
        (PieIrStmt *)realloc(program->stmts, next_capacity * sizeof(PieIrStmt));
    if (!next) {
      return 0;
    }
    program->stmts = next;
    program->stmt_capacity = next_capacity;
  }
  program->stmts[program->stmt_count++] = stmt;
  return 1;
}

int pie_ir_program_push_function(PieIrProgram *program,
                                 PieIrFunction function) {
  if (program->function_count == program->function_capacity) {
    size_t next_capacity =
        program->function_capacity ? program->function_capacity * 2 : 8;
    PieIrFunction *next = (PieIrFunction *)realloc(
        program->functions, next_capacity * sizeof(PieIrFunction));
    if (!next) {
      return 0;
    }
    program->functions = next;
    program->function_capacity = next_capacity;
  }
  program->functions[program->function_count++] = function;
  return 1;
}

PieIrExpr *pie_ir_expr_int(long long value) {
  PieIrExpr *expr = (PieIrExpr *)calloc(1, sizeof(PieIrExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_IR_EXPR_INT;
  expr->type = PIE_IR_TYPE_INT;
  expr->int_value = value;
  return expr;
}

PieIrExpr *pie_ir_expr_float(double value) {
  PieIrExpr *expr = (PieIrExpr *)calloc(1, sizeof(PieIrExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_IR_EXPR_FLOAT;
  expr->type = PIE_IR_TYPE_FLOAT;
  expr->float_value = value;
  return expr;
}

PieIrExpr *pie_ir_expr_char(unsigned int value) {
  PieIrExpr *expr = (PieIrExpr *)calloc(1, sizeof(PieIrExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_IR_EXPR_CHAR;
  expr->type = PIE_IR_TYPE_CHAR;
  expr->char_value = value;
  return expr;
}

PieIrExpr *pie_ir_expr_bool(int value) {
  PieIrExpr *expr = (PieIrExpr *)calloc(1, sizeof(PieIrExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_IR_EXPR_BOOL;
  expr->type = PIE_IR_TYPE_BOOL;
  expr->bool_value = value ? 1 : 0;
  return expr;
}

PieIrExpr *pie_ir_expr_maybe(void) {
  PieIrExpr *expr = (PieIrExpr *)calloc(1, sizeof(PieIrExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_IR_EXPR_MAYBE;
  expr->type = PIE_IR_TYPE_BOOL;
  expr->type_width = PIE_WIDTH_INFER;
  return expr;
}

PieIrExpr *pie_ir_expr_format(PieIrExpr *template, PieIrExpr **args,
                              size_t arg_count) {
  PieIrExpr *expr = (PieIrExpr *)calloc(1, sizeof(PieIrExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_IR_EXPR_FORMAT;
  expr->type = PIE_IR_TYPE_STRING;
  expr->type_width = PIE_WIDTH_INFER;
  expr->format_template = template;
  expr->format_args = args;
  expr->format_arg_count = arg_count;
  return expr;
}

PieIrExpr *pie_ir_expr_null(void) {
  PieIrExpr *expr = (PieIrExpr *)calloc(1, sizeof(PieIrExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_IR_EXPR_NULL;
  expr->type = PIE_IR_TYPE_NULL;
  return expr;
}

PieIrExpr *pie_ir_expr_string(const char *value, size_t len) {
  PieIrExpr *expr = (PieIrExpr *)calloc(1, sizeof(PieIrExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_IR_EXPR_STRING;
  expr->type = PIE_IR_TYPE_STRING;
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

PieIrExpr *pie_ir_expr_local(size_t local_id, PieIrTypeKind type,
                             int type_width, PieIrTypeKind ref_inner_type,
                             int ref_inner_width) {
  PieIrExpr *expr = (PieIrExpr *)calloc(1, sizeof(PieIrExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_IR_EXPR_LOCAL;
  expr->type = type;
  expr->type_width = type_width;
  expr->local_id = local_id;
  expr->ref_inner_type = ref_inner_type;
  expr->ref_inner_width = ref_inner_width;
  return expr;
}

PieIrExpr *pie_ir_expr_raw_local(size_t local_id, PieIrTypeKind type,
                                 int type_width, PieIrTypeKind raw_pointee_type,
                                 int raw_pointee_width) {
  PieIrExpr *expr = pie_ir_expr_local(local_id, type, type_width,
                                      PIE_IR_TYPE_UNKNOWN, PIE_WIDTH_INFER);
  if (!expr) {
    return NULL;
  }
  expr->raw_pointee_type = raw_pointee_type;
  expr->raw_pointee_width = raw_pointee_width;
  return expr;
}

PieIrExpr *pie_ir_expr_call(const char *name, PieIrTypeKind return_type) {
  PieIrExpr *expr = (PieIrExpr *)calloc(1, sizeof(PieIrExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_IR_EXPR_CALL;
  expr->type = return_type;
  expr->call_name = ir_strdup(name);
  if (!expr->call_name) {
    free(expr);
    return NULL;
  }
  return expr;
}

int pie_ir_expr_call_add_arg(PieIrExpr *call, PieIrExpr *arg) {
  if (call->call_arg_count == call->call_arg_capacity) {
    size_t next_capacity =
        call->call_arg_capacity ? call->call_arg_capacity * 2 : 4;
    PieIrCallArg *next = (PieIrCallArg *)realloc(
        call->call_args, next_capacity * sizeof(PieIrCallArg));
    if (!next) {
      return 0;
    }
    call->call_args = next;
    call->call_arg_capacity = next_capacity;
  }
  call->call_args[call->call_arg_count++].expr = arg;
  return 1;
}

PieIrExpr *pie_ir_expr_binary(char op, PieIrExpr *left, PieIrExpr *right) {
  PieIrExpr *expr = (PieIrExpr *)calloc(1, sizeof(PieIrExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_IR_EXPR_BINARY;
  expr->type = PIE_IR_TYPE_INT;
  expr->op = op;
  expr->op_text[0] = op;
  expr->op_text[1] = '\0';
  expr->left = left;
  expr->right = right;
  return expr;
}

PieIrExpr *pie_ir_expr_binary_typed(const char *op, PieIrExpr *left,
                                    PieIrExpr *right, PieIrTypeKind type) {
  PieIrExpr *expr = (PieIrExpr *)calloc(1, sizeof(PieIrExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_IR_EXPR_BINARY;
  expr->type = type;
  expr->op = op[0];
  strncpy(expr->op_text, op, sizeof(expr->op_text) - 1);
  expr->left = left;
  expr->right = right;
  return expr;
}

PieIrExpr *pie_ir_expr_unary(char op, PieIrExpr *inner) {
  PieIrExpr *expr = (PieIrExpr *)calloc(1, sizeof(PieIrExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_IR_EXPR_UNARY;
  expr->type = PIE_IR_TYPE_INT;
  expr->op = op;
  expr->op_text[0] = op;
  expr->op_text[1] = '\0';
  expr->right = inner;
  return expr;
}

PieIrExpr *pie_ir_expr_unary_typed(const char *op, PieIrExpr *inner,
                                   PieIrTypeKind type) {
  PieIrExpr *expr = (PieIrExpr *)calloc(1, sizeof(PieIrExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_IR_EXPR_UNARY;
  expr->type = type;
  expr->op = op[0];
  strncpy(expr->op_text, op, sizeof(expr->op_text) - 1);
  expr->right = inner;
  return expr;
}

static void write_escaped_string(FILE *out, const char *text, size_t len) {
  fputc('"', out);
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)text[i];
    if (c == '"' || c == '\\') {
      fputc('\\', out);
      fputc(c, out);
    } else if (c == '\n') {
      fputs("\\n", out);
    } else if (c == '\t') {
      fputs("\\t", out);
    } else if (c >= 32 && c <= 126) {
      fputc(c, out);
    } else {
      fprintf(out, "\\x%02x", c);
    }
  }
  fputc('"', out);
}

static void write_expr(const PieIrExpr *expr, FILE *out) {
  if (!expr) {
    fputs("<null>", out);
    return;
  }

  switch (expr->kind) {
  case PIE_IR_EXPR_INT:
    fprintf(out, "%lld", expr->int_value);
    break;
  case PIE_IR_EXPR_FLOAT:
    fprintf(out, "%.17g", expr->float_value);
    break;
  case PIE_IR_EXPR_CHAR:
    if (expr->char_value >= 32 && expr->char_value <= 126 &&
        expr->char_value != '\'' && expr->char_value != '\\') {
      fprintf(out, "'%c'", (char)expr->char_value);
    } else {
      fprintf(out, "'\\x%02x'", expr->char_value & 0xffu);
    }
    break;
  case PIE_IR_EXPR_BOOL:
    fputs(expr->bool_value ? "true" : "false", out);
    break;
  case PIE_IR_EXPR_MAYBE:
    fputs("maybe", out);
    break;
  case PIE_IR_EXPR_STRING:
    write_escaped_string(out, expr->string_value, expr->string_len);
    break;
  case PIE_IR_EXPR_LOCAL:
    fprintf(out, "%%%zu", expr->local_id);
    break;
  case PIE_IR_EXPR_CALL:
    fprintf(out, "%s(", expr->call_name);
    for (size_t i = 0; i < expr->call_arg_count; i++) {
      if (i) {
        fputs(", ", out);
      }
      write_expr(expr->call_args[i].expr, out);
    }
    fputc(')', out);
    break;
  case PIE_IR_EXPR_BINARY:
    fputc('(', out);
    write_expr(expr->left, out);
    fprintf(out, " %s ", expr->op_text);
    write_expr(expr->right, out);
    fputc(')', out);
    break;
  case PIE_IR_EXPR_UNARY:
    fprintf(out, "(%s ", expr->op_text);
    write_expr(expr->right, out);
    fputc(')', out);
    break;
  case PIE_IR_EXPR_NEW:
    fputs("<new>", out);
    break;
  case PIE_IR_EXPR_NULL:
    fputs("null", out);
    break;
  case PIE_IR_EXPR_TUPLE:
    fputc('(', out);
    for (size_t i = 0; i < expr->tuple_element_count; i++) {
      if (i)
        fputs(", ", out);
      write_expr(expr->tuple_elements[i], out);
    }
    fputc(')', out);
    break;
  case PIE_IR_EXPR_LIST:
    fputc('[', out);
    for (size_t i = 0; i < expr->list_element_count; i++) {
      if (i)
        fputs(", ", out);
      write_expr(expr->list_elements[i], out);
    }
    fputc(']', out);
    break;
  case PIE_IR_EXPR_INDEX:
    fputs("<index>", out);
    break;
  case PIE_IR_EXPR_VARIANT:
    fprintf(out, "%s.%s", expr->enum_name ? expr->enum_name : "?",
            expr->variant_name ? expr->variant_name : "?");
    if (expr->call_arg_count > 0) {
      fputc('(', out);
      for (size_t i = 0; i < expr->call_arg_count; i++) {
        if (i)
          fputs(", ", out);
        write_expr(expr->call_args[i].expr, out);
      }
      fputc(')', out);
    }
    break;
  case PIE_IR_EXPR_CAST:
    fputs("cast(", out);
    write_expr(expr->cast_inner, out);
    fprintf(out, " as %s)", pie_ir_type_name(expr->cast_target_type));
    break;
  case PIE_IR_EXPR_TERNARY:
    write_expr(expr->left, out);
    fputs(" ? ", out);
    write_expr(expr->right, out);
    fputs(" : ", out);
    write_expr(expr->ternary_false, out);
    break;
  case PIE_IR_EXPR_RANGE:
    write_expr(expr->range_start, out);
    fputs(expr->range_inclusive ? "..=" : "..", out);
    write_expr(expr->range_end, out);
    break;
  case PIE_IR_EXPR_MAP:
    fputs("{", out);
    for (size_t i = 0; i < expr->map_entry_count; i++) {
      if (i)
        fputs(", ", out);
      write_expr(expr->map_keys[i], out);
      fputs(": ", out);
      write_expr(expr->map_values[i], out);
    }
    fputs("}", out);
    break;
  case PIE_IR_EXPR_MATCH:
    fputs("(match ", out);
    write_expr(expr->match_expr_target, out);
    fputs(")", out);
    break;
  case PIE_IR_EXPR_CLOSURE:
    fputs("(closure)", out);
    break;
  case PIE_IR_EXPR_METHOD_CALL:
    write_expr(expr->left, out);
    fprintf(out, ".%s(", expr->method_name ? expr->method_name : "?");
    for (size_t i = 0; i < expr->call_arg_count; i++) {
      if (i)
        fputs(", ", out);
      write_expr(expr->call_args[i].expr, out);
    }
    fputc(')', out);
    break;
  case PIE_IR_EXPR_CLOSURE_CALL:
    write_expr(expr->left, out);
    fputc('(', out);
    for (size_t i = 0; i < expr->call_arg_count; i++) {
      if (i)
        fputs(", ", out);
      write_expr(expr->call_args[i].expr, out);
    }
    fputc(')', out);
    break;
  case PIE_IR_EXPR_FIELD:
    fputs("<field>", out);
    break;
  }
}

void pie_ir_program_write_text(const PieIrProgram *program, FILE *out) {
  for (size_t i = 0; i < program->function_count; i++) {
    const PieIrFunction *function = &program->functions[i];
    fprintf(out, "fn %s(", function->name);
    for (size_t j = 0; j < function->param_count; j++) {
      if (j) {
        fputs(", ", out);
      }
      fprintf(out, "%s: %s", function->param_names[j],
              pie_ir_type_name(function->param_types[j]));
      if (function->param_types[j] == PIE_IR_TYPE_RAW_PTR) {
        fprintf(out, "<%s>",
                pie_ir_type_name(function->param_raw_pointee_types[j]));
      }
    }
    fprintf(out, ") -> %s:\n", pie_ir_type_name(function->return_type));
    if (function->body) {
      pie_ir_program_write_text(function->body, out);
    }
    fputs("end\n", out);
  }

  fputs("locals:\n", out);
  for (size_t i = 0; i < program->local_count; i++) {
    fprintf(out, "  %%%zu %s type=%s mut=%d\n", i, program->locals[i].name,
            pie_ir_type_name(program->locals[i].type),
            program->locals[i].is_mut);
    if (program->locals[i].type == PIE_IR_TYPE_RAW_PTR) {
      fprintf(out, "    raw_pointee=%s width=%d\n",
              pie_ir_type_name(program->locals[i].raw_pointee_type),
              program->locals[i].raw_pointee_width);
    }
    if (program->locals[i].type == PIE_IR_TYPE_REF ||
        program->locals[i].type == PIE_IR_TYPE_REF_MUT) {
      fprintf(out, "    ref_inner=%s width=%d\n",
              pie_ir_type_name(program->locals[i].ref_inner_type),
              program->locals[i].ref_inner_width);
    }
  }

  fputs("body:\n", out);
  for (size_t i = 0; i < program->stmt_count; i++) {
    const PieIrStmt *stmt = &program->stmts[i];
    switch (stmt->kind) {
    case PIE_IR_STMT_EXPR:
      fputs("  expr ", out);
      write_expr(stmt->expr, out);
      fputc('\n', out);
      break;
    case PIE_IR_STMT_LET:
      fprintf(out, "  let %%%zu = ", stmt->local_id);
      write_expr(stmt->expr, out);
      fputc('\n', out);
      break;
    case PIE_IR_STMT_ASSIGN:
      fprintf(out, "  assign %%%zu %s ", stmt->local_id, stmt->assign_op);
      write_expr(stmt->expr, out);
      fputc('\n', out);
      break;
    case PIE_IR_STMT_RAW_STORE:
      fputs("  raw_store ", out);
      write_expr(stmt->target, out);
      fputs(" = ", out);
      write_expr(stmt->expr, out);
      fputc('\n', out);
      break;
    case PIE_IR_STMT_PRINT:
      fprintf(out, "  %s ", stmt->println ? "println" : "print");
      for (size_t j = 0; j < stmt->arg_count; j++) {
        if (j) {
          fputs(", ", out);
        }
        if (stmt->args[j].is_string) {
          write_escaped_string(out, stmt->args[j].text, stmt->args[j].text_len);
        } else {
          write_expr(stmt->args[j].expr, out);
        }
      }
      fputc('\n', out);
      break;
    case PIE_IR_STMT_RETURN:
      fputs("  return", out);
      if (stmt->expr) {
        fputc(' ', out);
        write_expr(stmt->expr, out);
      }
      fputc('\n', out);
      break;
    case PIE_IR_STMT_IF:
      fputs("  if ", out);
      write_expr(stmt->expr, out);
      fputs(":\n", out);
      if (stmt->then_branch) {
        pie_ir_program_write_text(stmt->then_branch, out);
      }
      if (stmt->else_branch) {
        fputs("  else:\n", out);
        pie_ir_program_write_text(stmt->else_branch, out);
      }
      fputs("  end\n", out);
      break;
    case PIE_IR_STMT_WHILE:
      fputs("  while ", out);
      write_expr(stmt->expr, out);
      fputs(":\n", out);
      if (stmt->then_branch) {
        pie_ir_program_write_text(stmt->then_branch, out);
      }
      fputs("  end\n", out);
      break;
    case PIE_IR_STMT_REGION:
      fputs("  region:\n", out);
      if (stmt->then_branch) {
        pie_ir_program_write_text(stmt->then_branch, out);
      }
      fputs("  end\n", out);
      break;
    case PIE_IR_STMT_UNSAFE:
      fputs("  unsafe:\n", out);
      if (stmt->then_branch) {
        pie_ir_program_write_text(stmt->then_branch, out);
      }
      fputs("  end\n", out);
      break;
    case PIE_IR_STMT_BREAK:
      fputs("  break\n", out);
      break;
    case PIE_IR_STMT_CONTINUE:
      fputs("  continue\n", out);
      break;
    case PIE_IR_STMT_PASS:
      fputs("  pass\n", out);
      break;
    case PIE_IR_STMT_STRUCT:
      fputs("  struct", out);
      fputc('\n', out);
      break;
    case PIE_IR_STMT_FIELD_ASSIGN:
      fputs("  field_assign", out);
      fputc('\n', out);
      break;
    case PIE_IR_STMT_INDEX_ASSIGN:
      fputs("  index_assign", out);
      fputc('\n', out);
      break;
    case PIE_IR_STMT_MATCH:
      fputs("  match ", out);
      write_expr(stmt->match_target, out);
      fputs(":\n", out);
      for (size_t j = 0; j < stmt->match_case_count; j++) {
        fprintf(out, "    case %s", stmt->match_case_names[j]);
        if (stmt->match_case_binding_counts[j] > 0) {
          fputs("(", out);
          for (size_t k = 0; k < stmt->match_case_binding_counts[j]; k++) {
            if (k)
              fputs(", ", out);
            fprintf(out, "%%%zu", stmt->match_case_binding_ids[j][k]);
          }
          fputs(")", out);
        }
        fputs(":\n", out);
        if (stmt->match_case_bodies[j]) {
          pie_ir_program_write_text(stmt->match_case_bodies[j], out);
        }
      }
      if (stmt->match_default) {
        fputs("    case _:\n", out);
        pie_ir_program_write_text(stmt->match_default, out);
      }
      fputs("  end\n", out);
      break;
    }
  }
}

PieIrExpr *pie_ir_expr_new(const char *struct_name) {
  PieIrExpr *expr = (PieIrExpr *)calloc(1, sizeof(PieIrExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_IR_EXPR_NEW;
  expr->type = PIE_IR_TYPE_STRUCT;
  if (struct_name) {
    size_t len = strlen(struct_name);
    expr->struct_name = malloc(len + 1);
    if (expr->struct_name) {
      memcpy(expr->struct_name, struct_name, len + 1);
    }
  }
  return expr;
}

PieIrExpr *pie_ir_expr_field(PieIrExpr *object, const char *field_name) {
  PieIrExpr *expr = (PieIrExpr *)calloc(1, sizeof(PieIrExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_IR_EXPR_FIELD;
  expr->type = PIE_IR_TYPE_INT;
  expr->left = object;
  expr->field_offset = 0;
  if (field_name) {
    size_t len = strlen(field_name);
    expr->field_name = malloc(len + 1);
    if (expr->field_name) {
      memcpy(expr->field_name, field_name, len + 1);
    }
  }
  if (object) {
    if (object->type == PIE_IR_TYPE_STRUCT && object->struct_name) {
      size_t len = strlen(object->struct_name);
      expr->struct_name = malloc(len + 1);
      if (expr->struct_name) {
        memcpy(expr->struct_name, object->struct_name, len + 1);
      }
    }
  }
  return expr;
}

PieIrExpr *pie_ir_expr_tuple(size_t element_count) {
  PieIrExpr *expr = (PieIrExpr *)calloc(1, sizeof(PieIrExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_IR_EXPR_TUPLE;
  expr->type = PIE_IR_TYPE_TUPLE;
  if (element_count > 0) {
    expr->tuple_elements =
        (PieIrExpr **)calloc(element_count, sizeof(PieIrExpr *));
    if (!expr->tuple_elements) {
      free(expr);
      return NULL;
    }
  }
  expr->tuple_element_count = element_count;
  return expr;
}

int pie_ir_expr_tuple_add_element(PieIrExpr *tuple, PieIrExpr *element) {
  if (!tuple || tuple->kind != PIE_IR_EXPR_TUPLE) {
    return 0;
  }
  size_t new_count = tuple->tuple_element_count + 1;
  PieIrExpr **new_elements = (PieIrExpr **)realloc(
      tuple->tuple_elements, new_count * sizeof(PieIrExpr *));
  if (!new_elements) {
    return 0;
  }
  new_elements[new_count - 1] = element;
  tuple->tuple_elements = new_elements;
  tuple->tuple_element_count = new_count;
  return 1;
}

PieIrExpr *pie_ir_expr_list(size_t element_count) {
  PieIrExpr *expr = (PieIrExpr *)calloc(1, sizeof(PieIrExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_IR_EXPR_LIST;
  expr->type = PIE_IR_TYPE_LIST;
  if (element_count > 0) {
    expr->list_elements =
        (PieIrExpr **)calloc(element_count, sizeof(PieIrExpr *));
    if (!expr->list_elements) {
      free(expr);
      return NULL;
    }
  }
  expr->list_element_count = element_count;
  return expr;
}

int pie_ir_expr_list_add_element(PieIrExpr *list, PieIrExpr *element) {
  if (!list || list->kind != PIE_IR_EXPR_LIST) {
    return 0;
  }
  size_t new_count = list->list_element_count + 1;
  PieIrExpr **new_elements = (PieIrExpr **)realloc(
      list->list_elements, new_count * sizeof(PieIrExpr *));
  if (!new_elements) {
    return 0;
  }
  new_elements[new_count - 1] = element;
  list->list_elements = new_elements;
  list->list_element_count = new_count;
  return 1;
}

PieIrExpr *pie_ir_expr_index(PieIrExpr *object, PieIrExpr *index) {
  PieIrExpr *expr = (PieIrExpr *)calloc(1, sizeof(PieIrExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_IR_EXPR_INDEX;
  expr->type = PIE_IR_TYPE_INT;
  expr->left = object;
  expr->right = index;
  return expr;
}

PieIrExpr *pie_ir_expr_variant(const char *enum_name,
                               const char *variant_name) {
  PieIrExpr *expr = (PieIrExpr *)calloc(1, sizeof(PieIrExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_IR_EXPR_VARIANT;
  expr->type = PIE_IR_TYPE_ENUM;
  expr->variant_tag = 0;
  if (enum_name) {
    size_t len = strlen(enum_name);
    expr->enum_name = malloc(len + 1);
    if (expr->enum_name) {
      memcpy(expr->enum_name, enum_name, len + 1);
    }
  }
  if (variant_name) {
    size_t len = strlen(variant_name);
    expr->variant_name = malloc(len + 1);
    if (expr->variant_name) {
      memcpy(expr->variant_name, variant_name, len + 1);
    }
  }
  return expr;
}

PieIrExpr *pie_ir_expr_cast(PieIrExpr *inner, PieIrTypeKind target_type,
                            int target_width) {
  PieIrExpr *expr = (PieIrExpr *)calloc(1, sizeof(PieIrExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_IR_EXPR_CAST;
  expr->cast_inner = inner;
  expr->cast_target_type = target_type;
  expr->cast_target_width = target_width;
  expr->type = target_type;
  return expr;
}

PieIrExpr *pie_ir_expr_ternary(PieIrExpr *cond, PieIrExpr *true_expr,
                               PieIrExpr *false_expr) {
  PieIrExpr *expr = (PieIrExpr *)calloc(1, sizeof(PieIrExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_IR_EXPR_TERNARY;
  expr->left = cond;
  expr->right = true_expr;
  expr->ternary_false = false_expr;
  return expr;
}

PieIrExpr *pie_ir_expr_if(PieIrExpr *cond, PieIrExpr *then_expr,
                          PieIrExpr *else_expr) {
  PieIrExpr *expr = (PieIrExpr *)calloc(1, sizeof(PieIrExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_IR_EXPR_IF;
  expr->if_condition = cond;
  expr->if_then = then_expr;
  expr->if_else = else_expr;
  return expr;
}

PieIrExpr *pie_ir_expr_range(PieIrExpr *start, PieIrExpr *end, int inclusive) {
  PieIrExpr *expr = (PieIrExpr *)calloc(1, sizeof(PieIrExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_IR_EXPR_RANGE;
  expr->range_start = start;
  expr->range_end = end;
  expr->range_inclusive = inclusive;
  expr->type = PIE_IR_TYPE_STRUCT;
  return expr;
}

PieIrExpr *pie_ir_expr_map(void) {
  PieIrExpr *expr = (PieIrExpr *)calloc(1, sizeof(PieIrExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_IR_EXPR_MAP;
  expr->type = PIE_IR_TYPE_MAP;
  return expr;
}

int pie_ir_expr_map_add(PieIrExpr *map, PieIrExpr *key, PieIrExpr *value) {
  if (!map || !key || !value) {
    return 0;
  }
  size_t new_count = map->map_entry_count + 1;
  PieIrExpr **new_keys =
      (PieIrExpr **)realloc(map->map_keys, new_count * sizeof(PieIrExpr *));
  if (!new_keys) {
    return 0;
  }
  map->map_keys = new_keys;

  PieIrExpr **new_values =
      (PieIrExpr **)realloc(map->map_values, new_count * sizeof(PieIrExpr *));
  if (!new_values) {
    return 0;
  }
  map->map_values = new_values;

  map->map_keys[map->map_entry_count] = key;
  map->map_values[map->map_entry_count] = value;
  map->map_entry_count = new_count;
  return 1;
}

PieIrExpr *pie_ir_expr_match(PieIrExpr *target) {
  PieIrExpr *expr = (PieIrExpr *)calloc(1, sizeof(PieIrExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_IR_EXPR_MATCH;
  expr->match_expr_target = target;
  expr->match_expr_value_exprs = NULL;
  expr->match_expr_default_value = NULL;
  return expr;
}

PieIrExpr *pie_ir_expr_closure(void) {
  PieIrExpr *expr = (PieIrExpr *)calloc(1, sizeof(PieIrExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_IR_EXPR_CLOSURE;
  return expr;
}

PieIrExpr *pie_ir_expr_method_call(PieIrExpr *object, const char *method_name,
                                   PieIrTypeKind return_type) {
  PieIrExpr *expr = (PieIrExpr *)calloc(1, sizeof(PieIrExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_IR_EXPR_METHOD_CALL;
  expr->left = object;
  expr->type = return_type;
  if (method_name) {
    expr->method_name = ir_strdup(method_name);
  }
  return expr;
}

PieIrExpr *pie_ir_expr_closure_call(PieIrExpr *closure,
                                    PieIrTypeKind return_type) {
  PieIrExpr *expr = (PieIrExpr *)calloc(1, sizeof(PieIrExpr));
  if (!expr) {
    return NULL;
  }
  expr->kind = PIE_IR_EXPR_CLOSURE_CALL;
  expr->left = closure;
  expr->type = return_type;
  return expr;
}

const char *pie_ir_type_name(PieIrTypeKind type) {
  switch (type) {
  case PIE_IR_TYPE_UNKNOWN:
    return "unknown";
  case PIE_IR_TYPE_VOID:
    return "void";
  case PIE_IR_TYPE_INT:
    return "int";
  case PIE_IR_TYPE_FLOAT:
    return "float";
  case PIE_IR_TYPE_CHAR:
    return "char";
  case PIE_IR_TYPE_BYTE:
    return "byte";
  case PIE_IR_TYPE_BOOL:
    return "bool";
  case PIE_IR_TYPE_STRING:
    return "string";
  case PIE_IR_TYPE_REF:
    return "&";
  case PIE_IR_TYPE_REF_MUT:
    return "&mut";
  case PIE_IR_TYPE_RAW_PTR:
    return "*raw";
  case PIE_IR_TYPE_STRUCT:
    return "struct";
  case PIE_IR_TYPE_NULL:
    return "null";
  case PIE_IR_TYPE_NULLABLE:
    return "?";
  case PIE_IR_TYPE_TUPLE:
    return "tuple";
  case PIE_IR_TYPE_LIST:
    return "list";
  case PIE_IR_TYPE_MAP:
    return "map";
  case PIE_IR_TYPE_ENUM:
    return "enum";
  case PIE_IR_TYPE_CLOSURE:
    return "closure";
  case PIE_IR_TYPE_THREAD:
    return "thread";
  case PIE_IR_TYPE_MUTEX:
    return "mutex";
  case PIE_IR_TYPE_CHANNEL:
    return "channel";
  }
  return "unknown";
}
