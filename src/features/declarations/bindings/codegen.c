#include "pie/backend/asm/asm_codegen.h"

#include <string.h>

static int emit_assignment_op(PieAsmCodegenContext *ctx, const char *op,
                              int offset) {
  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;

  api->emit(cg, "    mov rbx, rax\n");
  api->emit(cg, "    mov rax, [rbp-%d]\n", offset);
  if (strcmp(op, "+=") == 0) {
    api->emit(cg, "    add rax, rbx\n");
  } else if (strcmp(op, "-=") == 0) {
    api->emit(cg, "    sub rax, rbx\n");
  } else if (strcmp(op, "*=") == 0) {
    api->emit(cg, "    imul rax, rbx\n");
  } else if (strcmp(op, "/=") == 0) {
    api->emit(cg, "    cqo\n");
    api->emit(cg, "    idiv rbx\n");
  } else if (strcmp(op, "%=") == 0) {
    api->emit(cg, "    cqo\n");
    api->emit(cg, "    idiv rbx\n");
    api->emit(cg, "    mov rax, rdx\n");
  } else if (strcmp(op, "**=") == 0) {
    api->emit(cg, "    mov rdi, rax\n");
    api->emit(cg, "    mov rsi, rbx\n");
    api->emit(cg, "    call pie_int_power\n");
  } else {
    api->errorf(cg, "unsupported assignment operator '%s'", op);
    return 0;
  }
  return 1;
}

static int is_fat_ref(const PieAsmSymbol *symbol) {
  return symbol->type == PIE_IR_TYPE_REF &&
         symbol->ref_inner_type == PIE_IR_TYPE_STRING;
}

static int is_ref_mut_string(const PieAsmSymbol *symbol) {
  return symbol->type == PIE_IR_TYPE_REF_MUT &&
         symbol->ref_inner_type == PIE_IR_TYPE_STRING;
}

static void emit_ref_mut_string_view(PieAsmCodegenContext *ctx) {
  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;
  api->emit(cg, "    mov rbx, rax\n");
  api->emit(cg, "    mov rax, [rbx]\n");
  api->emit(cg, "    mov rdx, [rbx+8]\n");
}

static void emit_store(PieAsmCodegenContext *ctx, const PieAsmSymbol *symbol) {
  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;
  if (symbol->type == PIE_IR_TYPE_REF_MUT) {
    api->emit(cg, "    mov [rbp-%d], rax\n", symbol->offset);
  } else if (symbol->type == PIE_IR_TYPE_FLOAT) {
    if (symbol->type_width == PIE_WIDTH_WIDE) {
      api->emit(cg, "    mov [rbp-%d], rax\n", symbol->offset);
    } else if (symbol->type_width == PIE_WIDTH_32) {
      api->emit(cg, "    movss [rbp-%d], xmm0\n", symbol->offset);
    } else {
      api->emit(cg, "    movsd [rbp-%d], xmm0\n", symbol->offset);
    }
  } else if (symbol->type_width == PIE_WIDTH_8) {
    api->emit(cg, "    mov byte [rbp-%d], al\n", symbol->offset);
  } else if (symbol->type_width == PIE_WIDTH_16) {
    api->emit(cg, "    mov word [rbp-%d], ax\n", symbol->offset);
  } else if (symbol->type_width == PIE_WIDTH_32) {
    api->emit(cg, "    mov dword [rbp-%d], eax\n", symbol->offset);
  } else {
    api->emit(cg, "    mov [rbp-%d], rax\n", symbol->offset);
  }
  if (symbol->type == PIE_IR_TYPE_STRING || is_fat_ref(symbol)) {
    api->emit(cg, "    mov [rbp-%d], rdx\n", symbol->offset - 8);
  }
  if (symbol->type == PIE_IR_TYPE_CLOSURE) {
    api->emit(cg, "    mov [rbp-%d], rdx\n", symbol->offset - 8);
  }
}

static void emit_load(PieAsmCodegenContext *ctx, const PieAsmSymbol *symbol) {
  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;
  if (symbol->type == PIE_IR_TYPE_REF_MUT) {
    api->emit(cg, "    mov rax, [rbp-%d]\n", symbol->offset);
  } else if (symbol->type == PIE_IR_TYPE_FLOAT) {
    if (symbol->type_width == PIE_WIDTH_WIDE) {
      api->emit(cg, "    mov rax, [rbp-%d]\n", symbol->offset);
    } else if (symbol->type_width == PIE_WIDTH_32) {
      api->emit(cg, "    movss xmm0, [rbp-%d]\n", symbol->offset);
    } else {
      api->emit(cg, "    movsd xmm0, [rbp-%d]\n", symbol->offset);
    }
  } else if (symbol->type_width == PIE_WIDTH_8) {
    api->emit(cg, "    movzx rax, byte [rbp-%d]\n", symbol->offset);
  } else if (symbol->type_width == PIE_WIDTH_16) {
    api->emit(cg, "    movzx rax, word [rbp-%d]\n", symbol->offset);
  } else if (symbol->type_width == PIE_WIDTH_32) {
    api->emit(cg, "    movsxd rax, dword [rbp-%d]\n", symbol->offset);
  } else {
    api->emit(cg, "    mov rax, [rbp-%d]\n", symbol->offset);
  }
  if (symbol->type == PIE_IR_TYPE_STRING || is_fat_ref(symbol)) {
    api->emit(cg, "    mov rdx, [rbp-%d]\n", symbol->offset - 8);
  }
  if (symbol->type == PIE_IR_TYPE_CLOSURE) {
    api->emit(cg, "    mov rdx, [rbp-%d]\n", symbol->offset - 8);
  }
}

PieAsmGenResult pie_feature_bindings_codegen_stmt(PieAsmCodegenContext *ctx,
                                                  const PieIrStmt *stmt) {
  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;

  if (stmt->kind == PIE_IR_STMT_LET) {
    PieAsmSymbol symbol;
    PieIrTypeKind ref_inner_type = PIE_IR_TYPE_UNKNOWN;
    int ref_inner_width = PIE_WIDTH_INFER;
    if (stmt->expr->type == PIE_IR_TYPE_REF ||
        stmt->expr->type == PIE_IR_TYPE_REF_MUT) {
      ref_inner_type = stmt->expr->ref_inner_type;
      ref_inner_width = stmt->expr->ref_inner_width;
    }
    if (!api->add_local(cg, stmt->local_id, stmt->is_mut, stmt->expr->type,
                        stmt->expr->type_width, ref_inner_type, ref_inner_width,
                        &symbol)) {
      return PIE_ASM_GEN_ERROR;
    }
    if (!api->emit_expr(cg, stmt->expr)) {
      return PIE_ASM_GEN_ERROR;
    }
    if (symbol.type == PIE_IR_TYPE_REF &&
        stmt->expr->type == PIE_IR_TYPE_REF_MUT) {
      emit_ref_mut_string_view(ctx);
    }
    emit_store(ctx, &symbol);
    return PIE_ASM_GEN_OK;
  }

  if (stmt->kind == PIE_IR_STMT_ASSIGN) {
    PieAsmSymbol symbol;
    if (!api->find_local(cg, stmt->local_id, &symbol)) {
      api->errorf(cg, "cannot assign undefined local '%%%zu'", stmt->local_id);
      return PIE_ASM_GEN_ERROR;
    }
    if (!symbol.is_mut && symbol.type != PIE_IR_TYPE_REF_MUT) {
      api->errorf(cg, "cannot assign immutable local '%%%zu'", stmt->local_id);
      return PIE_ASM_GEN_ERROR;
    }
    if (!api->emit_expr(cg, stmt->expr)) {
      return PIE_ASM_GEN_ERROR;
    }
    if (symbol.type == PIE_IR_TYPE_REF_MUT) {
      if (strcmp(stmt->assign_op, "<-") != 0) {
        api->errorf(cg, "unsupported assignment operator '%s' for &mut ref",
                    stmt->assign_op);
        return PIE_ASM_GEN_ERROR;
      }
      if (stmt->expr->type == PIE_IR_TYPE_REF_MUT) {
        emit_ref_mut_string_view(ctx);
      }
      api->emit(cg, "    mov rcx, [rbp-%d]\n", symbol.offset);
      api->emit(cg, "    mov [rcx], rax\n");
      if (is_fat_ref(&symbol) || is_ref_mut_string(&symbol)) {
        api->emit(cg, "    mov [rcx+8], rdx\n");
      }
      return PIE_ASM_GEN_OK;
    }
    if (strcmp(stmt->assign_op, "<-") != 0) {
      if (!emit_assignment_op(ctx, stmt->assign_op, symbol.offset)) {
        return PIE_ASM_GEN_ERROR;
      }
    }
    emit_store(ctx, &symbol);
    return PIE_ASM_GEN_OK;
  }

  return PIE_ASM_GEN_NO_MATCH;
}

PieAsmGenResult pie_feature_bindings_codegen_expr(PieAsmCodegenContext *ctx,
                                                  const PieIrExpr *expr) {
  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;

  if (expr->kind != PIE_IR_EXPR_LOCAL) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  PieAsmSymbol symbol;
  if (!api->find_local(cg, expr->local_id, &symbol)) {
    api->errorf(cg, "undefined local '%%%zu'", expr->local_id);
    return PIE_ASM_GEN_ERROR;
  }
  emit_load(ctx, &symbol);
  return PIE_ASM_GEN_OK;
}
