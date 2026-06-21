#include "pie/core/sema/sema.h"

#include <string.h>

PieSemaResult pie_feature_int_sema_expr(PieSemaContext *ctx,
                                        const PieExpr *expr,
                                        PieType *out_type) {
  (void)ctx;

  if (expr->kind != PIE_EXPR_INT) {
    return PIE_SEMA_NO_MATCH;
  }

  memset(out_type, 0, sizeof(*out_type));
  out_type->kind = PIE_TYPE_INT;
  out_type->type_width = PIE_WIDTH_INFER;
  return PIE_SEMA_OK;
}
