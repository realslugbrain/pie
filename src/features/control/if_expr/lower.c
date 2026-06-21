#include "pie/core/lower/lower.h"

#include <string.h>

PieLowerResult pie_feature_if_expr_lower_expr(PieLowerContext *ctx,
                                              const PieExpr *expr,
                                              PieIrExpr **out_expr) {
  if (expr->kind != PIE_EXPR_IF) {
    return PIE_LOWER_NO_MATCH;
  }

  PieIrExpr *cond = NULL;
  if (ctx->api->lower_expr(ctx->lower, expr->if_condition, &cond) !=
      PIE_LOWER_OK) {
    return PIE_LOWER_ERROR;
  }

  PieIrExpr *then_expr = NULL;
  if (ctx->api->lower_expr(ctx->lower, expr->if_then, &then_expr) !=
      PIE_LOWER_OK) {
    pie_ir_expr_free(cond);
    return PIE_LOWER_ERROR;
  }

  PieIrExpr *else_expr = NULL;
  if (ctx->api->lower_expr(ctx->lower, expr->if_else, &else_expr) !=
      PIE_LOWER_OK) {
    pie_ir_expr_free(cond);
    pie_ir_expr_free(then_expr);
    return PIE_LOWER_ERROR;
  }

  *out_expr = pie_ir_expr_if(cond, then_expr, else_expr);
  if (!*out_expr) {
    pie_ir_expr_free(cond);
    pie_ir_expr_free(then_expr);
    pie_ir_expr_free(else_expr);
    ctx->api->error(ctx->lower, "out of memory while lowering if expression");
    return PIE_LOWER_ERROR;
  }

  (*out_expr)->type = then_expr->type;
  (*out_expr)->type_width = then_expr->type_width;

  return PIE_LOWER_OK;
}
