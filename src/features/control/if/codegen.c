#include "pie/backend/asm/asm_codegen.h"

static int emit_branch(PieAsmCodegenContext *ctx, const PieIrProgram *branch) {
  if (!branch) {
    return 1;
  }
  for (size_t i = 0; i < branch->stmt_count; i++) {
    if (!ctx->api->emit_stmt(ctx->cg, &branch->stmts[i])) {
      return 0;
    }
  }
  return 1;
}

PieAsmGenResult pie_feature_if_codegen_stmt(PieAsmCodegenContext *ctx,
                                            const PieIrStmt *stmt) {
  if (stmt->kind != PIE_IR_STMT_IF) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;
  int else_label = api->next_label(cg);
  int end_label = api->next_label(cg);

  if (!api->emit_expr(cg, stmt->expr)) {
    return PIE_ASM_GEN_ERROR;
  }
  api->emit(cg, "    cmp rax, 0\n");
  api->emit(cg, "    je .Lif_else_%d\n", else_label);
  if (!emit_branch(ctx, stmt->then_branch)) {
    return PIE_ASM_GEN_ERROR;
  }
  api->emit(cg, "    jmp .Lif_end_%d\n", end_label);
  api->emit(cg, ".Lif_else_%d:\n", else_label);
  if (!emit_branch(ctx, stmt->else_branch)) {
    return PIE_ASM_GEN_ERROR;
  }
  api->emit(cg, ".Lif_end_%d:\n", end_label);
  return PIE_ASM_GEN_OK;
}
