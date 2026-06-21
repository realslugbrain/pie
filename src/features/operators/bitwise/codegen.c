#include "pie/backend/asm/asm_codegen.h"

#include <string.h>

static int is_bitwise_op(const char *op) {
  return strcmp(op, "&") == 0 || strcmp(op, "|") == 0 || strcmp(op, "^") == 0 ||
         strcmp(op, "<<") == 0 || strcmp(op, ">>") == 0;
}

PieAsmGenResult pie_feature_bitwise_codegen_expr(PieAsmCodegenContext *ctx,
                                                 const PieIrExpr *expr) {
  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;

  if (expr->kind == PIE_IR_EXPR_BINARY && is_bitwise_op(expr->op_text)) {
    if (!api->emit_expr(cg, expr->left)) {
      return PIE_ASM_GEN_ERROR;
    }
    api->emit(cg, "    push rax\n");
    if (!api->emit_expr(cg, expr->right)) {
      return PIE_ASM_GEN_ERROR;
    }
    api->emit(cg, "    mov rcx, rax\n");
    api->emit(cg, "    pop rbx\n");
    api->emit(cg, "    mov rax, rbx\n");

    if (strcmp(expr->op_text, "&") == 0) {
      api->emit(cg, "    and rax, rcx\n");
    } else if (strcmp(expr->op_text, "|") == 0) {
      api->emit(cg, "    or rax, rcx\n");
    } else if (strcmp(expr->op_text, "^") == 0) {
      api->emit(cg, "    xor rax, rcx\n");
    } else if (strcmp(expr->op_text, "<<") == 0) {
      api->emit(cg, "    shl rax, cl\n");
    } else if (strcmp(expr->op_text, ">>") == 0) {
      api->emit(cg, "    shr rax, cl\n");
    }

    return PIE_ASM_GEN_OK;
  }

  if (expr->kind == PIE_IR_EXPR_UNARY && expr->op == '~') {
    if (!api->emit_expr(cg, expr->right)) {
      return PIE_ASM_GEN_ERROR;
    }
    api->emit(cg, "    not rax\n");
    return PIE_ASM_GEN_OK;
  }

  return PIE_ASM_GEN_NO_MATCH;
}
