#include "pie/backend/asm/asm_codegen.h"

PieAsmGenResult pie_feature_string_codegen_expr(PieAsmCodegenContext *ctx,
                                                const PieIrExpr *expr) {
  if (expr->kind != PIE_IR_EXPR_STRING) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  size_t id = 0;
  if (!ctx->api->add_string(ctx->cg, expr->string_value, expr->string_len,
                            &id)) {
    return PIE_ASM_GEN_ERROR;
  }
  ctx->api->emit(ctx->cg, "    mov rax, pie_str_%zu\n", id);
  ctx->api->emit(ctx->cg, "    mov rdx, %zu\n", expr->string_len);
  return PIE_ASM_GEN_OK;
}
