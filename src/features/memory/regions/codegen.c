#include "pie/backend/asm/asm_codegen.h"

static int emit_body(PieAsmCodegenContext *ctx, const PieIrProgram *body) {
  if (!body) {
    return 1;
  }
  for (size_t i = 0; i < body->stmt_count; i++) {
    if (!ctx->api->emit_stmt(ctx->cg, &body->stmts[i])) {
      return 0;
    }
  }
  return 1;
}

PieAsmGenResult pie_feature_regions_codegen_stmt(PieAsmCodegenContext *ctx,
                                                 const PieIrStmt *stmt) {
  if (stmt->kind != PIE_IR_STMT_REGION) {
    return PIE_ASM_GEN_NO_MATCH;
  }
  if (!emit_body(ctx, stmt->then_branch)) {
    return PIE_ASM_GEN_ERROR;
  }
  return PIE_ASM_GEN_OK;
}
