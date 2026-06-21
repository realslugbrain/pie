#include "pie/core/lower/lower.h"

#include <string.h>

PieLowerResult pie_feature_pass_lower_stmt(PieLowerContext *ctx,
                                           const PieStmt *stmt) {
  if (stmt->kind != PIE_STMT_PASS) {
    return PIE_LOWER_NO_MATCH;
  }

  PieIrStmt ir_stmt;
  memset(&ir_stmt, 0, sizeof(ir_stmt));
  ir_stmt.kind = PIE_IR_STMT_PASS;
  if (!ctx->api->push_stmt(ctx->lower, ir_stmt)) {
    return PIE_LOWER_ERROR;
  }
  return PIE_LOWER_OK;
}
