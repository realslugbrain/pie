#include "pie/core/borrow/borrowcheck.h"

#include <stdlib.h>
#include <string.h>

typedef enum BorrowExprUse {
  BORROW_EXPR_READ,
  BORROW_EXPR_CONSUME
} BorrowExprUse;

typedef enum BorrowEscapeKind {
  BORROW_ESCAPE_NONE,
  BORROW_ESCAPE_FUNCTION,
  BORROW_ESCAPE_REGION
} BorrowEscapeKind;

typedef struct BorrowSymbol {
  const char *name;
  PieAstTypeKind type;
  size_t region_depth;
  int moved;
  size_t shared_borrows;
  int mut_borrowed;
  int has_borrow_target;
  size_t borrow_target_index;
  int is_mut_ref;
} BorrowSymbol;

typedef struct BorrowFunction {
  const char *name;
  PieAstTypeKind return_type;
  const PieAstType *param_types;
  size_t param_count;
} BorrowFunction;

typedef struct PieBorrowcheck {
  BorrowSymbol *symbols;
  size_t symbol_count;
  size_t symbol_capacity;
  size_t *scope_marks;
  size_t scope_count;
  size_t scope_capacity;
  BorrowFunction *functions;
  size_t function_count;
  size_t function_capacity;
  size_t region_depth;
  PieDiagnosticBag *diag;
} PieBorrowcheck;

typedef struct BorrowStateSnapshot {
  int *moved;
  size_t *shared_borrows;
  int *mut_borrowed;
} BorrowStateSnapshot;

static int is_noncopy_type(PieAstTypeKind type) {
  return type == PIE_AST_TYPE_STRING || type == PIE_AST_TYPE_STRUCT ||
         type == PIE_AST_TYPE_LIST || type == PIE_AST_TYPE_MAP ||
         type == PIE_AST_TYPE_TUPLE || type == PIE_AST_TYPE_THREAD ||
         type == PIE_AST_TYPE_MUTEX || type == PIE_AST_TYPE_CHANNEL;
}

static int is_mut_ref_type(PieAstTypeKind type) {
  return type == PIE_AST_TYPE_REF_MUT;
}

static BorrowSymbol *find_symbol(PieBorrowcheck *check, const char *name) {
  for (size_t i = check->symbol_count; i > 0; i--) {
    BorrowSymbol *symbol = &check->symbols[i - 1];
    if (strcmp(symbol->name, name) == 0) {
      return symbol;
    }
  }
  return NULL;
}

static const BorrowFunction *find_function(const PieBorrowcheck *check,
                                           const char *name) {
  for (size_t i = 0; i < check->function_count; i++) {
    const BorrowFunction *function = &check->functions[i];
    if (strcmp(function->name, name) == 0) {
      return function;
    }
  }
  return NULL;
}

static int enter_scope(PieBorrowcheck *check) {
  if (check->scope_count == check->scope_capacity) {
    size_t next_capacity =
        check->scope_capacity ? check->scope_capacity * 2 : 16;
    size_t *next =
        (size_t *)realloc(check->scope_marks, next_capacity * sizeof(size_t));
    if (!next) {
      pie_diag_error(check->diag,
                     "out of memory while entering borrowcheck scope");
      return 0;
    }
    check->scope_marks = next;
    check->scope_capacity = next_capacity;
  }

  check->scope_marks[check->scope_count++] = check->symbol_count;
  return 1;
}

static void leave_scope(PieBorrowcheck *check) {
  if (check->scope_count == 0) {
    return;
  }
  size_t mark = check->scope_marks[--check->scope_count];
  for (size_t i = check->symbol_count; i > mark; i--) {
    BorrowSymbol *symbol = &check->symbols[i - 1];
    if (!symbol->has_borrow_target ||
        symbol->borrow_target_index >= check->symbol_count) {
      continue;
    }
    BorrowSymbol *target = &check->symbols[symbol->borrow_target_index];
    if (symbol->is_mut_ref) {
      target->mut_borrowed = 0;
    } else if (target->shared_borrows > 0) {
      target->shared_borrows--;
    }
  }
  check->symbol_count = mark;
}

static int declare_symbol(PieBorrowcheck *check, const char *name,
                          PieAstTypeKind type) {
  if (check->symbol_count == check->symbol_capacity) {
    size_t next_capacity =
        check->symbol_capacity ? check->symbol_capacity * 2 : 16;
    BorrowSymbol *next = (BorrowSymbol *)realloc(
        check->symbols, next_capacity * sizeof(BorrowSymbol));
    if (!next) {
      pie_diag_error(check->diag,
                     "out of memory while storing borrowcheck symbol");
      return 0;
    }
    check->symbols = next;
    check->symbol_capacity = next_capacity;
  }

  BorrowSymbol *symbol = &check->symbols[check->symbol_count++];
  symbol->name = name;
  symbol->type = type;
  symbol->region_depth = check->region_depth;
  symbol->moved = 0;
  symbol->shared_borrows = 0;
  symbol->mut_borrowed = 0;
  symbol->has_borrow_target = 0;
  symbol->borrow_target_index = 0;
  symbol->is_mut_ref = 0;
  return 1;
}

static int declare_reference_symbol(PieBorrowcheck *check, const char *name,
                                    PieAstTypeKind type, size_t target_index) {
  if (!declare_symbol(check, name, type)) {
    return 0;
  }
  BorrowSymbol *symbol = &check->symbols[check->symbol_count - 1];
  symbol->has_borrow_target = 1;
  symbol->borrow_target_index = target_index;
  symbol->is_mut_ref = is_mut_ref_type(type);
  return 1;
}

static int register_functions(PieBorrowcheck *check,
                              const PieProgram *program) {
  if (program->function_count == 0) {
    return 1;
  }

  check->functions =
      (BorrowFunction *)calloc(program->function_count, sizeof(BorrowFunction));
  if (!check->functions) {
    pie_diag_error(check->diag,
                   "out of memory while storing borrowcheck functions");
    return 0;
  }
  check->function_capacity = program->function_count;

  for (size_t i = 0; i < program->function_count; i++) {
    const PieFunction *ast_function = &program->functions[i];
    BorrowFunction *function = &check->functions[check->function_count++];
    function->name = ast_function->name;
    function->return_type = ast_function->return_type.kind;
    function->param_types = ast_function->param_types;
    function->param_count = ast_function->param_count;
  }
  return 1;
}

static void free_snapshot(BorrowStateSnapshot *snapshot) {
  free(snapshot->moved);
  free(snapshot->shared_borrows);
  free(snapshot->mut_borrowed);
  snapshot->moved = NULL;
  snapshot->shared_borrows = NULL;
  snapshot->mut_borrowed = NULL;
}

static int copy_state(PieBorrowcheck *check, size_t count,
                      BorrowStateSnapshot *out_snapshot) {
  memset(out_snapshot, 0, sizeof(*out_snapshot));
  out_snapshot->moved = (int *)calloc(count ? count : 1, sizeof(int));
  out_snapshot->shared_borrows =
      (size_t *)calloc(count ? count : 1, sizeof(size_t));
  out_snapshot->mut_borrowed = (int *)calloc(count ? count : 1, sizeof(int));
  if (!out_snapshot->moved || !out_snapshot->shared_borrows ||
      !out_snapshot->mut_borrowed) {
    free_snapshot(out_snapshot);
    pie_diag_error(check->diag,
                   "out of memory while storing borrowcheck branch state");
    return 0;
  }
  for (size_t i = 0; i < count; i++) {
    out_snapshot->moved[i] = check->symbols[i].moved;
    out_snapshot->shared_borrows[i] = check->symbols[i].shared_borrows;
    out_snapshot->mut_borrowed[i] = check->symbols[i].mut_borrowed;
  }
  return 1;
}

static void restore_state(PieBorrowcheck *check, size_t count,
                          const BorrowStateSnapshot *snapshot) {
  for (size_t i = 0; i < count; i++) {
    check->symbols[i].moved = snapshot->moved[i];
    check->symbols[i].shared_borrows = snapshot->shared_borrows[i];
    check->symbols[i].mut_borrowed = snapshot->mut_borrowed[i];
  }
}

static PieAstTypeKind check_expr(PieBorrowcheck *check, const PieExpr *expr,
                                 BorrowExprUse use);
static int check_stmt(PieBorrowcheck *check, const PieStmt *stmt);
static int check_block(PieBorrowcheck *check, const PieProgram *program);

static size_t symbol_index(PieBorrowcheck *check, const BorrowSymbol *symbol) {
  return (size_t)(symbol - check->symbols);
}

static int validate_borrow_target(PieBorrowcheck *check, BorrowSymbol *target,
                                  int is_mut_borrow) {
  if (target->moved) {
    pie_diag_errorf(check->diag, "use of moved value '%s'", target->name);
    return 0;
  }
  if (is_mut_borrow) {
    if (target->mut_borrowed) {
      pie_diag_errorf(check->diag, "cannot mutably borrow '%s' more than once",
                      target->name);
      return 0;
    }
    if (target->shared_borrows > 0) {
      pie_diag_errorf(check->diag,
                      "cannot mutably borrow '%s' while it is shared",
                      target->name);
      return 0;
    }
    return 1;
  }
  if (target->mut_borrowed) {
    pie_diag_errorf(check->diag,
                    "cannot shared borrow '%s' while it is mutably borrowed",
                    target->name);
    return 0;
  }
  return 1;
}

static PieAstTypeKind type_for_borrow(BorrowSymbol *target, int is_mut_borrow) {
  if (target->type == PIE_AST_TYPE_STRING ||
      target->type == PIE_AST_TYPE_STRUCT ||
      target->type == PIE_AST_TYPE_LIST || target->type == PIE_AST_TYPE_MAP ||
      target->type == PIE_AST_TYPE_TUPLE ||
      target->type == PIE_AST_TYPE_THREAD ||
      target->type == PIE_AST_TYPE_MUTEX ||
      target->type == PIE_AST_TYPE_CHANNEL) {
    return is_mut_borrow ? PIE_AST_TYPE_REF_MUT : PIE_AST_TYPE_REF;
  }
  return PIE_AST_TYPE_INFER;
}

static PieAstTypeKind check_borrow_expr(PieBorrowcheck *check,
                                        const PieExpr *expr, int persist,
                                        size_t *out_target_index) {
  int is_mut_borrow = strcmp(expr->op_text, "&mut") == 0;

  if (!expr->right) {
    return PIE_AST_TYPE_INFER;
  }

  if (expr->right->kind == PIE_EXPR_VAR) {
    BorrowSymbol *target = find_symbol(check, expr->right->name);
    if (!target) {
      return PIE_AST_TYPE_INFER;
    }
    PieAstTypeKind ref_type = type_for_borrow(target, is_mut_borrow);
    if (ref_type == PIE_AST_TYPE_INFER) {
      return ref_type;
    }
    if (!validate_borrow_target(check, target, is_mut_borrow)) {
      return ref_type;
    }

    if (is_mut_borrow) {
      target->mut_borrowed = 1;
    } else {
      target->shared_borrows++;
    }

    if (out_target_index) {
      *out_target_index = symbol_index(check, target);
    }
    if (!persist) {
      if (is_mut_borrow) {
        target->mut_borrowed = 0;
      } else if (target->shared_borrows > 0) {
        target->shared_borrows--;
      }
    }
    return ref_type;
  }

  if (expr->right->kind == PIE_EXPR_FIELD ||
      expr->right->kind == PIE_EXPR_INDEX) {
    const char *container_name = NULL;
    if (expr->right->kind == PIE_EXPR_FIELD && expr->right->left &&
        expr->right->left->kind == PIE_EXPR_VAR) {
      container_name = expr->right->left->name;
    } else if (expr->right->kind == PIE_EXPR_INDEX &&
               expr->right->index_object &&
               expr->right->index_object->kind == PIE_EXPR_VAR) {
      container_name = expr->right->index_object->name;
    }

    if (container_name) {
      BorrowSymbol *target = find_symbol(check, container_name);
      if (target) {
        if (target->moved) {
          pie_diag_errorf(check->diag, "use of moved value '%s'",
                          container_name);
          return PIE_AST_TYPE_INFER;
        }
        PieAstTypeKind ref_type = type_for_borrow(target, is_mut_borrow);
        if (ref_type == PIE_AST_TYPE_INFER) {
          return ref_type;
        }
        if (!validate_borrow_target(check, target, is_mut_borrow)) {
          return ref_type;
        }
        if (is_mut_borrow) {
          target->mut_borrowed = 1;
        } else {
          target->shared_borrows++;
        }
        if (out_target_index) {
          *out_target_index = symbol_index(check, target);
        }
        if (!persist) {
          if (is_mut_borrow) {
            target->mut_borrowed = 0;
          } else if (target->shared_borrows > 0) {
            target->shared_borrows--;
          }
        }
        return ref_type;
      }
    }
  }

  return PIE_AST_TYPE_INFER;
}

static BorrowEscapeKind escape_kind_for_target(const BorrowSymbol *target) {
  if (!target) {
    return BORROW_ESCAPE_NONE;
  }
  if (target->region_depth > 0) {
    return BORROW_ESCAPE_REGION;
  }
  return BORROW_ESCAPE_FUNCTION;
}

static BorrowEscapeKind expr_borrows_escaping_local(PieBorrowcheck *check,
                                                    const PieExpr *expr,
                                                    const char **out_name) {
  if (!expr) {
    return BORROW_ESCAPE_NONE;
  }

  if (expr->kind == PIE_EXPR_UNARY &&
      (strcmp(expr->op_text, "&") == 0 || strcmp(expr->op_text, "&mut") == 0) &&
      expr->right) {
    if (expr->right->kind == PIE_EXPR_VAR) {
      BorrowSymbol *target = find_symbol(check, expr->right->name);
      BorrowEscapeKind escape = escape_kind_for_target(target);
      if (escape != BORROW_ESCAPE_NONE) {
        if (out_name) {
          *out_name = target->name;
        }
        return escape;
      }
    }
    if (expr->right->kind == PIE_EXPR_FIELD && expr->right->left &&
        expr->right->left->kind == PIE_EXPR_VAR) {
      BorrowSymbol *target = find_symbol(check, expr->right->left->name);
      BorrowEscapeKind escape = escape_kind_for_target(target);
      if (escape != BORROW_ESCAPE_NONE) {
        if (out_name) {
          *out_name = target->name;
        }
        return escape;
      }
    }
    if (expr->right->kind == PIE_EXPR_INDEX && expr->right->index_object &&
        expr->right->index_object->kind == PIE_EXPR_VAR) {
      BorrowSymbol *target =
          find_symbol(check, expr->right->index_object->name);
      BorrowEscapeKind escape = escape_kind_for_target(target);
      if (escape != BORROW_ESCAPE_NONE) {
        if (out_name) {
          *out_name = target->name;
        }
        return escape;
      }
    }
  }

  if (expr->kind == PIE_EXPR_VAR) {
    BorrowSymbol *symbol = find_symbol(check, expr->name);
    if (symbol && symbol->has_borrow_target &&
        symbol->borrow_target_index < check->symbol_count) {
      BorrowSymbol *target = &check->symbols[symbol->borrow_target_index];
      BorrowEscapeKind escape = escape_kind_for_target(target);
      if (escape != BORROW_ESCAPE_NONE) {
        if (out_name) {
          *out_name = target->name;
        }
        return escape;
      }
    }
  }

  return BORROW_ESCAPE_NONE;
}

static PieAstTypeKind check_var_expr(PieBorrowcheck *check, const PieExpr *expr,
                                     BorrowExprUse use) {
  BorrowSymbol *symbol = find_symbol(check, expr->name);
  if (!symbol) {
    return PIE_AST_TYPE_INFER;
  }

  if (symbol->moved) {
    pie_diag_errorf(check->diag, "use of moved value '%s'", expr->name);
    return symbol->type;
  }

  if (use == BORROW_EXPR_READ && symbol->mut_borrowed) {
    pie_diag_errorf(check->diag, "cannot use '%s' while it is mutably borrowed",
                    expr->name);
    return symbol->type;
  }

  if (use == BORROW_EXPR_CONSUME &&
      (symbol->mut_borrowed || symbol->shared_borrows > 0)) {
    pie_diag_errorf(check->diag, "cannot move '%s' while it is borrowed",
                    expr->name);
    return symbol->type;
  }

  if (use == BORROW_EXPR_CONSUME && is_noncopy_type(symbol->type)) {
    symbol->moved = 1;
  }
  return symbol->type;
}

static PieAstTypeKind check_call_expr(PieBorrowcheck *check,
                                      const PieExpr *expr) {
  const BorrowFunction *function = find_function(check, expr->call_name);

  for (size_t i = 0; i < expr->call_arg_count; i++) {
    BorrowExprUse arg_use = BORROW_EXPR_READ;
    if (function && i < function->param_count &&
        is_noncopy_type(function->param_types[i].kind)) {
      arg_use = BORROW_EXPR_CONSUME;
    }
    check_expr(check, expr->call_args[i].expr, arg_use);
  }

  return function ? function->return_type : PIE_AST_TYPE_INFER;
}

static PieAstTypeKind check_expr(PieBorrowcheck *check, const PieExpr *expr,
                                 BorrowExprUse use) {
  if (!expr) {
    return PIE_AST_TYPE_VOID;
  }

  switch (expr->kind) {
  case PIE_EXPR_INT:
    return PIE_AST_TYPE_INT;
  case PIE_EXPR_FLOAT:
    return PIE_AST_TYPE_FLOAT;
  case PIE_EXPR_CHAR:
    return PIE_AST_TYPE_CHAR;
  case PIE_EXPR_BOOL:
    return PIE_AST_TYPE_BOOL;
  case PIE_EXPR_STRING:
    return PIE_AST_TYPE_STRING;
  case PIE_EXPR_NULL:
    return PIE_AST_TYPE_INFER;
  case PIE_EXPR_VAR:
    return check_var_expr(check, expr, use);
  case PIE_EXPR_CALL:
    return check_call_expr(check, expr);
  case PIE_EXPR_BINARY:
    check_expr(check, expr->left, BORROW_EXPR_READ);
    check_expr(check, expr->right, BORROW_EXPR_READ);
    if (strcmp(expr->op_text, "==") == 0 || strcmp(expr->op_text, "!=") == 0 ||
        strcmp(expr->op_text, "<") == 0 || strcmp(expr->op_text, "<=") == 0 ||
        strcmp(expr->op_text, ">") == 0 || strcmp(expr->op_text, ">=") == 0 ||
        strcmp(expr->op_text, "and") == 0 || strcmp(expr->op_text, "or") == 0) {
      return PIE_AST_TYPE_BOOL;
    }
    return PIE_AST_TYPE_INT;
  case PIE_EXPR_UNARY:
    if (strcmp(expr->op_text, "&raw") == 0) {
      check_expr(check, expr->right, BORROW_EXPR_READ);
      return PIE_AST_TYPE_RAW_PTR;
    }
    if (strcmp(expr->op_text, "&") == 0 || strcmp(expr->op_text, "&mut") == 0) {
      return check_borrow_expr(check, expr, 0, NULL);
    }
    if (strcmp(expr->op_text, "not") == 0) {
      check_expr(check, expr->right, BORROW_EXPR_READ);
      return PIE_AST_TYPE_BOOL;
    }
    return check_expr(check, expr->right, BORROW_EXPR_READ);
  case PIE_EXPR_NEW:
    return PIE_AST_TYPE_INFER;
  case PIE_EXPR_FIELD: {
    check_expr(check, expr->left, BORROW_EXPR_READ);
    if (expr->left && expr->left->kind == PIE_EXPR_VAR) {
      BorrowSymbol *container = find_symbol(check, expr->left->name);
      if (container && container->moved) {
        pie_diag_errorf(check->diag, "cannot access field of moved value '%s'",
                        expr->left->name);
        return PIE_AST_TYPE_INFER;
      }
    }
    return PIE_AST_TYPE_INFER;
  }
  case PIE_EXPR_INDEX: {
    check_expr(check, expr->index_object, BORROW_EXPR_READ);
    check_expr(check, expr->index_expr, BORROW_EXPR_READ);
    if (expr->index_object && expr->index_object->kind == PIE_EXPR_VAR) {
      BorrowSymbol *container = find_symbol(check, expr->index_object->name);
      if (container && container->moved) {
        pie_diag_errorf(check->diag, "cannot index into moved value '%s'",
                        expr->index_object->name);
        return PIE_AST_TYPE_INFER;
      }
    }
    return PIE_AST_TYPE_INFER;
  }
  case PIE_EXPR_LIST:
    for (size_t i = 0; i < expr->list_element_count; i++) {
      check_expr(check, expr->list_elements[i], BORROW_EXPR_READ);
    }
    return PIE_AST_TYPE_LIST;
  case PIE_EXPR_MAP:
    for (size_t i = 0; i < expr->map_entry_count; i++) {
      check_expr(check, expr->map_keys[i], BORROW_EXPR_READ);
      check_expr(check, expr->map_values[i], BORROW_EXPR_READ);
    }
    return PIE_AST_TYPE_MAP;
  case PIE_EXPR_TUPLE:
    for (size_t i = 0; i < expr->tuple_element_count; i++) {
      check_expr(check, expr->tuple_elements[i], BORROW_EXPR_READ);
    }
    return PIE_AST_TYPE_TUPLE;
  case PIE_EXPR_THREAD_CALL:
    for (size_t i = 0; i < expr->call_arg_count; i++) {
      check_expr(check, expr->call_args[i].expr, BORROW_EXPR_READ);
    }
    return PIE_AST_TYPE_THREAD;
  case PIE_EXPR_METHOD_CALL:
    if (expr->left) {
      check_expr(check, expr->left, BORROW_EXPR_READ);
    }
    for (size_t i = 0; i < expr->call_arg_count; i++) {
      check_expr(check, expr->call_args[i].expr, BORROW_EXPR_READ);
    }
    return PIE_AST_TYPE_INFER;
  case PIE_EXPR_CLOSURE:
    if (expr->closure_capture_names) {
      for (size_t i = 0; i < expr->closure_capture_count; i++) {
        BorrowSymbol *cap = find_symbol(check, expr->closure_capture_names[i]);
        if (cap && cap->moved) {
          pie_diag_errorf(check->diag,
                          "cannot capture moved value '%s' in closure",
                          expr->closure_capture_names[i]);
          return PIE_AST_TYPE_INFER;
        }
        if (cap && cap->mut_borrowed) {
          pie_diag_errorf(
              check->diag,
              "cannot capture mutably borrowed value '%s' in closure",
              expr->closure_capture_names[i]);
          return PIE_AST_TYPE_INFER;
        }
      }
    }
    return PIE_AST_TYPE_CLOSURE;
  case PIE_EXPR_IF:
    check_expr(check, expr->if_condition, BORROW_EXPR_READ);
    if (expr->if_then) {
      check_expr(check, expr->if_then, BORROW_EXPR_READ);
    }
    if (expr->if_else) {
      check_expr(check, expr->if_else, BORROW_EXPR_READ);
    }
    return PIE_AST_TYPE_INFER;
  case PIE_EXPR_TERNARY:
    check_expr(check, expr->left, BORROW_EXPR_READ);
    if (expr->if_then) {
      check_expr(check, expr->if_then, BORROW_EXPR_READ);
    }
    if (expr->ternary_false) {
      check_expr(check, expr->ternary_false, BORROW_EXPR_READ);
    }
    return PIE_AST_TYPE_INFER;
  case PIE_EXPR_TRY:
    check_expr(check, expr->left, BORROW_EXPR_READ);
    return PIE_AST_TYPE_INFER;
  case PIE_EXPR_POSTFIX:
    if (expr->left) {
      check_expr(check, expr->left, BORROW_EXPR_READ);
    }
    return PIE_AST_TYPE_INFER;
  case PIE_EXPR_STRING_INTERP:
    for (size_t i = 0; i < expr->interp_part_count; i++) {
      if (expr->interp_exprs && expr->interp_exprs[i]) {
        check_expr(check, expr->interp_exprs[i], BORROW_EXPR_READ);
      }
    }
    return PIE_AST_TYPE_STRING;
  case PIE_EXPR_CAST:
    check_expr(check, expr->cast_inner, BORROW_EXPR_READ);
    return PIE_AST_TYPE_INFER;
  case PIE_EXPR_VARIANT:
    for (size_t i = 0; i < expr->call_arg_count; i++) {
      check_expr(check, expr->call_args[i].expr, BORROW_EXPR_READ);
    }
    return PIE_AST_TYPE_INFER;
  case PIE_EXPR_RANGE:
    if (expr->range_start) {
      check_expr(check, expr->range_start, BORROW_EXPR_READ);
    }
    if (expr->range_end) {
      check_expr(check, expr->range_end, BORROW_EXPR_READ);
    }
    return PIE_AST_TYPE_INFER;
  case PIE_EXPR_MATCH:
    check_expr(check, expr->match_expr_target, BORROW_EXPR_READ);
    for (size_t i = 0; i < expr->match_expr_case_count; i++) {
      if (expr->match_expr_case_bodies && expr->match_expr_case_bodies[i]) {
        check_block(check, expr->match_expr_case_bodies[i]);
      }
    }
    if (expr->match_expr_default) {
      check_block(check, expr->match_expr_default);
    }
    return PIE_AST_TYPE_INFER;
  case PIE_EXPR_MAYBE:
    check_expr(check, expr->left, BORROW_EXPR_READ);
    return PIE_AST_TYPE_INFER;
  default:
    break;
  }

  return PIE_AST_TYPE_INFER;
}

static int capture_branch_state(PieBorrowcheck *check,
                                const PieProgram *program, size_t parent_count,
                                const BorrowStateSnapshot *before,
                                BorrowStateSnapshot *out_after) {
  restore_state(check, parent_count, before);
  if (!check_block(check, program)) {
    return 0;
  }

  if (!copy_state(check, parent_count, out_after)) {
    return 0;
  }
  restore_state(check, parent_count, before);
  return 1;
}

static int merge_branch_states(PieBorrowcheck *check, size_t parent_count,
                               const BorrowStateSnapshot *before,
                               const BorrowStateSnapshot *a,
                               const BorrowStateSnapshot *b) {
  for (size_t i = 0; i < parent_count; i++) {
    check->symbols[i].moved = before->moved[i] || a->moved[i] || b->moved[i];
    size_t max_shared = a->shared_borrows[i] > b->shared_borrows[i]
                            ? a->shared_borrows[i]
                            : b->shared_borrows[i];
    check->symbols[i].shared_borrows = before->shared_borrows[i] > max_shared
                                           ? before->shared_borrows[i]
                                           : max_shared;
    check->symbols[i].mut_borrowed =
        before->mut_borrowed[i] || a->mut_borrowed[i] || b->mut_borrowed[i];
  }
  return 1;
}

static int check_if_stmt(PieBorrowcheck *check, const PieStmt *stmt) {
  check_expr(check, stmt->expr, BORROW_EXPR_READ);

  size_t parent_count = check->symbol_count;
  BorrowStateSnapshot before;
  if (!copy_state(check, parent_count, &before)) {
    return 0;
  }

  BorrowStateSnapshot then_after;
  BorrowStateSnapshot else_after;
  memset(&then_after, 0, sizeof(then_after));
  memset(&else_after, 0, sizeof(else_after));
  int ok = capture_branch_state(check, stmt->then_branch, parent_count, &before,
                                &then_after);
  if (ok && stmt->else_branch) {
    ok = capture_branch_state(check, stmt->else_branch, parent_count, &before,
                              &else_after);
  } else if (ok) {
    if (!copy_state(check, parent_count, &else_after)) {
      ok = 0;
    }
  }

  if (ok) {
    merge_branch_states(check, parent_count, &before, &then_after, &else_after);
  }

  free_snapshot(&before);
  free_snapshot(&then_after);
  free_snapshot(&else_after);
  return ok;
}

static int check_while_stmt(PieBorrowcheck *check, const PieStmt *stmt) {
  check_expr(check, stmt->expr, BORROW_EXPR_READ);

  size_t parent_count = check->symbol_count;
  BorrowStateSnapshot before;
  if (!copy_state(check, parent_count, &before)) {
    return 0;
  }

  BorrowStateSnapshot body_after;
  memset(&body_after, 0, sizeof(body_after));
  int ok = capture_branch_state(check, stmt->then_branch, parent_count, &before,
                                &body_after);
  if (ok) {
    for (size_t i = 0; i < parent_count; i++) {
      check->symbols[i].moved = before.moved[i] || body_after.moved[i];
      check->symbols[i].shared_borrows =
          before.shared_borrows[i] > body_after.shared_borrows[i]
              ? before.shared_borrows[i]
              : body_after.shared_borrows[i];
      check->symbols[i].mut_borrowed =
          before.mut_borrowed[i] || body_after.mut_borrowed[i];
    }
  }

  free_snapshot(&before);
  free_snapshot(&body_after);
  return ok;
}

static int check_region_stmt(PieBorrowcheck *check, const PieStmt *stmt) {
  check->region_depth++;
  int ok = check_block(check, stmt->then_branch);
  check->region_depth--;
  return ok;
}

static int check_match_stmt(PieBorrowcheck *check, const PieStmt *stmt) {
  check_expr(check, stmt->match_target, BORROW_EXPR_READ);

  for (size_t i = 0; i < stmt->match_case_count; i++) {
    if (stmt->match_case_bindings && stmt->match_case_bindings[i]) {
      for (size_t j = 0; j < stmt->match_case_binding_counts[i]; j++) {
        declare_symbol(check, stmt->match_case_bindings[i][j],
                       PIE_AST_TYPE_INFER);
      }
    }
    if (stmt->match_case_bodies && stmt->match_case_bodies[i]) {
      if (!check_block(check, stmt->match_case_bodies[i])) {
        return 0;
      }
    }
  }
  if (stmt->match_default) {
    if (!check_block(check, stmt->match_default)) {
      return 0;
    }
  }
  return !check->diag->has_error;
}

static int check_stmt(PieBorrowcheck *check, const PieStmt *stmt) {
  switch (stmt->kind) {
  case PIE_STMT_PRINT:
    for (size_t i = 0; i < stmt->arg_count; i++) {
      if (!stmt->args[i].is_string) {
        check_expr(check, stmt->args[i].expr, BORROW_EXPR_READ);
      }
    }
    return !check->diag->has_error;
  case PIE_STMT_EXPR:
    check_expr(check, stmt->expr, BORROW_EXPR_CONSUME);
    return !check->diag->has_error;
  case PIE_STMT_LET: {
    size_t borrow_target_index = 0;
    int is_persistent_borrow = stmt->expr &&
                               stmt->expr->kind == PIE_EXPR_UNARY &&
                               (strcmp(stmt->expr->op_text, "&") == 0 ||
                                strcmp(stmt->expr->op_text, "&mut") == 0);
    PieAstTypeKind init_type =
        is_persistent_borrow
            ? check_borrow_expr(check, stmt->expr, 1, &borrow_target_index)
            : check_expr(check, stmt->expr, BORROW_EXPR_CONSUME);
    if (check->diag->has_error) {
      return 0;
    }
    PieAstTypeKind symbol_type =
        stmt->type_annotation.kind == PIE_AST_TYPE_INFER
            ? init_type
            : stmt->type_annotation.kind;
    if (is_persistent_borrow) {
      return declare_reference_symbol(check, stmt->name, symbol_type,
                                      borrow_target_index);
    }
    return declare_symbol(check, stmt->name, symbol_type);
  }
  case PIE_STMT_ASSIGN: {
    BorrowSymbol *target = find_symbol(check, stmt->name);
    if (target && (target->mut_borrowed || target->shared_borrows > 0)) {
      pie_diag_errorf(check->diag, "cannot assign to '%s' while it is borrowed",
                      stmt->name);
      return 0;
    }
    check_expr(check, stmt->expr, BORROW_EXPR_CONSUME);
    if (target) {
      target->moved = 0;
    }
    return !check->diag->has_error;
  }
  case PIE_STMT_ASSIGN_MULTI:
    for (size_t i = 0; i < stmt->multi_count; i++) {
      BorrowSymbol *target = find_symbol(check, stmt->multi_names[i]);
      if (target && (target->mut_borrowed || target->shared_borrows > 0)) {
        pie_diag_errorf(check->diag,
                        "cannot assign to '%s' while it is borrowed",
                        stmt->multi_names[i]);
        return 0;
      }
      check_expr(check, stmt->multi_exprs[i], BORROW_EXPR_CONSUME);
      if (target) {
        target->moved = 0;
      }
      if (check->diag->has_error) {
        return 0;
      }
    }
    return !check->diag->has_error;
  case PIE_STMT_RAW_STORE:
    check_expr(check, stmt->target, BORROW_EXPR_READ);
    check_expr(check, stmt->expr, BORROW_EXPR_CONSUME);
    return !check->diag->has_error;
  case PIE_STMT_RETURN: {
    const char *name = NULL;
    BorrowEscapeKind escape =
        expr_borrows_escaping_local(check, stmt->expr, &name);
    if (escape == BORROW_ESCAPE_REGION) {
      pie_diag_errorf(check->diag, "borrow of region local '%s' escapes region",
                      name ? name : "<unknown>");
      return 0;
    }
    if (escape == BORROW_ESCAPE_FUNCTION) {
      pie_diag_errorf(check->diag, "borrow of local '%s' escapes function",
                      name ? name : "<unknown>");
      return 0;
    }
    check_expr(check, stmt->expr, BORROW_EXPR_CONSUME);
    return !check->diag->has_error;
  }
  case PIE_STMT_IF:
    return check_if_stmt(check, stmt) && !check->diag->has_error;
  case PIE_STMT_WHILE:
    return check_while_stmt(check, stmt) && !check->diag->has_error;
  case PIE_STMT_FOR:
    return check_while_stmt(check, stmt) && !check->diag->has_error;
  case PIE_STMT_REGION:
    return check_region_stmt(check, stmt) && !check->diag->has_error;
  case PIE_STMT_UNSAFE:
    return check_block(check, stmt->then_branch) && !check->diag->has_error;
  case PIE_STMT_BLOCK:
    return check_block(check, stmt->then_branch) && !check->diag->has_error;
  case PIE_STMT_BREAK:
  case PIE_STMT_CONTINUE:
  case PIE_STMT_PASS:
    return 1;
  case PIE_STMT_STRUCT:
    return 1;
  case PIE_STMT_ENUM:
    return 1;
  case PIE_STMT_CONST:
    if (stmt->const_def && stmt->const_def->value) {
      check_expr(check, stmt->const_def->value, BORROW_EXPR_READ);
    }
    if (stmt->const_def) {
      return declare_symbol(check, stmt->const_def->name, PIE_AST_TYPE_INFER);
    }
    return 1;
  case PIE_STMT_MATCH:
    return check_match_stmt(check, stmt) && !check->diag->has_error;
  case PIE_STMT_FIELD_ASSIGN: {
    check_expr(check, stmt->field_target, BORROW_EXPR_READ);
    const PieExpr *container_expr = stmt->field_target;
    if (container_expr && container_expr->kind == PIE_EXPR_FIELD) {
      container_expr = container_expr->left;
    }
    if (container_expr && container_expr->kind == PIE_EXPR_VAR) {
      BorrowSymbol *container = find_symbol(check, container_expr->name);
      if (container && container->moved) {
        pie_diag_errorf(check->diag, "cannot assign field of moved value '%s'",
                        container_expr->name);
        return 0;
      }
      if (container &&
          (container->mut_borrowed || container->shared_borrows > 0)) {
        pie_diag_errorf(check->diag,
                        "cannot assign to field of '%s' while it is borrowed",
                        container_expr->name);
        return 0;
      }
    }
    check_expr(check, stmt->expr, BORROW_EXPR_CONSUME);
    return !check->diag->has_error;
  }
  case PIE_STMT_INDEX_ASSIGN: {
    check_expr(check, stmt->index_target, BORROW_EXPR_READ);
    if (stmt->index_target && stmt->index_target->kind == PIE_EXPR_VAR) {
      BorrowSymbol *container = find_symbol(check, stmt->index_target->name);
      if (container && container->moved) {
        pie_diag_errorf(check->diag, "cannot assign index of moved value '%s'",
                        stmt->index_target->name);
        return 0;
      }
      if (container &&
          (container->mut_borrowed || container->shared_borrows > 0)) {
        pie_diag_errorf(check->diag,
                        "cannot assign to index of '%s' while it is borrowed",
                        stmt->index_target->name);
        return 0;
      }
    }
    check_expr(check, stmt->index_expr, BORROW_EXPR_READ);
    check_expr(check, stmt->expr, BORROW_EXPR_CONSUME);
    return !check->diag->has_error;
  }
  case PIE_STMT_DEFER:
    check_expr(check, stmt->expr, BORROW_EXPR_READ);
    return !check->diag->has_error;
  default:
    break;
  }

  return 1;
}

static int check_block(PieBorrowcheck *check, const PieProgram *program) {
  if (!program) {
    return 1;
  }
  if (!enter_scope(check)) {
    return 0;
  }

  int ok = 1;
  for (size_t i = 0; i < program->stmt_count; i++) {
    if (!check_stmt(check, &program->stmts[i])) {
      ok = 0;
      break;
    }
  }

  leave_scope(check);
  return ok;
}

static int check_function(PieBorrowcheck *check, const PieFunction *function) {
  if (!enter_scope(check)) {
    return 0;
  }

  int ok = 1;
  for (size_t i = 0; i < function->param_count; i++) {
    if (!declare_symbol(check, function->param_names[i],
                        function->param_types[i].kind)) {
      ok = 0;
      break;
    }
  }

  for (size_t i = 0; ok && i < function->body->stmt_count; i++) {
    if (!check_stmt(check, &function->body->stmts[i])) {
      ok = 0;
      break;
    }
  }

  leave_scope(check);
  return ok;
}

int pie_borrowcheck_program(const PieProgram *program, PieDiagnosticBag *diag) {
  PieBorrowcheck check;
  memset(&check, 0, sizeof(check));
  check.diag = diag;

  int ok = register_functions(&check, program);
  for (size_t i = 0; ok && i < program->stmt_count; i++) {
    if (!check_stmt(&check, &program->stmts[i])) {
      ok = 0;
    }
  }
  for (size_t i = 0; ok && i < program->function_count; i++) {
    if (!check_function(&check, &program->functions[i])) {
      ok = 0;
    }
  }

  free(check.symbols);
  free(check.scope_marks);
  free(check.functions);
  return ok && !diag->has_error;
}
