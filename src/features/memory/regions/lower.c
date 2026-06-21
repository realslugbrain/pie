#include "pie/core/lower/lower.h"

#include <stdlib.h>
#include <string.h>

static void free_ir_body(PieIrProgram *program) {
  if (!program) {
    return;
  }
  pie_ir_program_free(program);
  free(program);
}

PieLowerResult pie_feature_regions_lower_stmt(PieLowerContext *ctx,
                                              const PieStmt *stmt) {
  if (stmt->kind != PIE_STMT_REGION) {
    return PIE_LOWER_NO_MATCH;
  }

  PieIrProgram *body = (PieIrProgram *)malloc(sizeof(PieIrProgram));
  if (!body) {
    ctx->api->error(ctx->lower, "out of memory while lowering region body");
    return PIE_LOWER_ERROR;
  }
  if (!ctx->api->lower_block(ctx->lower, stmt->then_branch, body)) {
    free_ir_body(body);
    return PIE_LOWER_ERROR;
  }

  PieIrStmt ir_stmt;
  memset(&ir_stmt, 0, sizeof(ir_stmt));
  ir_stmt.kind = PIE_IR_STMT_REGION;
  ir_stmt.then_branch = body;
  if (!ctx->api->push_stmt(ctx->lower, ir_stmt)) {
    free_ir_body(body);
    return PIE_LOWER_ERROR;
  }
  return PIE_LOWER_OK;
}
