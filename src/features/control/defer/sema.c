#include "pie/core/sema/sema.h"

PieSemaResult pie_feature_defer_sema_stmt(PieSemaContext *ctx,
                                          const PieStmt *stmt) {
  if (stmt->kind != PIE_STMT_DEFER) {
    return PIE_SEMA_NO_MATCH;
  }
  PieType expr_type;
  if (ctx->api->check_expr(ctx->sema, stmt->expr, &expr_type) != PIE_SEMA_OK) {
    return PIE_SEMA_ERROR;
  }
  return PIE_SEMA_OK;
}
