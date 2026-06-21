#include "pie/core/lower/lower.h"

#include <string.h>

PieLowerResult pie_feature_tuple_lower_expr(PieLowerContext *ctx,
                                            const PieExpr *expr,
                                            PieIrExpr **out_expr) {
  if (expr->kind != PIE_EXPR_TUPLE) {
    return PIE_LOWER_NO_MATCH;
  }

  *out_expr = pie_ir_expr_tuple(0);
  if (!*out_expr) {
    ctx->api->error(ctx->lower, "out of memory while lowering tuple");
    return PIE_LOWER_ERROR;
  }

  for (size_t i = 0; i < expr->tuple_element_count; i++) {
    PieIrExpr *elem = NULL;
    if (ctx->api->lower_expr(ctx->lower, expr->tuple_elements[i], &elem) !=
        PIE_LOWER_OK) {
      pie_ir_expr_free(*out_expr);
      *out_expr = NULL;
      return PIE_LOWER_ERROR;
    }
    if (!pie_ir_expr_tuple_add_element(*out_expr, elem)) {
      pie_ir_expr_free(elem);
      pie_ir_expr_free(*out_expr);
      *out_expr = NULL;
      ctx->api->error(ctx->lower, "out of memory while lowering tuple element");
      return PIE_LOWER_ERROR;
    }
    (*out_expr)->tuple_element_types[i] = elem->type;
    (*out_expr)->tuple_element_widths[i] = elem->type_width;
  }

  return PIE_LOWER_OK;
}
