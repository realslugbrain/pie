#define _GNU_SOURCE
#include "pie/core/sema/sema.h"

#include <stdlib.h>
#include <string.h>

static PieType ast_type_to_pie_type(PieAstTypeKind kind) {
  PieType type;
  memset(&type, 0, sizeof(type));
  switch (kind) {
  case PIE_AST_TYPE_INT:
    type.kind = PIE_TYPE_INT;
    break;
  case PIE_AST_TYPE_FLOAT:
    type.kind = PIE_TYPE_FLOAT;
    break;
  case PIE_AST_TYPE_STRING:
    type.kind = PIE_TYPE_STRING;
    break;
  case PIE_AST_TYPE_BOOL:
    type.kind = PIE_TYPE_BOOL;
    break;
  case PIE_AST_TYPE_CHAR:
    type.kind = PIE_TYPE_CHAR;
    break;
  case PIE_AST_TYPE_BYTE:
    type.kind = PIE_TYPE_BYTE;
    break;
  case PIE_AST_TYPE_VOID:
    type.kind = PIE_TYPE_VOID;
    break;
  case PIE_AST_TYPE_STRUCT:
    type.kind = PIE_TYPE_STRUCT;
    break;
  default:
    type.kind = PIE_TYPE_INT;
    break;
  }
  return type;
}

static int is_closure_param(const PieExpr *expr, const char *name) {
  for (size_t i = 0; i < expr->closure_param_count; i++) {
    if (strcmp(expr->closure_param_names[i], name) == 0) {
      return 1;
    }
  }
  return 0;
}

static int already_captured(const PieExpr *expr, const char *name) {
  for (size_t i = 0; i < expr->closure_capture_count; i++) {
    if (strcmp(expr->closure_capture_names[i], name) == 0) {
      return 1;
    }
  }
  return 0;
}

static void collect_captures_from_expr(PieSemaContext *ctx,
                                       const PieExpr *closure,
                                       const PieExpr *expr);

static void collect_captures_from_stmt(PieSemaContext *ctx,
                                       const PieExpr *closure,
                                       const PieStmt *stmt) {
  if (!stmt)
    return;
  if (stmt->expr)
    collect_captures_from_expr(ctx, closure, stmt->expr);
  if (stmt->target)
    collect_captures_from_expr(ctx, closure, stmt->target);
  for (size_t i = 0; i < stmt->arg_count; i++) {
    collect_captures_from_expr(ctx, closure, stmt->args[i].expr);
  }
  if (stmt->then_branch) {
    for (size_t i = 0; i < stmt->then_branch->stmt_count; i++) {
      collect_captures_from_stmt(ctx, closure, &stmt->then_branch->stmts[i]);
    }
  }
  if (stmt->else_branch) {
    for (size_t i = 0; i < stmt->else_branch->stmt_count; i++) {
      collect_captures_from_stmt(ctx, closure, &stmt->else_branch->stmts[i]);
    }
  }
  for (size_t i = 0; i < stmt->multi_count; i++) {
    collect_captures_from_expr(ctx, closure, stmt->multi_exprs[i]);
  }
}

static void collect_captures_from_expr(PieSemaContext *ctx,
                                       const PieExpr *closure,
                                       const PieExpr *expr) {
  if (!expr)
    return;

  if (expr->kind == PIE_EXPR_VAR && expr->name) {
    if (!is_closure_param(closure, expr->name) &&
        !already_captured(closure, expr->name)) {
      PieSymbolInfo info;
      if (ctx->api->find_symbol(ctx->sema, expr->name, &info)) {
        PieType param_type = info.type;

        size_t n = closure->closure_capture_count;

        char **new_names =
            (char **)realloc(((PieExpr *)closure)->closure_capture_names,
                             (n + 1) * sizeof(char *));
        if (!new_names) {
          return;
        }
        ((PieExpr *)closure)->closure_capture_names = new_names;

        PieAstTypeKind *new_types = (PieAstTypeKind *)realloc(
            ((PieExpr *)closure)->closure_capture_types,
            (n + 1) * sizeof(PieAstTypeKind));
        if (!new_types) {
          return;
        }
        ((PieExpr *)closure)->closure_capture_types = new_types;

        ((PieExpr *)closure)->closure_capture_names[n] = strdup(expr->name);

        switch (param_type.kind) {
        case PIE_TYPE_INT:
          ((PieExpr *)closure)->closure_capture_types[n] = PIE_AST_TYPE_INT;
          break;
        case PIE_TYPE_FLOAT:
          ((PieExpr *)closure)->closure_capture_types[n] = PIE_AST_TYPE_FLOAT;
          break;
        case PIE_TYPE_STRING:
          ((PieExpr *)closure)->closure_capture_types[n] = PIE_AST_TYPE_STRING;
          break;
        case PIE_TYPE_BOOL:
          ((PieExpr *)closure)->closure_capture_types[n] = PIE_AST_TYPE_BOOL;
          break;
        case PIE_TYPE_CHAR:
          ((PieExpr *)closure)->closure_capture_types[n] = PIE_AST_TYPE_CHAR;
          break;
        case PIE_TYPE_BYTE:
          ((PieExpr *)closure)->closure_capture_types[n] = PIE_AST_TYPE_BYTE;
          break;
        case PIE_TYPE_STRUCT:
          ((PieExpr *)closure)->closure_capture_types[n] = PIE_AST_TYPE_STRUCT;
          break;
        case PIE_TYPE_MAP:
          ((PieExpr *)closure)->closure_capture_types[n] = PIE_AST_TYPE_MAP;
          break;
        case PIE_TYPE_ENUM:
          ((PieExpr *)closure)->closure_capture_types[n] = PIE_AST_TYPE_ENUM;
          break;
        case PIE_TYPE_CLOSURE:
          ((PieExpr *)closure)->closure_capture_types[n] = PIE_AST_TYPE_CLOSURE;
          break;
        case PIE_TYPE_THREAD:
          ((PieExpr *)closure)->closure_capture_types[n] = PIE_AST_TYPE_THREAD;
          break;
        case PIE_TYPE_MUTEX:
          ((PieExpr *)closure)->closure_capture_types[n] = PIE_AST_TYPE_MUTEX;
          break;
        case PIE_TYPE_CHANNEL:
          ((PieExpr *)closure)->closure_capture_types[n] = PIE_AST_TYPE_CHANNEL;
          break;
        default:
          ((PieExpr *)closure)->closure_capture_types[n] = PIE_AST_TYPE_INFER;
          break;
        }
        ((PieExpr *)closure)->closure_capture_count++;
      }
    }
    return;
  }

  collect_captures_from_expr(ctx, closure, expr->left);
  collect_captures_from_expr(ctx, closure, expr->right);
  for (size_t i = 0; i < expr->call_arg_count; i++) {
    collect_captures_from_expr(ctx, closure, expr->call_args[i].expr);
  }
  for (size_t i = 0; i < expr->tuple_element_count; i++) {
    collect_captures_from_expr(ctx, closure, expr->tuple_elements[i]);
  }
  for (size_t i = 0; i < expr->list_element_count; i++) {
    collect_captures_from_expr(ctx, closure, expr->list_elements[i]);
  }
  for (size_t i = 0; i < expr->map_entry_count; i++) {
    collect_captures_from_expr(ctx, closure, expr->map_keys[i]);
    collect_captures_from_expr(ctx, closure, expr->map_values[i]);
  }
}

static void collect_captures(PieSemaContext *ctx, const PieExpr *closure) {
  if (!closure->closure_body)
    return;
  for (size_t i = 0; i < closure->closure_body->stmt_count; i++) {
    collect_captures_from_stmt(ctx, closure, &closure->closure_body->stmts[i]);
  }
}

PieSemaResult pie_feature_closures_sema_expr(PieSemaContext *ctx,
                                             const PieExpr *expr,
                                             PieType *out_type) {
  if (expr->kind != PIE_EXPR_CLOSURE) {
    return PIE_SEMA_NO_MATCH;
  }

  collect_captures(ctx, expr);

  if (!ctx->api->enter_scope(ctx->sema)) {
    return PIE_SEMA_ERROR;
  }

  for (size_t i = 0; i < expr->closure_capture_count; i++) {
    PieType cap_type = ast_type_to_pie_type(expr->closure_capture_types[i]);
    if (!ctx->api->declare_symbol(ctx->sema, expr->closure_capture_names[i],
                                  cap_type, 1)) {
      ctx->api->leave_scope(ctx->sema);
      return PIE_SEMA_ERROR;
    }
  }

  for (size_t i = 0; i < expr->closure_param_count; i++) {
    PieType param_type =
        ast_type_to_pie_type(expr->closure_param_types[i].kind);
    if (!ctx->api->declare_symbol(ctx->sema, expr->closure_param_names[i],
                                  param_type, 0)) {
      ctx->api->leave_scope(ctx->sema);
      return PIE_SEMA_ERROR;
    }
  }

  PieType prev_ret = ctx->api->current_return_type(ctx->sema);
  ctx->api->set_return_type(
      ctx->sema, ast_type_to_pie_type(expr->closure_return_type.kind));

  if (ctx->api->check_block(ctx->sema, expr->closure_body) != PIE_SEMA_OK) {
    ctx->api->set_return_type(ctx->sema, prev_ret);
    ctx->api->leave_scope(ctx->sema);
    return PIE_SEMA_ERROR;
  }

  ctx->api->set_return_type(ctx->sema, prev_ret);
  ctx->api->leave_scope(ctx->sema);

  out_type->kind = PIE_TYPE_CLOSURE;
  return PIE_SEMA_OK;
}