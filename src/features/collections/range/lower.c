#include "pie/core/lower/lower.h"

PieLowerResult pie_feature_range_lower_expr(PieLowerContext *ctx,
                                            const PieExpr *expr,
                                            PieIrExpr **out_expr) {
  if (expr->kind != PIE_EXPR_RANGE) {
    return PIE_LOWER_NO_MATCH;
  }

  PieIrExpr *start = NULL;
  if (ctx->api->lower_expr(ctx->lower, expr->range_start, &start) !=
      PIE_LOWER_OK) {
    return PIE_LOWER_ERROR;
  }

  PieIrExpr *end = NULL;
  if (ctx->api->lower_expr(ctx->lower, expr->range_end, &end) != PIE_LOWER_OK) {
    pie_ir_expr_free(start);
    return PIE_LOWER_ERROR;
  }

  *out_expr = pie_ir_expr_range(start, end, expr->range_inclusive);
  if (!*out_expr) {
    pie_ir_expr_free(start);
    pie_ir_expr_free(end);
    return PIE_LOWER_ERROR;
  }

  return PIE_LOWER_OK;
}
