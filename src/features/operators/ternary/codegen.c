#include "pie/backend/asm/asm_codegen.h"

#include <stdio.h>

PieAsmGenResult pie_feature_ternary_codegen_expr(PieAsmCodegenContext *ctx,
                                                 const PieIrExpr *expr) {
  if (expr->kind != PIE_IR_EXPR_TERNARY) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;

  int label = api->next_label(cg);

  if (!api->emit_expr(cg, expr->left)) {
    return PIE_ASM_GEN_ERROR;
  }

  api->emit(cg, "    test rax, rax\n");
  api->emit(cg, "    je .Lternary_false_%d\n", label);

  if (!api->emit_expr(cg, expr->right)) {
    return PIE_ASM_GEN_ERROR;
  }

  api->emit(cg, "    jmp .Lternary_end_%d\n", label);
  api->emit(cg, ".Lternary_false_%d:\n", label);

  if (!api->emit_expr(cg, expr->ternary_false)) {
    return PIE_ASM_GEN_ERROR;
  }

  api->emit(cg, ".Lternary_end_%d:\n", label);

  return PIE_ASM_GEN_OK;
}
