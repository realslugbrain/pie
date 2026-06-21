#include "pie/core/lower/lower.h"

#include <stdlib.h>

PieLowerResult pie_feature_defer_lower_stmt(PieLowerContext *ctx,
                                            const PieStmt *stmt) {
  if (stmt->kind != PIE_STMT_DEFER) {
    return PIE_LOWER_NO_MATCH;
  }

  PieIrExpr *ir_expr = NULL;
  if (ctx->api->lower_expr(ctx->lower, stmt->expr, &ir_expr) != PIE_LOWER_OK) {
    return PIE_LOWER_ERROR;
  }

  PieIrStmt ir_stmt;
  memset(&ir_stmt, 0, sizeof(ir_stmt));
  ir_stmt.kind = PIE_IR_STMT_DEFER;
  ir_stmt.expr = ir_expr;
  if (!ctx->api->push_stmt(ctx->lower, ir_stmt)) {
    pie_ir_expr_free(ir_expr);
    ctx->api->error(ctx->lower, "out of memory while lowering defer statement");
    return PIE_LOWER_ERROR;
  }
  return PIE_LOWER_OK;
}
