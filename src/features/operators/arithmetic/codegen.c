#include "pie/backend/asm/asm_codegen.h"

static int is_arithmetic_binary(char op) {
  return op == '+' || op == '-' || op == '*' || op == '/' || op == '%';
}

static const char *int_reg(PieIrTypeKind type, int type_width) {
  (void)type;
  if (type_width == PIE_WIDTH_8)
    return "al";
  if (type_width == PIE_WIDTH_16)
    return "ax";
  if (type_width == PIE_WIDTH_32)
    return "eax";
  return "rax";
}

static const char *int_reg_b(PieIrTypeKind type, int type_width) {
  (void)type;
  if (type_width == PIE_WIDTH_8)
    return "bl";
  if (type_width == PIE_WIDTH_16)
    return "bx";
  if (type_width == PIE_WIDTH_32)
    return "ebx";
  return "rbx";
}

static PieAsmGenResult codegen_binary(PieAsmCodegenContext *ctx,
                                      const PieIrExpr *expr) {
  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;
  int w = expr->type_width;

  if (w == PIE_WIDTH_WIDE) {
    if (!api->emit_expr(cg, expr->left)) {
      return PIE_ASM_GEN_ERROR;
    }
    if (expr->left->type_width != PIE_WIDTH_WIDE) {
      api->emit(cg, "    mov rdi, rax\n");
      api->emit(cg, "    call pie_int_wide_new\n");
    }
    api->emit(cg, "    push rax\n");
    if (!api->emit_expr(cg, expr->right)) {
      return PIE_ASM_GEN_ERROR;
    }
    if (expr->right->type_width != PIE_WIDTH_WIDE) {
      api->emit(cg, "    mov rdi, rax\n");
      api->emit(cg, "    call pie_int_wide_new\n");
    }
    api->emit(cg, "    mov rbx, rax\n");
    api->emit(cg, "    pop rax\n");
    const char *fn = NULL;
    switch (expr->op) {
    case '+':
      fn = "pie_int_wide_add";
      break;
    case '-':
      fn = "pie_int_wide_sub";
      break;
    case '*':
      fn = "pie_int_wide_mul";
      break;
    case '/':
      fn = "pie_int_wide_div";
      break;
    case '%':
      fn = "pie_int_wide_mod";
      break;
    default:
      if (expr->op == '*' && expr->op_text[1] == '*') {
        api->emit(cg, "    mov rdi, rax\n");
        api->emit(cg, "    mov rsi, rbx\n");
        api->emit(cg, "    call pie_int_power\n");
        goto wide_done;
      }
      api->errorf(cg, "unsupported binary operator '%c' for int<wide>",
                  expr->op);
      return PIE_ASM_GEN_ERROR;
    }
    api->emit(cg, "    mov rdi, rax\n");
    api->emit(cg, "    mov rsi, rbx\n");
    api->emit(cg, "    call %s\n", fn);
    return PIE_ASM_GEN_OK;
  wide_done:
    return PIE_ASM_GEN_OK;
  }

  if (!api->emit_expr(cg, expr->left)) {
    return PIE_ASM_GEN_ERROR;
  }
  api->emit(cg, "    push rax\n");
  if (!api->emit_expr(cg, expr->right)) {
    return PIE_ASM_GEN_ERROR;
  }
  api->emit(cg, "    mov rbx, rax\n");
  api->emit(cg, "    pop rax\n");

  if (w == PIE_WIDTH_8 || w == PIE_WIDTH_16 || w == PIE_WIDTH_32) {
    switch (expr->op) {
    case '+':
      api->emit(cg, "    add %s, %s\n", int_reg(expr->type, w),
                int_reg_b(expr->type, w));
      break;
    case '-':
      api->emit(cg, "    sub %s, %s\n", int_reg(expr->type, w),
                int_reg_b(expr->type, w));
      break;
    case '*':
      if (expr->op_text[1] == '*') {
        api->emit(cg, "    mov rdi, rax\n");
        api->emit(cg, "    mov rsi, rbx\n");
        api->emit(cg, "    call pie_int_power\n");
      } else {
        api->emit(cg, "    imul %s, %s\n", int_reg(expr->type, w),
                  int_reg_b(expr->type, w));
      }
      break;
    default:
      api->errorf(cg, "unsupported binary operator '%c' for narrow int",
                  expr->op);
      return PIE_ASM_GEN_ERROR;
    }
    return PIE_ASM_GEN_OK;
  }

  switch (expr->op) {
  case '+':
    api->emit(cg, "    add rax, rbx\n");
    break;
  case '-':
    api->emit(cg, "    sub rax, rbx\n");
    break;
  case '*':
    if (expr->op_text[1] == '*') {
      api->emit(cg, "    mov rdi, rax\n");
      api->emit(cg, "    mov rsi, rbx\n");
      api->emit(cg, "    call pie_int_power\n");
    } else {
      api->emit(cg, "    imul rax, rbx\n");
    }
    break;
  case '/': {
    int ok_label = api->next_label(cg);
    api->emit(cg, "    cmp rbx, 0\n");
    api->emit(cg, "    jne .Ldiv_ok_%d\n", ok_label);
    api->emit(cg, "    mov rdi, pie_runtime_div_zero\n");
    api->emit(cg, "    mov rsi, 15\n");
    api->emit(cg, "    call pie_write\n");
    api->emit(cg, "    mov rax, 1\n");
    api->emit(cg, "    leave\n");
    api->emit(cg, "    ret\n");
    api->emit(cg, ".Ldiv_ok_%d:\n", ok_label);
    api->emit(cg, "    cqo\n");
    api->emit(cg, "    idiv rbx\n");
    break;
  }
  case '%': {
    int ok_label = api->next_label(cg);
    api->emit(cg, "    cmp rbx, 0\n");
    api->emit(cg, "    jne .Lmod_ok_%d\n", ok_label);
    api->emit(cg, "    mov rdi, pie_runtime_mod_zero\n");
    api->emit(cg, "    mov rsi, 17\n");
    api->emit(cg, "    call pie_write\n");
    api->emit(cg, "    mov rax, 1\n");
    api->emit(cg, "    leave\n");
    api->emit(cg, "    ret\n");
    api->emit(cg, ".Lmod_ok_%d:\n", ok_label);
    api->emit(cg, "    cqo\n");
    api->emit(cg, "    idiv rbx\n");
    api->emit(cg, "    mov rax, rdx\n");
    break;
  }
  default:
    if (expr->op == '*' && expr->op_text[1] == '*') {
      api->emit(cg, "    mov rdi, rax\n");
      api->emit(cg, "    mov rsi, rbx\n");
      api->emit(cg, "    call pie_int_power\n");
      break;
    }
    api->errorf(cg, "unsupported binary operator '%c'", expr->op);
    return PIE_ASM_GEN_ERROR;
  }
  return PIE_ASM_GEN_OK;
}

static PieAsmGenResult codegen_float_binary(PieAsmCodegenContext *ctx,
                                            const PieIrExpr *expr) {
  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;
  int w = expr->type_width;

  if (w == PIE_WIDTH_WIDE) {
    if (!api->emit_expr(cg, expr->left)) {
      return PIE_ASM_GEN_ERROR;
    }
    if (expr->left->type_width != PIE_WIDTH_WIDE) {
      if (expr->left->type_width == PIE_WIDTH_32) {
        api->emit(cg, "    cvtss2sd xmm0, xmm0\n");
      }
      api->emit(cg, "    call pie_float_wide_new\n");
    }
    api->emit(cg, "    push rax\n");
    if (!api->emit_expr(cg, expr->right)) {
      return PIE_ASM_GEN_ERROR;
    }
    if (expr->right->type_width != PIE_WIDTH_WIDE) {
      if (expr->right->type_width == PIE_WIDTH_32) {
        api->emit(cg, "    cvtss2sd xmm0, xmm0\n");
      }
      api->emit(cg, "    call pie_float_wide_new\n");
    }
    api->emit(cg, "    mov rsi, rax\n");
    api->emit(cg, "    pop rdi\n");
    const char *fn = NULL;
    switch (expr->op) {
    case '+':
      fn = "pie_float_wide_add";
      break;
    case '-':
      fn = "pie_float_wide_sub";
      break;
    case '*':
      fn = "pie_float_wide_mul";
      break;
    case '/':
      fn = "pie_float_wide_div";
      break;
    default:
      api->errorf(cg, "unsupported binary operator '%c' for float<wide>",
                  expr->op);
      return PIE_ASM_GEN_ERROR;
    }
    api->emit(cg, "    call %s\n", fn);
    return PIE_ASM_GEN_OK;
  }
  if (!api->emit_expr(cg, expr->left)) {
    return PIE_ASM_GEN_ERROR;
  }
  if (w == PIE_WIDTH_32) {
    api->emit(cg, "    sub rsp, 4\n");
    api->emit(cg, "    movss [rsp], xmm0\n");
  } else {
    api->emit(cg, "    sub rsp, 8\n");
    api->emit(cg, "    movsd [rsp], xmm0\n");
  }
  if (!api->emit_expr(cg, expr->right)) {
    return PIE_ASM_GEN_ERROR;
  }
  api->emit(cg, "    movapd xmm1, xmm0\n");
  if (w == PIE_WIDTH_32) {
    api->emit(cg, "    movss xmm0, [rsp]\n");
    api->emit(cg, "    add rsp, 4\n");
  } else {
    api->emit(cg, "    movsd xmm0, [rsp]\n");
    api->emit(cg, "    add rsp, 8\n");
  }

  if (w == PIE_WIDTH_32) {
    switch (expr->op) {
    case '+':
      api->emit(cg, "    addss xmm0, xmm1\n");
      break;
    case '-':
      api->emit(cg, "    subss xmm0, xmm1\n");
      break;
    case '*':
      if (expr->op_text[1] == '*') {
        api->emit(cg, "    call pie_float_power\n");
      } else {
        api->emit(cg, "    mulss xmm0, xmm1\n");
      }
      break;
    case '/':
      api->emit(cg, "    divss xmm0, xmm1\n");
      break;
    default:
      api->errorf(cg, "unsupported float binary operator '%c'", expr->op);
      return PIE_ASM_GEN_ERROR;
    }
  } else {
    switch (expr->op) {
    case '+':
      api->emit(cg, "    addsd xmm0, xmm1\n");
      break;
    case '-':
      api->emit(cg, "    subsd xmm0, xmm1\n");
      break;
    case '*':
      api->emit(cg, "    mulsd xmm0, xmm1\n");
      break;
    case '/':
      api->emit(cg, "    divsd xmm0, xmm1\n");
      break;
    default:
      if (expr->op == '*' && expr->op_text[1] == '*') {
        api->emit(cg, "    call pie_float_power\n");
        break;
      }
      api->errorf(cg, "unsupported float binary operator '%c'", expr->op);
      return PIE_ASM_GEN_ERROR;
    }
  }
  return PIE_ASM_GEN_OK;
}

PieAsmGenResult pie_feature_arithmetic_codegen_expr(PieAsmCodegenContext *ctx,
                                                    const PieIrExpr *expr) {
  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;

  if (expr->kind == PIE_IR_EXPR_UNARY && expr->op == '-') {
    if (!api->emit_expr(cg, expr->right)) {
      return PIE_ASM_GEN_ERROR;
    }
    if (expr->type == PIE_IR_TYPE_FLOAT) {
      if (expr->type_width == PIE_WIDTH_WIDE) {
        api->emit(cg, "    mov rdi, rax\n");
        api->emit(cg, "    call pie_float_wide_neg\n");
      } else if (expr->type_width == PIE_WIDTH_32) {
        api->emit(cg, "    movss xmm1, xmm0\n");
        api->emit(cg, "    pxor xmm0, xmm0\n");
        api->emit(cg, "    subss xmm0, xmm1\n");
      } else {
        api->emit(cg, "    xorpd xmm1, xmm1\n");
        api->emit(cg, "    subsd xmm1, xmm0\n");
        api->emit(cg, "    movapd xmm0, xmm1\n");
      }
      return PIE_ASM_GEN_OK;
    } else if (expr->op == '-') {
      if (expr->type_width == PIE_WIDTH_WIDE) {
        api->emit(cg, "    mov rdi, rax\n");
        api->emit(cg, "    call pie_int_wide_neg\n");
      } else {
        api->emit(cg, "    neg rax\n");
      }
      return PIE_ASM_GEN_OK;
    }
  }

  if (expr->kind == PIE_IR_EXPR_BINARY && is_arithmetic_binary(expr->op)) {
    if (expr->op == '+' && expr->op_text[1] == '+') {
      return PIE_ASM_GEN_NO_MATCH;
    }
    if (expr->type == PIE_IR_TYPE_FLOAT) {
      return codegen_float_binary(ctx, expr);
    }
    return codegen_binary(ctx, expr);
  }

  return PIE_ASM_GEN_NO_MATCH;
}
