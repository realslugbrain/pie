#include "pie/core/sema/sema.h"

#include <string.h>

PieSemaResult pie_feature_string_interp_sema_expr(PieSemaContext *ctx,
                                                  const PieExpr *expr,
                                                  PieType *out_type) {
  if (expr->kind != PIE_EXPR_STRING_INTERP) {
    return PIE_SEMA_NO_MATCH;
  }

  for (size_t i = 0; i < expr->interp_part_count; i++) {
    if (expr->interp_exprs[i]) {
      PieType elem_type;
      if (ctx->api->check_expr(ctx->sema, expr->interp_exprs[i], &elem_type) !=
          PIE_SEMA_OK) {
        return PIE_SEMA_ERROR;
      }
    }
  }

  memset(out_type, 0, sizeof(*out_type));
  out_type->kind = PIE_TYPE_STRING;
  out_type->type_width = PIE_WIDTH_INFER;
  return PIE_SEMA_OK;
}
