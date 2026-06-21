#include "pie/backend/asm/asm_codegen.h"

#include <stdlib.h>
#include <string.h>

PieAsmGenResult pie_feature_enums_codegen_stmt(PieAsmCodegenContext *ctx,
                                               const PieIrStmt *stmt) {
  if (stmt->kind == PIE_IR_STMT_STRUCT) {
    return PIE_ASM_GEN_OK;
  }

  if (stmt->kind == PIE_IR_STMT_MATCH) {
    if (!ctx->api->emit_expr(ctx->cg, stmt->match_target)) {
      return PIE_ASM_GEN_ERROR;
    }
    ctx->api->emit(ctx->cg, "    mov r10, rax\n");
    ctx->api->emit(ctx->cg, "    cmp r10, 4096\n");
    int is_tag_label = ctx->api->next_label(ctx->cg);
    int after_tag_label = ctx->api->next_label(ctx->cg);
    ctx->api->emit(ctx->cg, "    jl .Lis_tag_%d\n", is_tag_label);
    ctx->api->emit(ctx->cg, "    mov rax, [r10]\n");
    ctx->api->emit(ctx->cg, "    jmp .Lafter_tag_%d\n", after_tag_label);
    ctx->api->emit(ctx->cg, ".Lis_tag_%d:\n", is_tag_label);
    ctx->api->emit(ctx->cg, "    mov rax, r10\n");
    ctx->api->emit(ctx->cg, ".Lafter_tag_%d:\n", after_tag_label);

    int end_label = ctx->api->next_label(ctx->cg);
    int *case_labels = NULL;
    if (stmt->match_case_count > 0) {
      case_labels = (int *)calloc(stmt->match_case_count, sizeof(int));
      for (size_t i = 0; i < stmt->match_case_count; i++) {
        case_labels[i] = ctx->api->next_label(ctx->cg);
      }
    }

    for (size_t i = 0; i < stmt->match_case_count; i++) {
      ctx->api->emit(ctx->cg, "    cmp rax, %d\n", stmt->match_case_tags[i]);
      ctx->api->emit(ctx->cg, "    je .Lmatch_%d\n", case_labels[i]);
    }

    if (stmt->match_default) {
      for (size_t i = 0; i < stmt->match_default->stmt_count; i++) {
        if (!ctx->api->emit_stmt(ctx->cg, &stmt->match_default->stmts[i])) {
          free(case_labels);
          return PIE_ASM_GEN_ERROR;
        }
      }
    }
    ctx->api->emit(ctx->cg, "    jmp .Lmatch_end_%d\n", end_label);

    for (size_t i = 0; i < stmt->match_case_count; i++) {
      ctx->api->emit(ctx->cg, ".Lmatch_%d:\n", case_labels[i]);

      size_t offset = 8;
      for (size_t j = 0; j < stmt->match_case_binding_counts[i]; j++) {
        size_t local_id = stmt->match_case_binding_ids[i][j];
        PieAsmSymbol sym;
        if (!ctx->api->find_local(ctx->cg, local_id, &sym)) {
          ctx->api->errorf(ctx->cg, "undefined match binding %zu", local_id);
          free(case_labels);
          return PIE_ASM_GEN_ERROR;
        }

        if (sym.type == PIE_IR_TYPE_STRING || sym.type == PIE_IR_TYPE_REF) {
          ctx->api->emit(ctx->cg, "    mov rax, [r10+%zu]\n", offset);
          ctx->api->emit(ctx->cg, "    mov rdx, [r10+%zu]\n", offset + 8);
          ctx->api->emit(ctx->cg, "    mov [rbp-%d], rax\n", sym.offset);
          ctx->api->emit(ctx->cg, "    mov [rbp-%d], rdx\n", sym.offset - 8);
          offset += 16;
        } else if (sym.type == PIE_IR_TYPE_FLOAT) {
          if (sym.type_width == PIE_WIDTH_32) {
            ctx->api->emit(ctx->cg, "    movss xmm0, [r10+%zu]\n", offset);
            ctx->api->emit(ctx->cg, "    movss [rbp-%d], xmm0\n", sym.offset);
          } else if (sym.type_width == PIE_WIDTH_WIDE) {
            ctx->api->emit(ctx->cg, "    mov rax, [r10+%zu]\n", offset);
            ctx->api->emit(ctx->cg, "    mov [rbp-%d], rax\n", sym.offset);
          } else {
            ctx->api->emit(ctx->cg, "    movsd xmm0, [r10+%zu]\n", offset);
            ctx->api->emit(ctx->cg, "    movsd [rbp-%d], xmm0\n", sym.offset);
          }
          offset += 8;
        } else {
          ctx->api->emit(ctx->cg, "    mov rax, [r10+%zu]\n", offset);
          ctx->api->emit(ctx->cg, "    mov [rbp-%d], rax\n", sym.offset);
          offset += 8;
        }
      }

      if (stmt->match_case_bodies[i]) {
        for (size_t j = 0; j < stmt->match_case_bodies[i]->stmt_count; j++) {
          if (!ctx->api->emit_stmt(ctx->cg,
                                   &stmt->match_case_bodies[i]->stmts[j])) {
            free(case_labels);
            return PIE_ASM_GEN_ERROR;
          }
        }
      }
      ctx->api->emit(ctx->cg, "    jmp .Lmatch_end_%d\n", end_label);
    }

    ctx->api->emit(ctx->cg, ".Lmatch_end_%d:\n", end_label);
    free(case_labels);
    return PIE_ASM_GEN_OK;
  }

  return PIE_ASM_GEN_NO_MATCH;
}

PieAsmGenResult pie_feature_enums_codegen_expr(PieAsmCodegenContext *ctx,
                                               const PieIrExpr *expr) {
  if (expr->kind == PIE_IR_EXPR_VARIANT) {
    if (expr->call_arg_count > 0) {
      size_t total = 8;
      for (size_t i = 0; i < expr->call_arg_count; i++) {
        if (expr->call_args[i].expr->type == PIE_IR_TYPE_STRING ||
            expr->call_args[i].expr->type == PIE_IR_TYPE_REF) {
          total += 16;
        } else {
          total += 8;
        }
      }

      ctx->api->emit(ctx->cg, "    push r12\n");
      ctx->api->emit(ctx->cg, "    sub rsp, 8\n");
      ctx->api->emit(ctx->cg, "    mov rdi, %zu\n", total);
      ctx->api->emit(ctx->cg, "    call pie_malloc\n");
      ctx->api->emit(ctx->cg, "    add rsp, 8\n");
      ctx->api->emit(ctx->cg, "    mov r12, rax\n");
      ctx->api->emit(ctx->cg, "    mov qword [r12], %d\n", expr->variant_tag);

      size_t offset = 8;
      for (size_t i = 0; i < expr->call_arg_count; i++) {
        if (!ctx->api->emit_expr(ctx->cg, expr->call_args[i].expr)) {
          ctx->api->emit(ctx->cg, "    pop r12\n");
          return PIE_ASM_GEN_ERROR;
        }
        if (expr->call_args[i].expr->type == PIE_IR_TYPE_STRING ||
            expr->call_args[i].expr->type == PIE_IR_TYPE_REF) {
          ctx->api->emit(ctx->cg, "    mov [r12+%zu], rax\n", offset);
          ctx->api->emit(ctx->cg, "    mov [r12+%zu], rdx\n", offset + 8);
          offset += 16;
        } else if (expr->call_args[i].expr->type == PIE_IR_TYPE_FLOAT) {
          if (expr->call_args[i].expr->type_width == PIE_WIDTH_32) {
            ctx->api->emit(ctx->cg, "    movss [r12+%zu], xmm0\n", offset);
          } else if (expr->call_args[i].expr->type_width == PIE_WIDTH_WIDE) {
            ctx->api->emit(ctx->cg, "    mov [r12+%zu], rax\n", offset);
          } else {
            ctx->api->emit(ctx->cg, "    movsd [r12+%zu], xmm0\n", offset);
          }
          offset += 8;
        } else {
          ctx->api->emit(ctx->cg, "    mov [r12+%zu], rax\n", offset);
          offset += 8;
        }
      }
      ctx->api->emit(ctx->cg, "    mov rax, r12\n");
      ctx->api->emit(ctx->cg, "    pop r12\n");
    } else {
      ctx->api->emit(ctx->cg, "    mov rax, %d\n", expr->variant_tag);
    }
    return PIE_ASM_GEN_OK;
  }

  if (expr->kind == PIE_IR_EXPR_MATCH) {
    if (!ctx->api->emit_expr(ctx->cg, expr->match_expr_target)) {
      return PIE_ASM_GEN_ERROR;
    }
    ctx->api->emit(ctx->cg, "    mov r10, rax\n");
    ctx->api->emit(ctx->cg, "    cmp r10, 4096\n");
    int is_tag_label = ctx->api->next_label(ctx->cg);
    int after_tag_label = ctx->api->next_label(ctx->cg);
    ctx->api->emit(ctx->cg, "    jl .Lmatch_expr_is_tag_%d\n", is_tag_label);
    ctx->api->emit(ctx->cg, "    mov rax, [r10]\n");
    ctx->api->emit(ctx->cg, "    jmp .Lmatch_expr_after_tag_%d\n",
                   after_tag_label);
    ctx->api->emit(ctx->cg, ".Lmatch_expr_is_tag_%d:\n", is_tag_label);
    ctx->api->emit(ctx->cg, "    mov rax, r10\n");
    ctx->api->emit(ctx->cg, ".Lmatch_expr_after_tag_%d:\n", after_tag_label);

    int end_label = ctx->api->next_label(ctx->cg);
    int *case_labels = NULL;
    if (expr->match_expr_case_count > 0) {
      case_labels = (int *)calloc(expr->match_expr_case_count, sizeof(int));
      for (size_t i = 0; i < expr->match_expr_case_count; i++) {
        case_labels[i] = ctx->api->next_label(ctx->cg);
      }
    }

    for (size_t i = 0; i < expr->match_expr_case_count; i++) {
      ctx->api->emit(ctx->cg, "    cmp rax, %d\n",
                     expr->match_expr_case_tags[i]);
      ctx->api->emit(ctx->cg, "    je .Lmatch_expr_%d\n", case_labels[i]);
    }

    if (expr->match_expr_default) {
      for (size_t i = 0; i < expr->match_expr_default->stmt_count; i++) {
        if (!ctx->api->emit_stmt(ctx->cg,
                                 &expr->match_expr_default->stmts[i])) {
          free(case_labels);
          return PIE_ASM_GEN_ERROR;
        }
      }
      if (expr->match_expr_default_value) {
        if (!ctx->api->emit_expr(ctx->cg, expr->match_expr_default_value)) {
          free(case_labels);
          return PIE_ASM_GEN_ERROR;
        }
      }
    }
    ctx->api->emit(ctx->cg, "    jmp .Lmatch_expr_end_%d\n", end_label);

    for (size_t i = 0; i < expr->match_expr_case_count; i++) {
      ctx->api->emit(ctx->cg, ".Lmatch_expr_%d:\n", case_labels[i]);

      size_t offset = 8;
      for (size_t j = 0; j < expr->match_expr_case_binding_counts[i]; j++) {
        size_t local_id = expr->match_expr_case_binding_ids[i][j];
        PieAsmSymbol sym;
        if (!ctx->api->find_local(ctx->cg, local_id, &sym)) {
          ctx->api->errorf(ctx->cg, "undefined match expression binding %zu",
                           local_id);
          free(case_labels);
          return PIE_ASM_GEN_ERROR;
        }

        if (sym.type == PIE_IR_TYPE_STRING || sym.type == PIE_IR_TYPE_REF) {
          ctx->api->emit(ctx->cg, "    mov rax, [r10+%zu]\n", offset);
          ctx->api->emit(ctx->cg, "    mov rdx, [r10+%zu]\n", offset + 8);
          ctx->api->emit(ctx->cg, "    mov [rbp-%d], rax\n", sym.offset);
          ctx->api->emit(ctx->cg, "    mov [rbp-%d], rdx\n", sym.offset - 8);
          offset += 16;
        } else if (sym.type == PIE_IR_TYPE_FLOAT) {
          if (sym.type_width == PIE_WIDTH_32) {
            ctx->api->emit(ctx->cg, "    movss xmm0, [r10+%zu]\n", offset);
            ctx->api->emit(ctx->cg, "    movss [rbp-%d], xmm0\n", sym.offset);
          } else if (sym.type_width == PIE_WIDTH_WIDE) {
            ctx->api->emit(ctx->cg, "    mov rax, [r10+%zu]\n", offset);
            ctx->api->emit(ctx->cg, "    mov [rbp-%d], rax\n", sym.offset);
          } else {
            ctx->api->emit(ctx->cg, "    movsd xmm0, [r10+%zu]\n", offset);
            ctx->api->emit(ctx->cg, "    movsd [rbp-%d], xmm0\n", sym.offset);
          }
          offset += 8;
        } else {
          ctx->api->emit(ctx->cg, "    mov rax, [r10+%zu]\n", offset);
          ctx->api->emit(ctx->cg, "    mov [rbp-%d], rax\n", sym.offset);
          offset += 8;
        }
      }

      if (expr->match_expr_case_bodies[i]) {
        for (size_t j = 0; j < expr->match_expr_case_bodies[i]->stmt_count;
             j++) {
          if (!ctx->api->emit_stmt(
                  ctx->cg, &expr->match_expr_case_bodies[i]->stmts[j])) {
            free(case_labels);
            return PIE_ASM_GEN_ERROR;
          }
        }
      }
      if (expr->match_expr_value_exprs && expr->match_expr_value_exprs[i]) {
        if (!ctx->api->emit_expr(ctx->cg, expr->match_expr_value_exprs[i])) {
          free(case_labels);
          return PIE_ASM_GEN_ERROR;
        }
      }
      ctx->api->emit(ctx->cg, "    jmp .Lmatch_expr_end_%d\n", end_label);
    }

    ctx->api->emit(ctx->cg, ".Lmatch_expr_end_%d:\n", end_label);
    free(case_labels);
    return PIE_ASM_GEN_OK;
  }

  return PIE_ASM_GEN_NO_MATCH;
}