#include "pie/backend/asm/asm_codegen.h"

PieAsmGenResult pie_feature_null_codegen_expr(PieAsmCodegenContext *ctx,
                                              const PieIrExpr *expr) {
  if (expr->kind != PIE_IR_EXPR_NULL) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  ctx->api->emit(ctx->cg, "    xor rax, rax\n");
  return PIE_ASM_GEN_OK;
}
