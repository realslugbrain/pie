#include "pie/core/sema/sema.h"

#include <string.h>

static int is_bitwise_op(const char *op) {
  return strcmp(op, "&") == 0 || strcmp(op, "|") == 0 || strcmp(op, "^") == 0 ||
         strcmp(op, "<<") == 0 || strcmp(op, ">>") == 0;
}

PieSemaResult pie_feature_bitwise_sema_expr(PieSemaContext *ctx,
                                            const PieExpr *expr,
                                            PieType *out_type) {
  if (expr->kind == PIE_EXPR_BINARY && is_bitwise_op(expr->op_text)) {
    PieType left;
    PieType right;
    if (ctx->api->check_expr(ctx->sema, expr->left, &left) != PIE_SEMA_OK ||
        ctx->api->check_expr(ctx->sema, expr->right, &right) != PIE_SEMA_OK) {
      out_type->kind = PIE_TYPE_ERROR;
      return PIE_SEMA_ERROR;
    }

    if (left.kind != PIE_TYPE_INT || right.kind != PIE_TYPE_INT) {
      ctx->api->errorf(
          ctx->sema,
          "bitwise operator '%s' requires int operands, got %s and %s",
          expr->op_text, ctx->api->type_name(left), ctx->api->type_name(right));
      out_type->kind = PIE_TYPE_ERROR;
      return PIE_SEMA_ERROR;
    }

    out_type->kind = PIE_TYPE_INT;
    return PIE_SEMA_OK;
  }

  if (expr->kind == PIE_EXPR_UNARY && expr->op == '~') {
    PieType inner;
    if (ctx->api->check_expr(ctx->sema, expr->right, &inner) != PIE_SEMA_OK) {
      out_type->kind = PIE_TYPE_ERROR;
      return PIE_SEMA_ERROR;
    }

    if (inner.kind != PIE_TYPE_INT) {
      ctx->api->errorf(ctx->sema, "bitwise NOT requires int operand, got %s",
                       ctx->api->type_name(inner));
      out_type->kind = PIE_TYPE_ERROR;
      return PIE_SEMA_ERROR;
    }

    out_type->kind = PIE_TYPE_INT;
    return PIE_SEMA_OK;
  }

  return PIE_SEMA_NO_MATCH;
}
