#include "pie/backend/asm/asm_codegen.h"

#include <string.h>

PieAsmGenResult pie_feature_map_codegen_expr(PieAsmCodegenContext *ctx,
                                             const PieIrExpr *expr) {
  if (expr->kind == PIE_IR_EXPR_MAP) {
    ctx->api->emit(ctx->cg, "    push r12\n");
    ctx->api->emit(ctx->cg, "    sub rsp, 8\n");
    ctx->api->emit(ctx->cg, "    call pie_map_create\n");
    ctx->api->emit(ctx->cg, "    add rsp, 8\n");
    ctx->api->emit(ctx->cg, "    mov r12, rax\n");

    for (size_t i = 0; i < expr->map_entry_count; i++) {
      if (!ctx->api->emit_expr(ctx->cg, expr->map_values[i])) {
        ctx->api->emit(ctx->cg, "    pop r12\n");
        return PIE_ASM_GEN_ERROR;
      }
      ctx->api->emit(ctx->cg, "    push rax\n");

      if (expr->map_keys[i]->kind == PIE_IR_EXPR_STRING) {
        size_t str_id = 0;
        if (!ctx->api->add_string(ctx->cg, expr->map_keys[i]->string_value,
                                  expr->map_keys[i]->string_len, &str_id)) {
          ctx->api->emit(ctx->cg, "    pop rax\n");
          ctx->api->emit(ctx->cg, "    pop r12\n");
          return PIE_ASM_GEN_ERROR;
        }
        ctx->api->emit(ctx->cg, "    lea rsi, [rel pie_str_%zu]\n", str_id);
        ctx->api->emit(ctx->cg, "    mov rdx, %zu\n",
                       expr->map_keys[i]->string_len);
      } else {
        if (!ctx->api->emit_expr(ctx->cg, expr->map_keys[i])) {
          ctx->api->emit(ctx->cg, "    pop rax\n");
          ctx->api->emit(ctx->cg, "    pop r12\n");
          return PIE_ASM_GEN_ERROR;
        }
        ctx->api->emit(ctx->cg, "    mov rsi, rax\n");
        ctx->api->emit(ctx->cg, "    mov rdx, 1\n");
      }

      ctx->api->emit(ctx->cg, "    mov rdi, r12\n");
      ctx->api->emit(ctx->cg, "    pop rcx\n");
      ctx->api->emit(ctx->cg, "    sub rsp, 8\n");
      ctx->api->emit(ctx->cg, "    call pie_map_put\n");
      ctx->api->emit(ctx->cg, "    add rsp, 8\n");
    }

    ctx->api->emit(ctx->cg, "    mov rax, r12\n");
    ctx->api->emit(ctx->cg, "    pop r12\n");
    return PIE_ASM_GEN_OK;
  }

  if (expr->kind == PIE_IR_EXPR_INDEX) {
    if (!ctx->api->emit_expr(ctx->cg, expr->left)) {
      return PIE_ASM_GEN_ERROR;
    }
    ctx->api->emit(ctx->cg, "    push rax\n");

    if (expr->right->kind == PIE_IR_EXPR_STRING) {
      size_t str_id = 0;
      if (!ctx->api->add_string(ctx->cg, expr->right->string_value,
                                expr->right->string_len, &str_id)) {
        return PIE_ASM_GEN_ERROR;
      }
      ctx->api->emit(ctx->cg, "    pop rdi\n");
      ctx->api->emit(ctx->cg, "    lea rsi, [rel pie_str_%zu]\n", str_id);
      ctx->api->emit(ctx->cg, "    mov rdx, %zu\n", expr->right->string_len);
    } else {
      if (!ctx->api->emit_expr(ctx->cg, expr->right)) {
        return PIE_ASM_GEN_ERROR;
      }
      ctx->api->emit(ctx->cg, "    mov rdi, [rsp]\n");
      ctx->api->emit(ctx->cg, "    add rsp, 8\n");
      ctx->api->emit(ctx->cg, "    mov rsi, rax\n");
      ctx->api->emit(ctx->cg, "    mov rdx, 1\n");
    }

    ctx->api->emit(ctx->cg, "    call pie_map_get\n");
    return PIE_ASM_GEN_OK;
  }

  return PIE_ASM_GEN_NO_MATCH;
}

PieAsmGenResult pie_feature_map_codegen_index_assign(PieAsmCodegenContext *ctx,
                                                     const PieIrStmt *stmt) {
  if (stmt->kind != PIE_IR_STMT_INDEX_ASSIGN) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;

  if (!api->emit_expr(cg, stmt->index_target)) {
    return PIE_ASM_GEN_ERROR;
  }
  api->emit(cg, "    push rax\n");

  if (!api->emit_expr(cg, stmt->expr)) {
    return PIE_ASM_GEN_ERROR;
  }
  api->emit(cg, "    push rax\n");

  if (stmt->index_expr->kind == PIE_IR_EXPR_STRING) {
    size_t str_id = 0;
    if (!api->add_string(cg, stmt->index_expr->string_value,
                         stmt->index_expr->string_len, &str_id)) {
      return PIE_ASM_GEN_ERROR;
    }
    api->emit(cg, "    pop rcx\n");
    api->emit(cg, "    pop rdi\n");
    api->emit(cg, "    lea rsi, [rel pie_str_%zu]\n", str_id);
    api->emit(cg, "    mov rdx, %zu\n", stmt->index_expr->string_len);
    api->emit(cg, "    call pie_map_put\n");
  } else {
    api->emit(cg, "    pop rcx\n");
    api->emit(cg, "    pop rdi\n");
    return PIE_ASM_GEN_NO_MATCH;
  }

  return PIE_ASM_GEN_OK;
}
