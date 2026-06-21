#define _POSIX_C_SOURCE 200809L
#include "pie/core/lower/lower.h"

#include <stdlib.h>
#include <string.h>

static void free_ir_body(PieIrProgram *program) {
  if (!program)
    return;
  pie_ir_program_free(program);
  free(program);
}

PieLowerResult pie_feature_do_while_lower_stmt(PieLowerContext *ctx,
                                               const PieStmt *stmt) {
  if (stmt->kind != PIE_STMT_DO_WHILE) {
    return PIE_LOWER_NO_MATCH;
  }

  PieIrProgram *body = (PieIrProgram *)malloc(sizeof(PieIrProgram));
  if (!body) {
    ctx->api->error(ctx->lower, "out of memory while lowering do-while body");
    return PIE_LOWER_ERROR;
  }
  if (!ctx->api->lower_block(ctx->lower, stmt->then_branch, body)) {
    free_ir_body(body);
    return PIE_LOWER_ERROR;
  }

  PieIrExpr *condition = NULL;
  if (ctx->api->lower_expr(ctx->lower, stmt->expr, &condition) !=
      PIE_LOWER_OK) {
    free_ir_body(body);
    return PIE_LOWER_ERROR;
  }

  PieIrStmt ir_stmt;
  memset(&ir_stmt, 0, sizeof(ir_stmt));
  ir_stmt.kind = PIE_IR_STMT_DO_WHILE;
  ir_stmt.expr = condition;
  ir_stmt.then_branch = body;
  ir_stmt.label_name = stmt->label_name ? strdup(stmt->label_name) : NULL;
  if (!ctx->api->push_stmt(ctx->lower, ir_stmt)) {
    pie_ir_expr_free(condition);
    free_ir_body(body);
    free(ir_stmt.label_name);
    return PIE_LOWER_ERROR;
  }
  return PIE_LOWER_OK;
}
