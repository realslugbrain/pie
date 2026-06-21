#include "pie/backend/asm/asm_codegen.h"

PieAsmGenResult pie_feature_maybe_codegen_expr(PieAsmCodegenContext *ctx,
                                               const PieIrExpr *expr) {
  if (expr->kind != PIE_IR_EXPR_MAYBE) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  ctx->api->emit(ctx->cg, "    sub rsp, 8\n");
  ctx->api->emit(ctx->cg, "    call pie_maybe\n");
  ctx->api->emit(ctx->cg, "    add rsp, 8\n");
  ctx->api->emit(ctx->cg, "    movzx rax, al\n");
  return PIE_ASM_GEN_OK;
}
