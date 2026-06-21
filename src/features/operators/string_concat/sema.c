#include "pie/core/sema/sema.h"

PieSemaResult pie_feature_string_concat_sema_expr(PieSemaContext *ctx,
                                                  const PieExpr *expr,
                                                  PieType *out_type) {
  if (expr->kind != PIE_EXPR_BINARY) {
    return PIE_SEMA_NO_MATCH;
  }
  if (expr->op != '+' || expr->op_text[1] != '+') {
    return PIE_SEMA_NO_MATCH;
  }

  PieType left;
  PieType right;
  if (ctx->api->check_expr(ctx->sema, expr->left, &left) != PIE_SEMA_OK ||
      ctx->api->check_expr(ctx->sema, expr->right, &right) != PIE_SEMA_OK) {
    out_type->kind = PIE_TYPE_ERROR;
    return PIE_SEMA_ERROR;
  }

  if (left.kind != PIE_TYPE_STRING || right.kind != PIE_TYPE_STRING) {
    ctx->api->errorf(ctx->sema,
                     "string concatenation requires both operands to be "
                     "string, got %s and %s",
                     ctx->api->type_name(left), ctx->api->type_name(right));
    out_type->kind = PIE_TYPE_ERROR;
    return PIE_SEMA_ERROR;
  }

  out_type->kind = PIE_TYPE_STRING;
  return PIE_SEMA_OK;
}
