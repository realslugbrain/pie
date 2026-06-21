#include "pie/core/sema/sema.h"

PieSemaResult pie_feature_range_sema_expr(PieSemaContext *ctx,
                                          const PieExpr *expr,
                                          PieType *out_type) {
  if (expr->kind != PIE_EXPR_RANGE) {
    return PIE_SEMA_NO_MATCH;
  }

  PieType start_type;
  if (ctx->api->check_expr(ctx->sema, expr->range_start, &start_type) !=
      PIE_SEMA_OK) {
    return PIE_SEMA_ERROR;
  }

  PieType end_type;
  if (ctx->api->check_expr(ctx->sema, expr->range_end, &end_type) !=
      PIE_SEMA_OK) {
    return PIE_SEMA_ERROR;
  }

  if (start_type.kind != PIE_TYPE_INT) {
    ctx->api->error(ctx->sema, "range start must be int");
    out_type->kind = PIE_TYPE_ERROR;
    return PIE_SEMA_ERROR;
  }

  if (end_type.kind != PIE_TYPE_INT) {
    ctx->api->error(ctx->sema, "range end must be int");
    out_type->kind = PIE_TYPE_ERROR;
    return PIE_SEMA_ERROR;
  }

  out_type->kind = PIE_TYPE_STRUCT;
  out_type->struct_name = NULL;
  return PIE_SEMA_OK;
}
