#include "pie/core/sema/sema.h"

#include <string.h>

PieSemaResult pie_feature_tuple_sema_expr(PieSemaContext *ctx,
                                          const PieExpr *expr,
                                          PieType *out_type) {
  if (expr->kind != PIE_EXPR_TUPLE) {
    return PIE_SEMA_NO_MATCH;
  }

  memset(out_type, 0, sizeof(*out_type));
  out_type->kind = PIE_TYPE_TUPLE;
  out_type->tuple_element_count = expr->tuple_element_count;

  for (size_t i = 0; i < expr->tuple_element_count; i++) {
    PieType elem_type;
    if (ctx->api->check_expr(ctx->sema, expr->tuple_elements[i], &elem_type) !=
        PIE_SEMA_OK) {
      return PIE_SEMA_ERROR;
    }
    out_type->tuple_element_kinds[i] = elem_type.kind;
    out_type->tuple_element_widths[i] = elem_type.type_width;
  }

  return PIE_SEMA_OK;
}
