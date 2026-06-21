#include "pie/core/sema/sema.h"

PieSemaResult pie_feature_if_expr_sema_expr(PieSemaContext *ctx,
                                            const PieExpr *expr,
                                            PieType *out_type) {
  if (expr->kind != PIE_EXPR_IF) {
    return PIE_SEMA_NO_MATCH;
  }

  PieType cond_type;
  if (ctx->api->check_expr(ctx->sema, expr->if_condition, &cond_type) !=
      PIE_SEMA_OK) {
    return PIE_SEMA_ERROR;
  }
  if (cond_type.kind != PIE_TYPE_BOOL) {
    ctx->api->error(ctx->sema, "if expression condition must be bool");
    return PIE_SEMA_ERROR;
  }

  PieType then_type;
  if (ctx->api->check_expr(ctx->sema, expr->if_then, &then_type) !=
      PIE_SEMA_OK) {
    return PIE_SEMA_ERROR;
  }

  PieType else_type;
  if (ctx->api->check_expr(ctx->sema, expr->if_else, &else_type) !=
      PIE_SEMA_OK) {
    return PIE_SEMA_ERROR;
  }

  if (then_type.kind != else_type.kind) {
    ctx->api->error(ctx->sema,
                    "if expression branches must have the same type");
    return PIE_SEMA_ERROR;
  }

  *out_type = then_type;
  return PIE_SEMA_OK;
}
