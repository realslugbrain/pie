#include "pie/backend/asm/asm_codegen.h"

#include <string.h>

static PieAsmGenResult codegen_not(PieAsmCodegenContext *ctx,
                                   const PieIrExpr *expr) {
  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;

  if (!api->emit_expr(cg, expr->right)) {
    return PIE_ASM_GEN_ERROR;
  }
  api->emit(cg, "    cmp rax, 0\n");
  api->emit(cg, "    sete al\n");
  api->emit(cg, "    movzx rax, al\n");
  return PIE_ASM_GEN_OK;
}

static PieAsmGenResult codegen_and(PieAsmCodegenContext *ctx,
                                   const PieIrExpr *expr) {
  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;
  int false_label = api->next_label(cg);
  int end_label = api->next_label(cg);

  if (!api->emit_expr(cg, expr->left)) {
    return PIE_ASM_GEN_ERROR;
  }
  api->emit(cg, "    cmp rax, 0\n");
  api->emit(cg, "    je .Llogic_false_%d\n", false_label);
  if (!api->emit_expr(cg, expr->right)) {
    return PIE_ASM_GEN_ERROR;
  }
  api->emit(cg, "    cmp rax, 0\n");
  api->emit(cg, "    je .Llogic_false_%d\n", false_label);
  api->emit(cg, "    mov rax, 1\n");
  api->emit(cg, "    jmp .Llogic_end_%d\n", end_label);
  api->emit(cg, ".Llogic_false_%d:\n", false_label);
  api->emit(cg, "    xor rax, rax\n");
  api->emit(cg, ".Llogic_end_%d:\n", end_label);
  return PIE_ASM_GEN_OK;
}

static PieAsmGenResult codegen_or(PieAsmCodegenContext *ctx,
                                  const PieIrExpr *expr) {
  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;
  int true_label = api->next_label(cg);
  int end_label = api->next_label(cg);

  if (!api->emit_expr(cg, expr->left)) {
    return PIE_ASM_GEN_ERROR;
  }
  api->emit(cg, "    cmp rax, 0\n");
  api->emit(cg, "    jne .Llogic_true_%d\n", true_label);
  if (!api->emit_expr(cg, expr->right)) {
    return PIE_ASM_GEN_ERROR;
  }
  api->emit(cg, "    cmp rax, 0\n");
  api->emit(cg, "    jne .Llogic_true_%d\n", true_label);
  api->emit(cg, "    xor rax, rax\n");
  api->emit(cg, "    jmp .Llogic_end_%d\n", end_label);
  api->emit(cg, ".Llogic_true_%d:\n", true_label);
  api->emit(cg, "    mov rax, 1\n");
  api->emit(cg, ".Llogic_end_%d:\n", end_label);
  return PIE_ASM_GEN_OK;
}

PieAsmGenResult pie_feature_logical_codegen_expr(PieAsmCodegenContext *ctx,
                                                 const PieIrExpr *expr) {
  if (expr->kind == PIE_IR_EXPR_UNARY && strcmp(expr->op_text, "not") == 0) {
    return codegen_not(ctx, expr);
  }
  if (expr->kind == PIE_IR_EXPR_BINARY && strcmp(expr->op_text, "and") == 0) {
    return codegen_and(ctx, expr);
  }
  if (expr->kind == PIE_IR_EXPR_BINARY && strcmp(expr->op_text, "or") == 0) {
    return codegen_or(ctx, expr);
  }
  return PIE_ASM_GEN_NO_MATCH;
}
