#include "pie/core/sema/sema.h"

PieSemaResult pie_feature_continue_sema_stmt(PieSemaContext *ctx,
                                             const PieStmt *stmt) {
  if (stmt->kind != PIE_STMT_CONTINUE) {
    return PIE_SEMA_NO_MATCH;
  }
  if (!ctx->api->in_loop(ctx->sema)) {
    ctx->api->error(ctx->sema, "continue used outside of a loop");
    return PIE_SEMA_ERROR;
  }
  return PIE_SEMA_OK;
}
