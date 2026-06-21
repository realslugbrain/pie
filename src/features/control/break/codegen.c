#include "pie/backend/asm/asm_codegen.h"

PieAsmGenResult pie_feature_break_codegen_stmt(PieAsmCodegenContext *ctx,
                                               const PieIrStmt *stmt) {
  if (stmt->kind != PIE_IR_STMT_BREAK) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  int continue_label = 0;
  int break_label = 0;

  if (stmt->label_name) {
    if (!ctx->api->find_loop_label(ctx->cg, stmt->label_name, &continue_label,
                                   &break_label)) {
      ctx->api->errorf(ctx->cg, "label '%s' not found or not a loop",
                       stmt->label_name);
      return PIE_ASM_GEN_ERROR;
    }
  } else {
    if (!ctx->api->current_loop(ctx->cg, &continue_label, &break_label)) {
      ctx->api->error(ctx->cg, "break used outside of a loop during codegen");
      return PIE_ASM_GEN_ERROR;
    }
  }
  (void)continue_label;
  ctx->api->emit(ctx->cg, "    jmp .Lloop_break_%d\n", break_label);
  return PIE_ASM_GEN_OK;
}
