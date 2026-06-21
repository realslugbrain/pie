#include "pie/core/lower/lower.h"

#include <string.h>

static PieIrTypeKind borrow_type_from_op(const char *op) {
  return strcmp(op, "&mut") == 0 ? PIE_IR_TYPE_REF_MUT : PIE_IR_TYPE_REF;
}

PieLowerResult pie_feature_borrow_lower_expr(PieLowerContext *ctx,
                                             const PieExpr *expr,
                                             PieIrExpr **out_expr) {
  if (expr->kind != PIE_EXPR_UNARY ||
      (strcmp(expr->op_text, "&") != 0 && strcmp(expr->op_text, "&mut") != 0)) {
    return PIE_LOWER_NO_MATCH;
  }

  PieIrExpr *inner = NULL;
  if (ctx->api->lower_expr(ctx->lower, expr->right, &inner) != PIE_LOWER_OK) {
    return PIE_LOWER_ERROR;
  }

  *out_expr = pie_ir_expr_unary_typed(expr->op_text, inner,
                                      borrow_type_from_op(expr->op_text));
  if (!*out_expr) {
    pie_ir_expr_free(inner);
    ctx->api->error(ctx->lower,
                    "out of memory while lowering borrow expression");
    return PIE_LOWER_ERROR;
  }

  (*out_expr)->ref_inner_type = inner->type;
  (*out_expr)->ref_inner_width = inner->type_width;
  return PIE_LOWER_OK;
}
