#include "pie/core/lower/lower.h"

static int is_arithmetic_binary(char op, const char *op_text) {
  if (op == '+' && op_text[1] == '+')
    return 0;
  if (op == '*' && op_text[1] == '*')
    return 1;
  return op == '+' || op == '-' || op == '*' || op == '/' || op == '%';
}

PieLowerResult pie_feature_arithmetic_lower_expr(PieLowerContext *ctx,
                                                 const PieExpr *expr,
                                                 PieIrExpr **out_expr) {
  if (expr->kind == PIE_EXPR_UNARY) {
    if (expr->op != '-') {
      return PIE_LOWER_NO_MATCH;
    }
    if (expr->op_text[1] == '-' || expr->op_text[1] == '+') {
      return PIE_LOWER_NO_MATCH;
    }

    PieIrExpr *inner = NULL;
    if (ctx->api->lower_expr(ctx->lower, expr->right, &inner) != PIE_LOWER_OK) {
      return PIE_LOWER_ERROR;
    }
    *out_expr = pie_ir_expr_unary_typed(expr->op_text, inner, inner->type);
    if (!*out_expr) {
      pie_ir_expr_free(inner);
      ctx->api->error(ctx->lower,
                      "out of memory while lowering unary expression");
      return PIE_LOWER_ERROR;
    }
    return PIE_LOWER_OK;
  }

  if (expr->kind == PIE_EXPR_BINARY) {
    if (!is_arithmetic_binary(expr->op, expr->op_text)) {
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

    if (expr->op == '*' && expr->op_text[1] == '*') {
      PieIrTypeKind result_type =
          left->type == PIE_IR_TYPE_FLOAT ? PIE_IR_TYPE_FLOAT : PIE_IR_TYPE_INT;
      *out_expr =
          pie_ir_expr_binary_typed(expr->op_text, left, right, result_type);
      if (!*out_expr) {
        pie_ir_expr_free(left);
        pie_ir_expr_free(right);
        ctx->api->error(ctx->lower,
                        "out of memory while lowering power expression");
        return PIE_LOWER_ERROR;
      }
      return PIE_LOWER_OK;
    }

    PieIrTypeKind result_type =
        left->type == PIE_IR_TYPE_FLOAT ? PIE_IR_TYPE_FLOAT : PIE_IR_TYPE_INT;
    *out_expr =
        pie_ir_expr_binary_typed(expr->op_text, left, right, result_type);
    if (!*out_expr) {
      pie_ir_expr_free(left);
      pie_ir_expr_free(right);
      ctx->api->error(ctx->lower,
                      "out of memory while lowering binary expression");
      return PIE_LOWER_ERROR;
    }
    return PIE_LOWER_OK;
  }

  return PIE_LOWER_NO_MATCH;
}
