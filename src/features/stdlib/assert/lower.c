#include "pie/core/lower/lower.h"

#include <stdlib.h>
#include <string.h>

PieLowerResult pie_feature_assert_lower_stmt(PieLowerContext *ctx,
                                             const PieStmt *stmt) {
  if (stmt->kind == PIE_STMT_ASSERT) {
    PieIrStmt ir_stmt;
    memset(&ir_stmt, 0, sizeof(ir_stmt));
    ir_stmt.kind = PIE_IR_STMT_ASSERT;

    if (ctx->api->lower_expr(ctx->lower, stmt->target, &ir_stmt.assert_cond) !=
        PIE_LOWER_OK) {
      return PIE_LOWER_ERROR;
    }

    if (!ctx->api->push_stmt(ctx->lower, ir_stmt)) {
      pie_ir_expr_free(ir_stmt.assert_cond);
      return PIE_LOWER_ERROR;
    }
    return PIE_LOWER_OK;
  }

  if (stmt->kind == PIE_STMT_ASSERT_EQ) {
    PieIrStmt ir_stmt;
    memset(&ir_stmt, 0, sizeof(ir_stmt));
    ir_stmt.kind = PIE_IR_STMT_ASSERT_EQ;

    if (ctx->api->lower_expr(ctx->lower, stmt->target, &ir_stmt.assert_left) !=
        PIE_LOWER_OK) {
      return PIE_LOWER_ERROR;
    }
    if (ctx->api->lower_expr(ctx->lower, stmt->expr, &ir_stmt.assert_right) !=
        PIE_LOWER_OK) {
      pie_ir_expr_free(ir_stmt.assert_left);
      return PIE_LOWER_ERROR;
    }

    if (!ctx->api->push_stmt(ctx->lower, ir_stmt)) {
      pie_ir_expr_free(ir_stmt.assert_left);
      pie_ir_expr_free(ir_stmt.assert_right);
      return PIE_LOWER_ERROR;
    }
    return PIE_LOWER_OK;
  }

  return PIE_LOWER_NO_MATCH;
}
