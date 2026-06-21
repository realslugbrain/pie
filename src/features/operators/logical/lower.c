#include "pie/core/lower/lower.h"

#include <string.h>

static int is_logical_binary(const char *op) {
  return strcmp(op, "and") == 0 || strcmp(op, "or") == 0;
}

PieLowerResult pie_feature_logical_lower_expr(PieLowerContext *ctx,
                                              const PieExpr *expr,
                                              PieIrExpr **out_expr) {
  if (expr->kind == PIE_EXPR_UNARY && strcmp(expr->op_text, "not") == 0) {
    PieIrExpr *inner = NULL;
    if (ctx->api->lower_expr(ctx->lower, expr->right, &inner) != PIE_LOWER_OK) {
      return PIE_LOWER_ERROR;
    }
    *out_expr = pie_ir_expr_unary_typed("not", inner, PIE_IR_TYPE_BOOL);
    if (!*out_expr) {
      pie_ir_expr_free(inner);
      ctx->api->error(ctx->lower,
                      "out of memory while lowering logical not expression");
      return PIE_LOWER_ERROR;
    }
    return PIE_LOWER_OK;
  }

  if (expr->kind == PIE_EXPR_BINARY && is_logical_binary(expr->op_text)) {
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
                      "out of memory while lowering logical expression");
      return PIE_LOWER_ERROR;
    }
    return PIE_LOWER_OK;
  }

  return PIE_LOWER_NO_MATCH;
}
