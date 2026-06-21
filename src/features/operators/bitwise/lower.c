#include "pie/core/lower/lower.h"

#include <string.h>

static int is_bitwise_op(const char *op) {
  return strcmp(op, "&") == 0 || strcmp(op, "|") == 0 || strcmp(op, "^") == 0 ||
         strcmp(op, "<<") == 0 || strcmp(op, ">>") == 0;
}

PieLowerResult pie_feature_bitwise_lower_expr(PieLowerContext *ctx,
                                              const PieExpr *expr,
                                              PieIrExpr **out_expr) {
  if (expr->kind == PIE_EXPR_BINARY && is_bitwise_op(expr->op_text)) {
    PieIrExpr *left = NULL;
    if (ctx->api->lower_expr(ctx->lower, expr->left, &left) != PIE_LOWER_OK) {
      return PIE_LOWER_ERROR;
    }
    PieIrExpr *right = NULL;
    if (ctx->api->lower_expr(ctx->lower, expr->right, &right) != PIE_LOWER_OK) {
      pie_ir_expr_free(left);
      return PIE_LOWER_ERROR;
    }
    *out_expr =
        pie_ir_expr_binary_typed(expr->op_text, left, right, PIE_IR_TYPE_INT);
    if (!*out_expr) {
      pie_ir_expr_free(left);
      pie_ir_expr_free(right);
      ctx->api->error(ctx->lower,
                      "out of memory while lowering bitwise expression");
      return PIE_LOWER_ERROR;
    }
    (*out_expr)->type_width = PIE_WIDTH_64;
    return PIE_LOWER_OK;
  }

  if (expr->kind == PIE_EXPR_UNARY && expr->op == '~') {
    PieIrExpr *inner = NULL;
    if (ctx->api->lower_expr(ctx->lower, expr->right, &inner) != PIE_LOWER_OK) {
      return PIE_LOWER_ERROR;
    }
    *out_expr = pie_ir_expr_unary('~', inner);
    if (!*out_expr) {
      pie_ir_expr_free(inner);
      ctx->api->error(ctx->lower, "out of memory while lowering bitwise NOT");
      return PIE_LOWER_ERROR;
    }
    (*out_expr)->type = PIE_IR_TYPE_INT;
    (*out_expr)->type_width = PIE_WIDTH_64;
    return PIE_LOWER_OK;
  }

  return PIE_LOWER_NO_MATCH;
}
