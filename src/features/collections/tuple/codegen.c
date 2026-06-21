#include "pie/backend/asm/asm_codegen.h"

#include <stdio.h>
#include <string.h>

PieAsmGenResult pie_feature_tuple_codegen_expr(PieAsmCodegenContext *ctx,
                                               const PieIrExpr *expr) {
  if (expr->kind != PIE_IR_EXPR_TUPLE) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  size_t total_size = expr->tuple_element_count * 8;

  ctx->api->emit(ctx->cg, "    sub rsp, %zu\n", total_size);

  size_t offset = 0;
  for (size_t i = 0; i < expr->tuple_element_count; i++) {
    if (!ctx->api->emit_expr(ctx->cg, expr->tuple_elements[i])) {
      return PIE_ASM_GEN_ERROR;
    }
    ctx->api->emit(ctx->cg, "    mov [rsp + %zu], rax\n", offset);
    offset += 8;
  }

  ctx->api->emit(ctx->cg, "    mov rax, rsp\n");
  return PIE_ASM_GEN_OK;
}
