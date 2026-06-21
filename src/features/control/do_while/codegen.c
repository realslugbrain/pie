#include "pie/backend/asm/asm_codegen.h"

static int emit_body(PieAsmCodegenContext *ctx, const PieIrProgram *body) {
  if (!body)
    return 1;
  for (size_t i = 0; i < body->stmt_count; i++) {
    if (!ctx->api->emit_stmt(ctx->cg, &body->stmts[i])) {
      return 0;
    }
  }
  return 1;
}

PieAsmGenResult pie_feature_do_while_codegen_stmt(PieAsmCodegenContext *ctx,
                                                  const PieIrStmt *stmt) {
  if (stmt->kind != PIE_IR_STMT_DO_WHILE) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;
  int continue_label = api->next_label(cg);
  int break_label = api->next_label(cg);

  api->emit(cg, ".Lloop_continue_%d:\n", continue_label);
  if (!api->push_loop(cg, continue_label, break_label, stmt->label_name)) {
    return PIE_ASM_GEN_ERROR;
  }
  int ok = emit_body(ctx, stmt->then_branch);
  api->pop_loop(cg);
  if (!ok) {
    return PIE_ASM_GEN_ERROR;
  }

  if (!api->emit_expr(cg, stmt->expr)) {
    return PIE_ASM_GEN_ERROR;
  }
  api->emit(cg, "    cmp rax, 0\n");
  api->emit(cg, "    jne .Lloop_continue_%d\n", continue_label);
  api->emit(cg, ".Lloop_break_%d:\n", break_label);
  return PIE_ASM_GEN_OK;
}
