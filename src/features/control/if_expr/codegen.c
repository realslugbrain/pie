#include "pie/backend/asm/asm_codegen.h"

#include <stdio.h>

PieAsmGenResult pie_feature_if_expr_codegen_expr(PieAsmCodegenContext *ctx,
                                                 const PieIrExpr *expr) {
  if (expr->kind != PIE_IR_EXPR_IF) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;

  int label = api->next_label(cg);

  if (!api->emit_expr(cg, expr->if_condition)) {
    return PIE_ASM_GEN_ERROR;
  }

  api->emit(cg, "    test rax, rax\n");
  api->emit(cg, "    je .Lif_else_%d\n", label);

  if (!api->emit_expr(cg, expr->if_then)) {
    return PIE_ASM_GEN_ERROR;
  }

  api->emit(cg, "    jmp .Lif_end_%d\n", label);
  api->emit(cg, ".Lif_else_%d:\n", label);

  if (!api->emit_expr(cg, expr->if_else)) {
    return PIE_ASM_GEN_ERROR;
  }

  api->emit(cg, ".Lif_end_%d:\n", label);

  return PIE_ASM_GEN_OK;
}
