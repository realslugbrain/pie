#include "pie/core/lower/lower.h"

PieLowerResult pie_feature_block_lower_stmt(PieLowerContext *ctx,
                                            const PieStmt *stmt) {
  if (stmt->kind != PIE_STMT_BLOCK) {
    return PIE_LOWER_NO_MATCH;
  }
  if (!ctx->api->enter_scope(ctx->lower)) {
    return PIE_LOWER_ERROR;
  }

  PieLowerResult result = PIE_LOWER_OK;
  if (stmt->then_branch) {
    for (size_t i = 0; i < stmt->then_branch->stmt_count; i++) {
      if (ctx->api->lower_stmt(ctx->lower, &stmt->then_branch->stmts[i]) !=
          PIE_LOWER_OK) {
        result = PIE_LOWER_ERROR;
        break;
      }
    }
  }

  ctx->api->leave_scope(ctx->lower);
  return result;
}
