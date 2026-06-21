#include "pie/backend/asm/asm_codegen.h"

static const char *abi_arg_reg(size_t index) {
  static const char *regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
  return index < 6 ? regs[index] : NULL;
}

static const char *abi_xmm_reg(size_t index) {
  static const char *regs[] = {"xmm0", "xmm1", "xmm2", "xmm3",
                               "xmm4", "xmm5", "xmm6", "xmm7"};
  return index < 8 ? regs[index] : NULL;
}

static size_t count_int_args(const PieIrExpr *expr) {
  size_t count = 0;
  for (size_t i = 0; i < expr->call_arg_count; i++) {
    PieIrTypeKind t = expr->call_args[i].expr->type;
    if (t == PIE_IR_TYPE_STRING)
      count += 2;
    else if (t == PIE_IR_TYPE_FLOAT)
      count += 0;
    else
      count += 1;
  }
  return count;
}

static size_t count_float_args(const PieIrExpr *expr) {
  size_t count = 0;
  for (size_t i = 0; i < expr->call_arg_count; i++) {
    if (expr->call_args[i].expr->type == PIE_IR_TYPE_FLOAT)
      count++;
  }
  return count;
}

PieAsmGenResult pie_feature_functions_codegen_stmt(PieAsmCodegenContext *ctx,
                                                   const PieIrStmt *stmt) {
  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;

  if (stmt->kind == PIE_IR_STMT_EXPR) {
    if (!api->emit_expr(cg, stmt->expr)) {
      return PIE_ASM_GEN_ERROR;
    }
    return PIE_ASM_GEN_OK;
  }

  if (stmt->kind != PIE_IR_STMT_RETURN) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  if (stmt->expr) {
    if (!api->emit_expr(cg, stmt->expr)) {
      return PIE_ASM_GEN_ERROR;
    }
  } else {
    api->emit(cg, "    xor rax, rax\n");
  }

  api->emit_deferred(cg);

  api->emit(cg, "    lea rsp, [rbp-40]\n");
  api->emit(cg, "    pop r15\n");
  api->emit(cg, "    pop r14\n");
  api->emit(cg, "    pop r13\n");
  api->emit(cg, "    pop r12\n");
  api->emit(cg, "    pop rbx\n");
  api->emit(cg, "    pop rbp\n");
  api->emit(cg, "    ret\n");
  return PIE_ASM_GEN_OK;
}

static PieAsmGenResult emit_function_call(PieAsmCodegenContext *ctx,
                                          const PieIrExpr *expr,
                                          int is_closure) {
  size_t n = expr->call_arg_count;
  size_t num_float = count_float_args(expr);
  size_t num_int = count_int_args(expr);

  if (num_int > 6) {
    ctx->api->errorf(
        ctx->cg,
        "function call uses %zu integer argument register slot(s); limit 6",
        num_int);
    return PIE_ASM_GEN_ERROR;
  }
  if (num_float > 8) {
    ctx->api->errorf(
        ctx->cg, "function call uses %zu float argument register(s); limit 8",
        num_float);
    return PIE_ASM_GEN_ERROR;
  }

  for (size_t i = 0; i < n; i++) {
    const PieIrExpr *arg = expr->call_args[i].expr;
    if (!ctx->api->emit_expr(ctx->cg, arg)) {
      return PIE_ASM_GEN_ERROR;
    }
    ctx->api->emit(ctx->cg, "    sub rsp, 8\n");
    ctx->api->emit(ctx->cg, "    mov [rsp], rax\n");
    if (arg->type == PIE_IR_TYPE_STRING || arg->type == PIE_IR_TYPE_REF) {
      ctx->api->emit(ctx->cg, "    sub rsp, 8\n");
      ctx->api->emit(ctx->cg, "    mov [rsp], rdx\n");
    } else if (arg->type == PIE_IR_TYPE_FLOAT) {
      ctx->api->emit(ctx->cg, "    sub rsp, 8\n");
      ctx->api->emit(ctx->cg, "    mov [rsp], rdx\n");
    }
  }

  size_t total_stack = 0;
  for (size_t i = 0; i < n; i++) {
    PieIrTypeKind t = expr->call_args[i].expr->type;
    if (t == PIE_IR_TYPE_STRING || t == PIE_IR_TYPE_REF ||
        t == PIE_IR_TYPE_FLOAT) {
      total_stack += 16;
    } else {
      total_stack += 8;
    }
  }

  size_t int_reg_idx = 0;
  for (size_t i = 0; i < n; i++) {
    const PieIrExpr *arg = expr->call_args[i].expr;
    size_t offset = 0;
    for (size_t j = i + 1; j < n; j++) {
      PieIrTypeKind t = expr->call_args[j].expr->type;
      if (t == PIE_IR_TYPE_STRING || t == PIE_IR_TYPE_REF ||
          t == PIE_IR_TYPE_FLOAT) {
        offset += 16;
      } else {
        offset += 8;
      }
    }
    if (arg->type == PIE_IR_TYPE_FLOAT) {
      const char *xmm = abi_xmm_reg(i);
      if (!xmm) {
        ctx->api->errorf(ctx->cg,
                         "too many float args for register assignment");
        return PIE_ASM_GEN_ERROR;
      }
      if (arg->type_width == PIE_WIDTH_32) {
        ctx->api->emit(ctx->cg, "    movss %s, [rsp+%zu]\n", xmm, offset);
      } else {
        ctx->api->emit(ctx->cg, "    movsd %s, [rsp+%zu]\n", xmm, offset);
      }
    } else if (arg->type == PIE_IR_TYPE_STRING ||
               arg->type == PIE_IR_TYPE_REF) {
      const char *ptr_reg = abi_arg_reg(int_reg_idx);
      const char *len_reg = abi_arg_reg(int_reg_idx + 1);
      if (!ptr_reg || !len_reg) {
        ctx->api->errorf(ctx->cg, "too many int args for register assignment");
        return PIE_ASM_GEN_ERROR;
      }
      ctx->api->emit(ctx->cg, "    mov %s, [rsp+%zu]\n", ptr_reg, offset + 8);
      ctx->api->emit(ctx->cg, "    mov %s, [rsp+%zu]\n", len_reg, offset);
      int_reg_idx += 2;
    } else {
      const char *reg = abi_arg_reg(int_reg_idx);
      if (!reg) {
        ctx->api->errorf(ctx->cg, "too many int args for register assignment");
        return PIE_ASM_GEN_ERROR;
      }
      ctx->api->emit(ctx->cg, "    mov %s, [rsp+%zu]\n", reg, offset);
      int_reg_idx++;
    }
  }

  ctx->api->emit(ctx->cg, "    add rsp, %zu\n", total_stack);

  if (is_closure) {
    ctx->api->emit(ctx->cg, "    call r11\n");
  } else {
    ctx->api->emit(ctx->cg, "    call pie_fn_%s\n", expr->call_name);
  }

  return PIE_ASM_GEN_OK;
}

PieAsmGenResult pie_feature_functions_codegen_expr(PieAsmCodegenContext *ctx,
                                                   const PieIrExpr *expr) {
  if (expr->kind == PIE_IR_EXPR_CALL) {
    return emit_function_call(ctx, expr, 0);
  }

  if (expr->kind == PIE_IR_EXPR_CLOSURE_CALL) {
    if (!ctx->api->emit_expr(ctx->cg, expr->left)) {
      return PIE_ASM_GEN_ERROR;
    }
    ctx->api->emit(ctx->cg, "    mov r11, rax\n");
    ctx->api->emit(ctx->cg, "    mov r12, rdx\n");
    ctx->api->emit(ctx->cg, "    mov rdi, r12\n");

    size_t n = expr->call_arg_count;
    size_t num_float = count_float_args(expr);
    size_t num_int = count_int_args(expr);

    if (num_int > 4) {
      ctx->api->errorf(
          ctx->cg,
          "closure call uses %zu integer argument register slot(s); limit 4",
          num_int);
      return PIE_ASM_GEN_ERROR;
    }
    if (num_float > 7) {
      ctx->api->errorf(
          ctx->cg, "closure call uses %zu float argument register(s); limit 7",
          num_float);
      return PIE_ASM_GEN_ERROR;
    }

    for (size_t i = 0; i < n; i++) {
      const PieIrExpr *arg = expr->call_args[i].expr;
      if (!ctx->api->emit_expr(ctx->cg, arg)) {
        return PIE_ASM_GEN_ERROR;
      }
      ctx->api->emit(ctx->cg, "    sub rsp, 8\n");
      ctx->api->emit(ctx->cg, "    mov [rsp], rax\n");
      if (arg->type == PIE_IR_TYPE_STRING || arg->type == PIE_IR_TYPE_REF) {
        ctx->api->emit(ctx->cg, "    sub rsp, 8\n");
        ctx->api->emit(ctx->cg, "    mov [rsp], rdx\n");
      } else if (arg->type == PIE_IR_TYPE_FLOAT) {
        ctx->api->emit(ctx->cg, "    sub rsp, 8\n");
        ctx->api->emit(ctx->cg, "    mov [rsp], rdx\n");
      }
    }

    size_t int_reg_idx = 0;
    for (size_t i = 0; i < n; i++) {
      const PieIrExpr *arg = expr->call_args[i].expr;
      size_t offset = 0;
      for (size_t j = i + 1; j < n; j++) {
        PieIrTypeKind t = expr->call_args[j].expr->type;
        if (t == PIE_IR_TYPE_STRING || t == PIE_IR_TYPE_REF ||
            t == PIE_IR_TYPE_FLOAT) {
          offset += 16;
        } else {
          offset += 8;
        }
      }
      if (arg->type == PIE_IR_TYPE_FLOAT) {
        const char *xmm = abi_xmm_reg(i + 1);
        if (!xmm) {
          ctx->api->errorf(ctx->cg, "too many float args for closure");
          return PIE_ASM_GEN_ERROR;
        }
        if (arg->type_width == PIE_WIDTH_32) {
          ctx->api->emit(ctx->cg, "    movss %s, [rsp+%zu]\n", xmm, offset);
        } else {
          ctx->api->emit(ctx->cg, "    movsd %s, [rsp+%zu]\n", xmm, offset);
        }
      } else if (arg->type == PIE_IR_TYPE_STRING ||
                 arg->type == PIE_IR_TYPE_REF) {
        const char *ptr_reg = abi_arg_reg(int_reg_idx + 1);
        const char *len_reg = abi_arg_reg(int_reg_idx + 2);
        if (!ptr_reg || !len_reg) {
          ctx->api->errorf(ctx->cg, "too many int args for closure");
          return PIE_ASM_GEN_ERROR;
        }
        ctx->api->emit(ctx->cg, "    mov %s, [rsp+%zu]\n", ptr_reg, offset + 8);
        ctx->api->emit(ctx->cg, "    mov %s, [rsp+%zu]\n", len_reg, offset);
        int_reg_idx += 2;
      } else {
        const char *reg = abi_arg_reg(int_reg_idx + 1);
        if (!reg) {
          ctx->api->errorf(ctx->cg, "too many int args for closure");
          return PIE_ASM_GEN_ERROR;
        }
        ctx->api->emit(ctx->cg, "    mov %s, [rsp+%zu]\n", reg, offset);
        int_reg_idx++;
      }
    }

    size_t total_stack = 0;
    for (size_t i = 0; i < n; i++) {
      PieIrTypeKind t = expr->call_args[i].expr->type;
      if (t == PIE_IR_TYPE_STRING || t == PIE_IR_TYPE_REF ||
          t == PIE_IR_TYPE_FLOAT) {
        total_stack += 16;
      } else {
        total_stack += 8;
      }
    }
    ctx->api->emit(ctx->cg, "    add rsp, %zu\n", total_stack);

    ctx->api->emit(ctx->cg, "    call r11\n");

    return PIE_ASM_GEN_OK;
  }

  return PIE_ASM_GEN_NO_MATCH;
}
