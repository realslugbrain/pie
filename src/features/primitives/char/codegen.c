#include "pie/backend/asm/asm_codegen.h"

PieAsmGenResult pie_feature_char_codegen_expr(PieAsmCodegenContext *ctx,
                                              const PieIrExpr *expr) {
  if (expr->kind != PIE_IR_EXPR_CHAR) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  ctx->api->emit(ctx->cg, "    mov rax, %u\n", expr->char_value);
  return PIE_ASM_GEN_OK;
}
