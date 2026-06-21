#include "pie/core/lower/lower.h"

#include <string.h>

static int is_comparison_op(const char *op) {
  return strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
         strcmp(op, "<") == 0 || strcmp(op, "<=") == 0 ||
         strcmp(op, ">") == 0 || strcmp(op, ">=") == 0;
}

PieLowerResult pie_feature_comparison_lower_expr(PieLowerContext *ctx,
                                                 const PieExpr *expr,
                                                 PieIrExpr **out_expr) {
  if (expr->kind != PIE_EXPR_BINARY || !is_comparison_op(expr->op_text)) {
    return PIE_LOWER_NO_MATCH;
  }

  PieIrExpr *left = NULL;
  PieIrExpr *right = NULL;
  if (ctx->api->lower_expr(ctx->lower, expr->left, &left) != PIE_LOWER_OK) {
    return PIE_LOWER_ERROR;
  }
  if (ctx->api->lower_expr(ctx->lower, expr->right, &right) != PIE_LOWER_OK) {
    pie_ir_expr_free(left);
    return PIE_LOWER_ERROR;
  }

  *out_expr =
      pie_ir_expr_binary_typed(expr->op_text, left, right, PIE_IR_TYPE_BOOL);
  if (!*out_expr) {
    pie_ir_expr_free(left);
    pie_ir_expr_free(right);
    ctx->api->error(ctx->lower,
                    "out of memory while lowering comparison expression");
    return PIE_LOWER_ERROR;
  }
  return PIE_LOWER_OK;
}
