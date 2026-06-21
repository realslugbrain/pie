#include "pie/backend/asm/asm_codegen.h"

#include <string.h>

PieAsmGenResult pie_feature_cast_codegen_expr(PieAsmCodegenContext *ctx,
                                              const PieIrExpr *expr) {
  if (expr->kind != PIE_IR_EXPR_CAST) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  if (!ctx->api->emit_expr(ctx->cg, expr->cast_inner)) {
    return PIE_ASM_GEN_ERROR;
  }

  PieIrTypeKind from = expr->cast_inner->type;
  PieIrTypeKind to = expr->cast_target_type;

  if (from == PIE_IR_TYPE_INT && to == PIE_IR_TYPE_FLOAT) {
    ctx->api->emit(ctx->cg, "    cvtsi2sd xmm0, rax\n");
    return PIE_ASM_GEN_OK;
  }

  if (from == PIE_IR_TYPE_FLOAT && to == PIE_IR_TYPE_INT) {
    ctx->api->emit(ctx->cg, "    cvttsd2si rax, xmm0\n");
    return PIE_ASM_GEN_OK;
  }

  if (from == PIE_IR_TYPE_INT && to == PIE_IR_TYPE_STRING) {
    ctx->api->emit(ctx->cg, "    mov rdi, rax\n");
    ctx->api->emit(ctx->cg, "    call pie_int_to_string\n");
    return PIE_ASM_GEN_OK;
  }

  if (from == PIE_IR_TYPE_FLOAT && to == PIE_IR_TYPE_STRING) {
    ctx->api->emit(ctx->cg, "    movsd xmm0, xmm0\n");
    ctx->api->emit(ctx->cg, "    call pie_float_to_string\n");
    return PIE_ASM_GEN_OK;
  }

  if (from == PIE_IR_TYPE_STRING && to == PIE_IR_TYPE_INT) {
    ctx->api->emit(ctx->cg, "    mov rdi, rax\n");
    ctx->api->emit(ctx->cg, "    mov rsi, rdx\n");
    ctx->api->emit(ctx->cg, "    call pie_string_to_int\n");
    return PIE_ASM_GEN_OK;
  }

  if (from == PIE_IR_TYPE_STRING && to == PIE_IR_TYPE_FLOAT) {
    ctx->api->emit(ctx->cg, "    mov rdi, rax\n");
    ctx->api->emit(ctx->cg, "    mov rsi, rdx\n");
    ctx->api->emit(ctx->cg, "    call pie_string_to_float\n");
    return PIE_ASM_GEN_OK;
  }

  return PIE_ASM_GEN_OK;
}
