#include "pie/core/lower/lower.h"

#include <stdlib.h>
#include <string.h>

static void free_ir_branch(PieIrProgram *program) {
  if (!program) {
    return;
  }
  pie_ir_program_free(program);
  free(program);
}

static PieIrProgram *lower_branch(PieLowerContext *ctx,
                                  const PieProgram *branch) {
  if (!branch) {
    return NULL;
  }

  PieIrProgram *ir = (PieIrProgram *)malloc(sizeof(PieIrProgram));
  if (!ir) {
    ctx->api->error(ctx->lower, "out of memory while lowering if branch");
    return NULL;
  }
  if (!ctx->api->lower_block(ctx->lower, branch, ir)) {
    free_ir_branch(ir);
    return NULL;
  }
  return ir;
}

PieLowerResult pie_feature_if_lower_stmt(PieLowerContext *ctx,
                                         const PieStmt *stmt) {
  if (stmt->kind != PIE_STMT_IF) {
    return PIE_LOWER_NO_MATCH;
  }

  PieIrExpr *condition = NULL;
  if (ctx->api->lower_expr(ctx->lower, stmt->expr, &condition) !=
      PIE_LOWER_OK) {
    return PIE_LOWER_ERROR;
  }

  PieIrProgram *then_branch = lower_branch(ctx, stmt->then_branch);
  if (!then_branch) {
    pie_ir_expr_free(condition);
    return PIE_LOWER_ERROR;
  }

  PieIrProgram *else_branch = lower_branch(ctx, stmt->else_branch);
  if (stmt->else_branch && !else_branch) {
    pie_ir_expr_free(condition);
    free_ir_branch(then_branch);
    return PIE_LOWER_ERROR;
  }

  PieIrStmt ir_stmt;
  memset(&ir_stmt, 0, sizeof(ir_stmt));
  ir_stmt.kind = PIE_IR_STMT_IF;
  ir_stmt.expr = condition;
  ir_stmt.then_branch = then_branch;
  ir_stmt.else_branch = else_branch;
  if (!ctx->api->push_stmt(ctx->lower, ir_stmt)) {
    pie_ir_expr_free(condition);
    free_ir_branch(then_branch);
    free_ir_branch(else_branch);
    return PIE_LOWER_ERROR;
  }
  return PIE_LOWER_OK;
}
