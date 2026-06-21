#include "pie/backend/asm/asm_codegen.h"

PieAsmGenResult pie_feature_int_codegen_expr(PieAsmCodegenContext *ctx,
                                             const PieIrExpr *expr) {
  if (expr->kind != PIE_IR_EXPR_INT) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  if (expr->type_width == PIE_WIDTH_WIDE) {
    ctx->api->emit(ctx->cg, "    mov rdi, %lld\n", expr->int_value);
    ctx->api->emit(ctx->cg, "    call pie_int_wide_new\n");
  } else if (expr->type_width == PIE_WIDTH_8) {
    ctx->api->emit(ctx->cg, "    mov rax, %lld\n",
                   (long long)(signed char)(char)expr->int_value);
  } else if (expr->type_width == PIE_WIDTH_16) {
    ctx->api->emit(ctx->cg, "    mov rax, %lld\n",
                   (long long)(short)expr->int_value);
  } else if (expr->type_width == PIE_WIDTH_32) {
    ctx->api->emit(ctx->cg, "    mov eax, %lld\n", expr->int_value);
  } else {
    ctx->api->emit(ctx->cg, "    mov rax, %lld\n", expr->int_value);
  }
  return PIE_ASM_GEN_OK;
}
