#define _POSIX_C_SOURCE 200809L
#include "pie/core/lower/lower.h"

#include <stdlib.h>
#include <string.h>

PieLowerResult pie_feature_continue_lower_stmt(PieLowerContext *ctx,
                                               const PieStmt *stmt) {
  if (stmt->kind != PIE_STMT_CONTINUE) {
    return PIE_LOWER_NO_MATCH;
  }

  PieIrStmt ir_stmt;
  memset(&ir_stmt, 0, sizeof(ir_stmt));
  ir_stmt.kind = PIE_IR_STMT_CONTINUE;
  ir_stmt.label_name = stmt->label_name ? strdup(stmt->label_name) : NULL;
  if (!ctx->api->push_stmt(ctx->lower, ir_stmt)) {
    free(ir_stmt.label_name);
    return PIE_LOWER_ERROR;
  }
  return PIE_LOWER_OK;
}
