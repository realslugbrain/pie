#include "pie/core/lower/lower.h"

#include <string.h>

PieLowerResult pie_feature_ternary_lower_expr(PieLowerContext *ctx,
                                              const PieExpr *expr,
                                              PieIrExpr **out_expr) {
  if (expr->kind != PIE_EXPR_TERNARY) {
    return PIE_LOWER_NO_MATCH;
  }

  PieIrExpr *cond = NULL;
  if (ctx->api->lower_expr(ctx->lower, expr->left, &cond) != PIE_LOWER_OK) {
    return PIE_LOWER_ERROR;
  }

  PieIrExpr *true_expr = NULL;
  if (ctx->api->lower_expr(ctx->lower, expr->right, &true_expr) !=
      PIE_LOWER_OK) {
    pie_ir_expr_free(cond);
    return PIE_LOWER_ERROR;
  }

  PieIrExpr *false_expr = NULL;
  if (ctx->api->lower_expr(ctx->lower, expr->ternary_false, &false_expr) !=
      PIE_LOWER_OK) {
    pie_ir_expr_free(cond);
    pie_ir_expr_free(true_expr);
    return PIE_LOWER_ERROR;
  }

  *out_expr = pie_ir_expr_ternary(cond, true_expr, false_expr);
  if (!*out_expr) {
    pie_ir_expr_free(cond);
    pie_ir_expr_free(true_expr);
    pie_ir_expr_free(false_expr);
    ctx->api->error(ctx->lower,
                    "out of memory while lowering ternary expression");
    return PIE_LOWER_ERROR;
  }

  (*out_expr)->type = true_expr->type;
  (*out_expr)->type_width = true_expr->type_width;

  return PIE_LOWER_OK;
}
