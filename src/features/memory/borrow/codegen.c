#include "pie/backend/asm/asm_codegen.h"

#include <string.h>

PieAsmGenResult pie_feature_borrow_codegen_expr(PieAsmCodegenContext *ctx,
                                                const PieIrExpr *expr) {
  if (expr->kind != PIE_IR_EXPR_UNARY ||
      (strcmp(expr->op_text, "&") != 0 && strcmp(expr->op_text, "&mut") != 0 &&
       strcmp(expr->op_text, "refview") != 0)) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  if (strcmp(expr->op_text, "refview") == 0) {
    if (!expr->right || expr->right->type != PIE_IR_TYPE_REF_MUT) {
      ctx->api->error(ctx->cg,
                      "reference view coercion requires &mut string input");
      return PIE_ASM_GEN_ERROR;
    }
    if (!ctx->api->emit_expr(ctx->cg, expr->right)) {
      return PIE_ASM_GEN_ERROR;
    }
    ctx->api->emit(ctx->cg, "    mov rbx, rax\n");
    ctx->api->emit(ctx->cg, "    mov rax, [rbx]\n");
    ctx->api->emit(ctx->cg, "    mov rdx, [rbx+8]\n");
    return PIE_ASM_GEN_OK;
  }

  if (strcmp(expr->op_text, "&mut") == 0) {
    if (!expr->right || expr->right->kind != PIE_IR_EXPR_LOCAL) {
      ctx->api->error(ctx->cg,
                      "mutable borrow codegen currently requires a local");
      return PIE_ASM_GEN_ERROR;
    }

    PieAsmSymbol symbol;
    if (!ctx->api->find_local(ctx->cg, expr->right->local_id, &symbol)) {
      ctx->api->errorf(ctx->cg, "undefined local '%%%zu' for mutable borrow",
                       expr->right->local_id);
      return PIE_ASM_GEN_ERROR;
    }
    ctx->api->emit(ctx->cg, "    lea rax, [rbp-%d]\n", symbol.offset);
    return PIE_ASM_GEN_OK;
  }

  if (!ctx->api->emit_expr(ctx->cg, expr->right)) {
    return PIE_ASM_GEN_ERROR;
  }
  return PIE_ASM_GEN_OK;
}
