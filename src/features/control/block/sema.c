#include "pie/core/sema/sema.h"

PieSemaResult pie_feature_block_sema_stmt(PieSemaContext *ctx,
                                          const PieStmt *stmt) {
  if (stmt->kind != PIE_STMT_BLOCK) {
    return PIE_SEMA_NO_MATCH;
  }
  return ctx->api->check_block(ctx->sema, stmt->then_branch);
}
