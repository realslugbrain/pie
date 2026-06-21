#include "pie/backend/asm/asm_codegen.h"

#include <string.h>

static int emit_body(PieAsmCodegenContext *ctx, const PieIrProgram *body) {
  if (!body) {
    return 1;
  }
  for (size_t i = 0; i < body->stmt_count; i++) {
    if (!ctx->api->emit_stmt(ctx->cg, &body->stmts[i])) {
      return 0;
    }
  }
  return 1;
}

PieAsmGenResult pie_feature_unsafe_codegen_stmt(PieAsmCodegenContext *ctx,
                                                const PieIrStmt *stmt) {
  if (stmt->kind == PIE_IR_STMT_UNSAFE) {
    if (!emit_body(ctx, stmt->then_branch)) {
      return PIE_ASM_GEN_ERROR;
    }
    return PIE_ASM_GEN_OK;
  }

  if (stmt->kind != PIE_IR_STMT_RAW_STORE) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  if (!stmt->target || !stmt->expr) {
    ctx->api->error(ctx->cg, "raw pointer store is missing a target or value");
    return PIE_ASM_GEN_ERROR;
  }

  if (!ctx->api->emit_expr(ctx->cg, stmt->target)) {
    return PIE_ASM_GEN_ERROR;
  }
  ctx->api->emit(ctx->cg, "    push rax\n");
  if (!ctx->api->emit_expr(ctx->cg, stmt->expr)) {
    return PIE_ASM_GEN_ERROR;
  }
  ctx->api->emit(ctx->cg, "    pop rbx\n");

  if (stmt->expr->type == PIE_IR_TYPE_FLOAT) {
    if (stmt->expr->type_width == PIE_WIDTH_32) {
      ctx->api->emit(ctx->cg, "    movss [rbx], xmm0\n");
    } else if (stmt->expr->type_width == PIE_WIDTH_WIDE) {
      ctx->api->emit(ctx->cg, "    mov [rbx], rax\n");
    } else {
      ctx->api->emit(ctx->cg, "    movsd [rbx], xmm0\n");
    }
  } else if (stmt->expr->type == PIE_IR_TYPE_STRING ||
             stmt->expr->type == PIE_IR_TYPE_REF ||
             stmt->expr->type == PIE_IR_TYPE_REF_MUT) {
    ctx->api->emit(ctx->cg, "    mov [rbx], rax\n");
    ctx->api->emit(ctx->cg, "    mov [rbx+8], rdx\n");
  } else if (stmt->expr->type == PIE_IR_TYPE_BYTE ||
             stmt->expr->type == PIE_IR_TYPE_BOOL ||
             stmt->expr->type == PIE_IR_TYPE_CHAR ||
             stmt->expr->type_width == PIE_WIDTH_8) {
    ctx->api->emit(ctx->cg, "    mov [rbx], al\n");
  } else if (stmt->expr->type_width == PIE_WIDTH_16) {
    ctx->api->emit(ctx->cg, "    mov [rbx], ax\n");
  } else if (stmt->expr->type_width == PIE_WIDTH_32) {
    ctx->api->emit(ctx->cg, "    mov [rbx], eax\n");
  } else {
    ctx->api->emit(ctx->cg, "    mov [rbx], rax\n");
  }
  return PIE_ASM_GEN_OK;
}

static void emit_raw_load(PieAsmCodegenContext *ctx, const PieIrExpr *expr) {
  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;

  if (expr->type == PIE_IR_TYPE_FLOAT) {
    if (expr->type_width == PIE_WIDTH_32) {
      api->emit(cg, "    movss xmm0, [rax]\n");
    } else if (expr->type_width == PIE_WIDTH_WIDE) {
      api->emit(cg, "    mov rax, [rax]\n");
    } else {
      api->emit(cg, "    movsd xmm0, [rax]\n");
    }
    return;
  }

  if (expr->type == PIE_IR_TYPE_STRING || expr->type == PIE_IR_TYPE_REF ||
      expr->type == PIE_IR_TYPE_REF_MUT) {
    api->emit(cg, "    mov rbx, rax\n");
    api->emit(cg, "    mov rax, [rbx]\n");
    api->emit(cg, "    mov rdx, [rbx+8]\n");
    return;
  }

  if (expr->type == PIE_IR_TYPE_BYTE || expr->type == PIE_IR_TYPE_BOOL ||
      expr->type == PIE_IR_TYPE_CHAR || expr->type_width == PIE_WIDTH_8) {
    api->emit(cg, "    movzx rax, byte [rax]\n");
  } else if (expr->type_width == PIE_WIDTH_16) {
    api->emit(cg, "    movzx rax, word [rax]\n");
  } else if (expr->type_width == PIE_WIDTH_32) {
    api->emit(cg, "    movsxd rax, dword [rax]\n");
  } else {
    api->emit(cg, "    mov rax, [rax]\n");
  }
}

static int raw_pointee_size(const PieIrExpr *expr) {
  if (expr->raw_pointee_type == PIE_IR_TYPE_BYTE ||
      expr->raw_pointee_type == PIE_IR_TYPE_BOOL ||
      expr->raw_pointee_type == PIE_IR_TYPE_CHAR ||
      expr->raw_pointee_width == PIE_WIDTH_8) {
    return 1;
  }
  if (expr->raw_pointee_width == PIE_WIDTH_16) {
    return 2;
  }
  if (expr->raw_pointee_width == PIE_WIDTH_32 ||
      (expr->raw_pointee_type == PIE_IR_TYPE_FLOAT &&
       expr->raw_pointee_width == PIE_WIDTH_32)) {
    return 4;
  }
  if (expr->raw_pointee_type == PIE_IR_TYPE_STRING ||
      expr->raw_pointee_type == PIE_IR_TYPE_REF ||
      expr->raw_pointee_type == PIE_IR_TYPE_REF_MUT) {
    return 16;
  }
  return 8;
}

static void emit_scale_raw_offset(PieAsmCodegenContext *ctx, int scale) {
  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;

  if (scale == 2) {
    api->emit(cg, "    shl rax, 1\n");
  } else if (scale == 4) {
    api->emit(cg, "    shl rax, 2\n");
  } else if (scale == 8) {
    api->emit(cg, "    shl rax, 3\n");
  } else if (scale == 16) {
    api->emit(cg, "    shl rax, 4\n");
  }
}

static PieAsmGenResult codegen_raw_pointer_arithmetic(PieAsmCodegenContext *ctx,
                                                      const PieIrExpr *expr) {
  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;
  int left_raw = expr->left && expr->left->type == PIE_IR_TYPE_RAW_PTR;
  int right_raw = expr->right && expr->right->type == PIE_IR_TYPE_RAW_PTR;

  if (!left_raw && !right_raw) {
    return PIE_ASM_GEN_NO_MATCH;
  }
  if (right_raw && expr->op == '-') {
    api->error(cg, "raw pointer subtraction requires pointer - int");
    return PIE_ASM_GEN_ERROR;
  }

  const PieIrExpr *ptr_expr = left_raw ? expr->left : expr->right;
  const PieIrExpr *offset_expr = left_raw ? expr->right : expr->left;
  int scale = raw_pointee_size(expr);

  if (!api->emit_expr(cg, ptr_expr)) {
    return PIE_ASM_GEN_ERROR;
  }
  api->emit(cg, "    push rax\n");
  if (!api->emit_expr(cg, offset_expr)) {
    return PIE_ASM_GEN_ERROR;
  }
  emit_scale_raw_offset(ctx, scale);
  api->emit(cg, "    mov rbx, rax\n");
  api->emit(cg, "    pop rax\n");
  if (expr->op == '-') {
    api->emit(cg, "    sub rax, rbx\n");
  } else {
    api->emit(cg, "    add rax, rbx\n");
  }
  return PIE_ASM_GEN_OK;
}

PieAsmGenResult pie_feature_unsafe_codegen_expr(PieAsmCodegenContext *ctx,
                                                const PieIrExpr *expr) {
  if (expr->kind == PIE_IR_EXPR_BINARY &&
      (expr->op == '+' || expr->op == '-') &&
      expr->type == PIE_IR_TYPE_RAW_PTR) {
    return codegen_raw_pointer_arithmetic(ctx, expr);
  }

  if (expr->kind != PIE_IR_EXPR_UNARY) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  if (strcmp(expr->op_text, "*raw") == 0) {
    if (!expr->right) {
      ctx->api->error(ctx->cg, "raw pointer dereference is missing an operand");
      return PIE_ASM_GEN_ERROR;
    }
    if (!ctx->api->emit_expr(ctx->cg, expr->right)) {
      return PIE_ASM_GEN_ERROR;
    }
    emit_raw_load(ctx, expr);
    return PIE_ASM_GEN_OK;
  }

  if (strcmp(expr->op_text, "&raw") != 0) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  if (!expr->right || expr->right->kind != PIE_IR_EXPR_LOCAL) {
    ctx->api->error(ctx->cg, "raw address codegen currently requires a local");
    return PIE_ASM_GEN_ERROR;
  }

  PieAsmSymbol symbol;
  if (!ctx->api->find_local(ctx->cg, expr->right->local_id, &symbol)) {
    ctx->api->errorf(ctx->cg, "undefined local '%%%zu' for raw address",
                     expr->right->local_id);
    return PIE_ASM_GEN_ERROR;
  }

  ctx->api->emit(ctx->cg, "    lea rax, [rbp-%d]\n", symbol.offset);
  return PIE_ASM_GEN_OK;
}
