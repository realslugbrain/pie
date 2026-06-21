#include "pie/backend/asm/asm_codegen.h"

PieAsmGenResult pie_feature_bool_codegen_expr(PieAsmCodegenContext *ctx,
                                              const PieIrExpr *expr) {
  if (expr->kind != PIE_IR_EXPR_BOOL) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  ctx->api->emit(ctx->cg, "    mov rax, %d\n", expr->bool_value ? 1 : 0);
  return PIE_ASM_GEN_OK;
}
