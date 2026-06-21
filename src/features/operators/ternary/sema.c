#include "pie/core/sema/sema.h"

#include <string.h>

PieSemaResult pie_feature_ternary_sema_expr(PieSemaContext *ctx,
                                            const PieExpr *expr,
                                            PieType *out_type) {
  if (expr->kind != PIE_EXPR_TERNARY) {
    return PIE_SEMA_NO_MATCH;
  }

  PieType cond_type;
  if (ctx->api->check_expr(ctx->sema, expr->left, &cond_type) != PIE_SEMA_OK) {
    return PIE_SEMA_ERROR;
  }
  if (cond_type.kind != PIE_TYPE_BOOL) {
    ctx->api->errorf(ctx->sema, "ternary condition must be bool, got %s",
                     ctx->api->type_name(cond_type));
    return PIE_SEMA_ERROR;
  }

  PieType true_type;
  if (ctx->api->check_expr(ctx->sema, expr->right, &true_type) != PIE_SEMA_OK) {
    return PIE_SEMA_ERROR;
  }

  PieType false_type;
  if (ctx->api->check_expr(ctx->sema, expr->ternary_false, &false_type) !=
      PIE_SEMA_OK) {
    return PIE_SEMA_ERROR;
  }

  if (true_type.kind != false_type.kind) {
    ctx->api->errorf(
        ctx->sema, "ternary branches must have same type, got %s and %s",
        ctx->api->type_name(true_type), ctx->api->type_name(false_type));
    return PIE_SEMA_ERROR;
  }

  out_type->kind = true_type.kind;
  out_type->type_width = true_type.type_width;
  return PIE_SEMA_OK;
}
