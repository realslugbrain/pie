#include "pie/core/lower/lower.h"

PieLowerResult pie_feature_char_lower_expr(PieLowerContext *ctx,
                                           const PieExpr *expr,
                                           PieIrExpr **out_expr) {
  if (expr->kind != PIE_EXPR_CHAR) {
    return PIE_LOWER_NO_MATCH;
  }

  *out_expr = pie_ir_expr_char(expr->char_value);
  if (!*out_expr) {
    ctx->api->error(ctx->lower, "out of memory while lowering char literal");
    return PIE_LOWER_ERROR;
  }
  return PIE_LOWER_OK;
}
