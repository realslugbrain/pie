#include "pie/core/lower/lower.h"

PieLowerResult pie_feature_bool_lower_expr(PieLowerContext *ctx,
                                           const PieExpr *expr,
                                           PieIrExpr **out_expr) {
  if (expr->kind != PIE_EXPR_BOOL) {
    return PIE_LOWER_NO_MATCH;
  }

  *out_expr = pie_ir_expr_bool(expr->bool_value);
  if (!*out_expr) {
    ctx->api->error(ctx->lower, "out of memory while lowering bool literal");
    return PIE_LOWER_ERROR;
  }
  return PIE_LOWER_OK;
}
