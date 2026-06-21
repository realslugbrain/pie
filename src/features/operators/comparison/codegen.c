#include "pie/backend/asm/asm_codegen.h"

#include <string.h>

static const char *setcc_for_op(const char *op) {
  if (strcmp(op, "==") == 0) {
    return "sete";
  }
  if (strcmp(op, "!=") == 0) {
    return "setne";
  }
  if (strcmp(op, "<") == 0) {
    return "setl";
  }
  if (strcmp(op, "<=") == 0) {
    return "setle";
  }
  if (strcmp(op, ">") == 0) {
    return "setg";
  }
  if (strcmp(op, ">=") == 0) {
    return "setge";
  }
  return NULL;
}

PieAsmGenResult pie_feature_comparison_codegen_expr(PieAsmCodegenContext *ctx,
                                                    const PieIrExpr *expr) {
  if (expr->kind != PIE_IR_EXPR_BINARY || expr->type != PIE_IR_TYPE_BOOL) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  const char *setcc = setcc_for_op(expr->op_text);
  if (!setcc) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;
  if (!api->emit_expr(cg, expr->left)) {
    return PIE_ASM_GEN_ERROR;
  }

  int left_width = expr->left ? expr->left->type_width : 0;
  PieIrTypeKind left_type = expr->left ? expr->left->type : PIE_IR_TYPE_UNKNOWN;

  if (left_type == PIE_IR_TYPE_STRING ||
      (left_type == PIE_IR_TYPE_REF &&
       expr->left->ref_inner_type == PIE_IR_TYPE_STRING)) {
    api->emit(cg, "    push rdx\n");
    api->emit(cg, "    push rax\n");
    if (!api->emit_expr(cg, expr->right)) {
      return PIE_ASM_GEN_ERROR;
    }
    api->emit(cg, "    mov r8, rax\n");
    api->emit(cg, "    mov r9, rdx\n");
    api->emit(cg, "    pop rdi\n");
    api->emit(cg, "    pop rsi\n");
    api->emit(cg, "    mov rdx, r8\n");
    api->emit(cg, "    mov rcx, r9\n");
    api->emit(cg, "    call pie_string_eq\n");
    api->emit(cg, "    cmp rax, 1\n");
    api->emit(cg, "    %s al\n", setcc);
    api->emit(cg, "    movzx rax, al\n");
    return PIE_ASM_GEN_OK;
  }

  if (left_width == PIE_WIDTH_WIDE) {
    api->emit(cg, "    push rax\n");
    if (!api->emit_expr(cg, expr->right)) {
      return PIE_ASM_GEN_ERROR;
    }
    api->emit(cg, "    mov rsi, rax\n");
    api->emit(cg, "    pop rdi\n");
    if (expr->left && expr->left->type == PIE_IR_TYPE_FLOAT) {
      api->emit(cg, "    call pie_float_wide_cmp\n");
    } else {
      api->emit(cg, "    call pie_int_wide_cmp\n");
    }
    api->emit(cg, "    cmp rax, 0\n");
    api->emit(cg, "    %s al\n", setcc);
    api->emit(cg, "    movzx rax, al\n");
    return PIE_ASM_GEN_OK;
  }

  api->emit(cg, "    push rax\n");
  if (!api->emit_expr(cg, expr->right)) {
    return PIE_ASM_GEN_ERROR;
  }
  api->emit(cg, "    mov rbx, rax\n");
  api->emit(cg, "    pop rax\n");
  api->emit(cg, "    cmp rax, rbx\n");
  api->emit(cg, "    %s al\n", setcc);
  api->emit(cg, "    movzx rax, al\n");
  return PIE_ASM_GEN_OK;
}
