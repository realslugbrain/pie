#include "pie/backend/asm/asm_codegen.h"

#include <stdio.h>
#include <string.h>

PieAsmGenResult pie_feature_list_codegen_expr(PieAsmCodegenContext *ctx,
                                              const PieIrExpr *expr) {
  if (expr->kind == PIE_IR_EXPR_LIST) {
    ctx->api->emit(ctx->cg, "    sub rsp, 24\n");
    ctx->api->emit(ctx->cg, "    mov qword [rsp], 0\n");
    ctx->api->emit(ctx->cg, "    mov qword [rsp+8], 0\n");
    ctx->api->emit(ctx->cg, "    mov qword [rsp+16], 0\n");

    for (size_t i = 0; i < expr->list_element_count; i++) {
      if (!ctx->api->emit_expr(ctx->cg, expr->list_elements[i])) {
        return PIE_ASM_GEN_ERROR;
      }
      ctx->api->emit(ctx->cg, "    mov rdi, rsp\n");
      ctx->api->emit(ctx->cg, "    mov rsi, rax\n");
      ctx->api->emit(ctx->cg, "    call pie_list_push\n");
    }

    ctx->api->emit(ctx->cg, "    mov rax, rsp\n");
    return PIE_ASM_GEN_OK;
  }

  if (expr->kind == PIE_IR_EXPR_INDEX) {
    if (expr->left->type == PIE_IR_TYPE_STRING) {
      if (!ctx->api->emit_expr(ctx->cg, expr->left)) {
        return PIE_ASM_GEN_ERROR;
      }
      ctx->api->emit(ctx->cg, "    push rax\n");
      ctx->api->emit(ctx->cg, "    push rdx\n");
      if (!ctx->api->emit_expr(ctx->cg, expr->right)) {
        return PIE_ASM_GEN_ERROR;
      }
      ctx->api->emit(ctx->cg, "    pop rdx\n");
      ctx->api->emit(ctx->cg, "    pop rcx\n");
      ctx->api->emit(ctx->cg, "    cmp rax, rdx\n");
      int oob_label = ctx->api->next_label(ctx->cg);
      int end_label = ctx->api->next_label(ctx->cg);
      ctx->api->emit(ctx->cg, "    jae .Lstring_index_oob_%d\n", oob_label);
      ctx->api->emit(ctx->cg, "    movzx rax, byte [rcx + rax]\n");
      ctx->api->emit(ctx->cg, "    jmp .Lstring_index_end_%d\n", end_label);
      ctx->api->emit(ctx->cg, ".Lstring_index_oob_%d:\n", oob_label);
      ctx->api->emit(ctx->cg, "    call pie_string_index_oob\n");
      ctx->api->emit(ctx->cg, ".Lstring_index_end_%d:\n", end_label);
      return PIE_ASM_GEN_OK;
    }
    if (expr->left->type != PIE_IR_TYPE_LIST) {
      return PIE_ASM_GEN_NO_MATCH;
    }
    if (!ctx->api->emit_expr(ctx->cg, expr->left)) {
      return PIE_ASM_GEN_ERROR;
    }
    ctx->api->emit(ctx->cg, "    push rax\n");
    if (!ctx->api->emit_expr(ctx->cg, expr->right)) {
      return PIE_ASM_GEN_ERROR;
    }
    ctx->api->emit(ctx->cg, "    mov rdi, [rsp]\n");
    ctx->api->emit(ctx->cg, "    add rsp, 8\n");
    ctx->api->emit(ctx->cg, "    mov rsi, rax\n");
    ctx->api->emit(ctx->cg, "    call pie_list_get\n");
    return PIE_ASM_GEN_OK;
  }

  return PIE_ASM_GEN_NO_MATCH;
}

PieAsmGenResult pie_feature_list_codegen_index_assign(PieAsmCodegenContext *ctx,
                                                      const PieIrStmt *stmt) {
  if (stmt->kind != PIE_IR_STMT_INDEX_ASSIGN) {
    return PIE_ASM_GEN_NO_MATCH;
  }
  if (stmt->index_target->type != PIE_IR_TYPE_LIST) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;

  if (!api->emit_expr(cg, stmt->index_target)) {
    return PIE_ASM_GEN_ERROR;
  }
  api->emit(cg, "    push rax\n");

  if (!api->emit_expr(cg, stmt->index_expr)) {
    return PIE_ASM_GEN_ERROR;
  }
  api->emit(cg, "    push rax\n");

  if (!api->emit_expr(cg, stmt->expr)) {
    return PIE_ASM_GEN_ERROR;
  }

  api->emit(cg, "    mov rcx, rax\n");
  api->emit(cg, "    pop rsi\n");
  api->emit(cg, "    pop rdi\n");
  api->emit(cg, "    mov rdx, rcx\n");
  api->emit(cg, "    call pie_list_set\n");

  return PIE_ASM_GEN_OK;
}
