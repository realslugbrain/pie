#include "pie/core/lower/lower.h"

PieLowerResult pie_feature_string_lower_expr(PieLowerContext *ctx,
                                             const PieExpr *expr,
                                             PieIrExpr **out_expr) {
  if (expr->kind != PIE_EXPR_STRING) {
    return PIE_LOWER_NO_MATCH;
  }

  *out_expr = pie_ir_expr_string(expr->string_value, expr->string_len);
  if (!*out_expr) {
    ctx->api->error(ctx->lower, "out of memory while lowering string literal");
    return PIE_LOWER_ERROR;
  }
  return PIE_LOWER_OK;
}
