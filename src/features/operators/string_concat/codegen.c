#include "pie/backend/asm/asm_codegen.h"

PieAsmGenResult
pie_feature_string_concat_codegen_expr(PieAsmCodegenContext *ctx,
                                       const PieIrExpr *expr) {
  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;

  if (expr->kind != PIE_IR_EXPR_BINARY || expr->op != '+' ||
      expr->op_text[1] != '+') {
    return PIE_ASM_GEN_NO_MATCH;
  }

  if (!api->emit_expr(cg, expr->left)) {
    return PIE_ASM_GEN_ERROR;
  }
  api->emit(cg, "    push rax\n");
  api->emit(cg, "    push rdx\n");

  if (!api->emit_expr(cg, expr->right)) {
    return PIE_ASM_GEN_ERROR;
  }

  api->emit(cg, "    mov r8, rax\n");
  api->emit(cg, "    mov r9, rdx\n");

  api->emit(cg, "    pop rdx\n");
  api->emit(cg, "    pop rax\n");

  api->emit(cg, "    mov rdi, rax\n");
  api->emit(cg, "    mov rsi, rdx\n");
  api->emit(cg, "    mov rdx, r8\n");
  api->emit(cg, "    mov rcx, r9\n");
  api->emit(cg, "    call pie_string_concat\n");

  return PIE_ASM_GEN_OK;
}
