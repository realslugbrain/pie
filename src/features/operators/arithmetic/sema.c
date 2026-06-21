#include "pie/core/sema/sema.h"

static int is_arithmetic_binary(char op, const char *op_text) {
  if (op == '+' && op_text[1] == '+')
    return 0;
  if (op == '*' && op_text[1] == '*')
    return 1;
  return op == '+' || op == '-' || op == '*' || op == '/' || op == '%';
}

static int require_int(PieSemaContext *ctx, PieType type, const char *what) {
  if (type.kind == PIE_TYPE_INT || type.kind == PIE_TYPE_STRUCT) {
    return 1;
  }
  ctx->api->errorf(ctx->sema, "%s must be int, got %s", what,
                   ctx->api->type_name(type));
  return 0;
}

static int require_numeric(PieSemaContext *ctx, PieType type,
                           const char *what) {
  if (type.kind == PIE_TYPE_INT || type.kind == PIE_TYPE_FLOAT ||
      type.kind == PIE_TYPE_STRUCT) {
    return 1;
  }
  ctx->api->errorf(ctx->sema, "%s must be int or float, got %s", what,
                   ctx->api->type_name(type));
  return 0;
}

PieSemaResult pie_feature_arithmetic_sema_expr(PieSemaContext *ctx,
                                               const PieExpr *expr,
                                               PieType *out_type) {
  if (expr->kind == PIE_EXPR_UNARY) {
    if (expr->op != '-') {
      return PIE_SEMA_NO_MATCH;
    }
    if (expr->op_text[1] == '-' || expr->op_text[1] == '+') {
      return PIE_SEMA_NO_MATCH;
    }

    PieType inner;
    if (ctx->api->check_expr(ctx->sema, expr->right, &inner) != PIE_SEMA_OK) {
      out_type->kind = PIE_TYPE_ERROR;
      return PIE_SEMA_ERROR;
    }
    if (!require_numeric(ctx, inner, "unary '-' operand")) {
      out_type->kind = PIE_TYPE_ERROR;
      return PIE_SEMA_ERROR;
    }
    out_type->kind = inner.kind;
    return PIE_SEMA_OK;
  }

  if (expr->kind == PIE_EXPR_BINARY) {
    if (!is_arithmetic_binary(expr->op, expr->op_text)) {
      return PIE_SEMA_NO_MATCH;
    }

    PieType left;
    PieType right;
    if (ctx->api->check_expr(ctx->sema, expr->left, &left) != PIE_SEMA_OK ||
        ctx->api->check_expr(ctx->sema, expr->right, &right) != PIE_SEMA_OK) {
      out_type->kind = PIE_TYPE_ERROR;
      return PIE_SEMA_ERROR;
    }
    if (expr->op == '%') {
      if (!require_int(ctx, left, "left modulo operand") ||
          !require_int(ctx, right, "right modulo operand")) {
        out_type->kind = PIE_TYPE_ERROR;
        return PIE_SEMA_ERROR;
      }
      out_type->kind = PIE_TYPE_INT;
      return PIE_SEMA_OK;
    }

    if (expr->op == '*' && expr->op_text[1] == '*') {
      if (!require_numeric(ctx, left, "left power operand") ||
          !require_numeric(ctx, right, "right power operand")) {
        out_type->kind = PIE_TYPE_ERROR;
        return PIE_SEMA_ERROR;
      }
      out_type->kind = left.kind;
      return PIE_SEMA_OK;
    }

    if (!require_numeric(ctx, left, "left arithmetic operand") ||
        !require_numeric(ctx, right, "right arithmetic operand")) {
      out_type->kind = PIE_TYPE_ERROR;
      return PIE_SEMA_ERROR;
    }
    if (left.kind != right.kind) {
      ctx->api->errorf(
          ctx->sema,
          "arithmetic operands must have the same type, got %s and %s",
          ctx->api->type_name(left), ctx->api->type_name(right));
      out_type->kind = PIE_TYPE_ERROR;
      return PIE_SEMA_ERROR;
    }
    out_type->kind = left.kind;
    return PIE_SEMA_OK;
  }

  return PIE_SEMA_NO_MATCH;
}
