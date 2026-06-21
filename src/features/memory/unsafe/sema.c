#include "pie/core/sema/sema.h"

#include <string.h>

static int compatible_width(PieTypeKind kind, int a, int b) {
  if (a == b || b == PIE_WIDTH_INFER) {
    return 1;
  }
  if ((kind == PIE_TYPE_INT || kind == PIE_TYPE_FLOAT) &&
      ((a == PIE_WIDTH_INFER && b == PIE_WIDTH_64) ||
       (a == PIE_WIDTH_64 && b == PIE_WIDTH_INFER))) {
    return 1;
  }
  return 0;
}

static int raw_store_value_assignable(PieType target, PieType value) {
  if (target.kind == value.kind) {
    return compatible_width(target.kind, target.type_width, value.type_width);
  }
  return target.kind == PIE_TYPE_BYTE && value.kind == PIE_TYPE_INT;
}

static int is_raw_pointer_arithmetic_op(const PieExpr *expr) {
  return expr->kind == PIE_EXPR_BINARY && (expr->op == '+' || expr->op == '-');
}

static int is_raw_pointer_type(PieType type) {
  return type.kind == PIE_TYPE_RAW_PTR;
}

static PieSemaResult sema_raw_pointer_arithmetic(PieSemaContext *ctx,
                                                 const PieExpr *expr,
                                                 PieType *out_type) {
  PieType left;
  PieType right;
  if (ctx->api->check_expr(ctx->sema, expr->left, &left) != PIE_SEMA_OK ||
      ctx->api->check_expr(ctx->sema, expr->right, &right) != PIE_SEMA_OK) {
    out_type->kind = PIE_TYPE_ERROR;
    return PIE_SEMA_ERROR;
  }

  int left_raw = is_raw_pointer_type(left);
  int right_raw = is_raw_pointer_type(right);
  if (!left_raw && !right_raw) {
    return PIE_SEMA_NO_MATCH;
  }

  if (!ctx->api->in_unsafe(ctx->sema)) {
    ctx->api->error(ctx->sema, "raw pointer arithmetic requires unsafe");
    out_type->kind = PIE_TYPE_ERROR;
    return PIE_SEMA_ERROR;
  }

  if (left_raw && right_raw) {
    ctx->api->error(
        ctx->sema,
        "raw pointer arithmetic between two pointers is not implemented");
    out_type->kind = PIE_TYPE_ERROR;
    return PIE_SEMA_ERROR;
  }

  if (right_raw && expr->op == '-') {
    ctx->api->error(ctx->sema,
                    "raw pointer subtraction requires pointer - int");
    out_type->kind = PIE_TYPE_ERROR;
    return PIE_SEMA_ERROR;
  }

  PieType pointer_type = left_raw ? left : right;
  PieType offset_type = left_raw ? right : left;
  if (offset_type.kind != PIE_TYPE_INT) {
    ctx->api->errorf(ctx->sema,
                     "raw pointer arithmetic offset must be int, got %s",
                     ctx->api->type_name(offset_type));
    out_type->kind = PIE_TYPE_ERROR;
    return PIE_SEMA_ERROR;
  }

  *out_type = pointer_type;
  return PIE_SEMA_OK;
}

PieSemaResult pie_feature_unsafe_sema_stmt(PieSemaContext *ctx,
                                           const PieStmt *stmt) {
  if (stmt->kind == PIE_STMT_UNSAFE) {
    ctx->api->enter_unsafe(ctx->sema);
    PieSemaResult result = ctx->api->check_block(ctx->sema, stmt->then_branch);
    ctx->api->leave_unsafe(ctx->sema);
    return result;
  }

  if (stmt->kind != PIE_STMT_RAW_STORE) {
    return PIE_SEMA_NO_MATCH;
  }

  if (!ctx->api->in_unsafe(ctx->sema)) {
    ctx->api->error(ctx->sema, "raw pointer store requires unsafe");
    return PIE_SEMA_ERROR;
  }

  PieType ptr_type;
  if (!stmt->target ||
      ctx->api->check_expr(ctx->sema, stmt->target, &ptr_type) != PIE_SEMA_OK) {
    return PIE_SEMA_ERROR;
  }
  if (ptr_type.kind != PIE_TYPE_RAW_PTR) {
    ctx->api->errorf(ctx->sema, "raw pointer store target must be *raw, got %s",
                     ctx->api->type_name(ptr_type));
    return PIE_SEMA_ERROR;
  }

  PieType value_type;
  if (!stmt->expr ||
      ctx->api->check_expr(ctx->sema, stmt->expr, &value_type) != PIE_SEMA_OK) {
    return PIE_SEMA_ERROR;
  }

  PieType pointee;
  memset(&pointee, 0, sizeof(pointee));
  pointee.kind = ptr_type.raw_pointee_kind;
  pointee.type_width = ptr_type.raw_pointee_width;
  if (!raw_store_value_assignable(pointee, value_type)) {
    ctx->api->errorf(ctx->sema, "raw pointer store requires %s value, got %s",
                     ctx->api->type_name(pointee),
                     ctx->api->type_name(value_type));
    return PIE_SEMA_ERROR;
  }
  return PIE_SEMA_OK;
}

PieSemaResult pie_feature_unsafe_sema_expr(PieSemaContext *ctx,
                                           const PieExpr *expr,
                                           PieType *out_type) {
  if (is_raw_pointer_arithmetic_op(expr)) {
    return sema_raw_pointer_arithmetic(ctx, expr, out_type);
  }

  if (expr->kind != PIE_EXPR_UNARY) {
    return PIE_SEMA_NO_MATCH;
  }

  if (strcmp(expr->op_text, "&raw") == 0) {
    if (!ctx->api->in_unsafe(ctx->sema)) {
      ctx->api->error(ctx->sema, "raw pointer address-of requires unsafe");
      out_type->kind = PIE_TYPE_ERROR;
      return PIE_SEMA_ERROR;
    }

    if (!expr->right || expr->right->kind != PIE_EXPR_VAR) {
      ctx->api->error(
          ctx->sema,
          "raw pointer address-of currently requires a local variable");
      out_type->kind = PIE_TYPE_ERROR;
      return PIE_SEMA_ERROR;
    }

    PieSymbolInfo symbol;
    if (!ctx->api->find_symbol(ctx->sema, expr->right->name, &symbol)) {
      ctx->api->errorf(ctx->sema, "undefined variable '%s'", expr->right->name);
      out_type->kind = PIE_TYPE_ERROR;
      return PIE_SEMA_ERROR;
    }

    out_type->kind = PIE_TYPE_RAW_PTR;
    out_type->type_width = PIE_WIDTH_64;
    out_type->raw_pointee_kind = symbol.type.kind;
    out_type->raw_pointee_width = symbol.type.type_width;
    return PIE_SEMA_OK;
  }

  if (strcmp(expr->op_text, "*raw") != 0) {
    return PIE_SEMA_NO_MATCH;
  }

  if (!ctx->api->in_unsafe(ctx->sema)) {
    ctx->api->error(ctx->sema, "raw pointer dereference requires unsafe");
    out_type->kind = PIE_TYPE_ERROR;
    return PIE_SEMA_ERROR;
  }

  PieType ptr_type;
  if (!expr->right ||
      ctx->api->check_expr(ctx->sema, expr->right, &ptr_type) != PIE_SEMA_OK) {
    out_type->kind = PIE_TYPE_ERROR;
    return PIE_SEMA_ERROR;
  }
  if (ptr_type.kind != PIE_TYPE_RAW_PTR) {
    ctx->api->errorf(ctx->sema, "raw pointer dereference requires *raw, got %s",
                     ctx->api->type_name(ptr_type));
    out_type->kind = PIE_TYPE_ERROR;
    return PIE_SEMA_ERROR;
  }

  out_type->kind = ptr_type.raw_pointee_kind;
  out_type->type_width = ptr_type.raw_pointee_width;
  return PIE_SEMA_OK;
}
