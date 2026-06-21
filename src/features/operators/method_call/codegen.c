#include "pie/backend/asm/asm_codegen.h"

static const char *abi_arg_reg(size_t index) {
  static const char *regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
  return index < 6 ? regs[index] : NULL;
}

static size_t abi_slots_for_type(PieIrTypeKind type,
                                 PieIrTypeKind ref_inner_type) {
  if (type == PIE_IR_TYPE_STRING)
    return 2;
  if (type == PIE_IR_TYPE_REF && ref_inner_type == PIE_IR_TYPE_STRING)
    return 2;
  return 1;
}

static int emit_method_call(PieAsmCodegenContext *ctx, const PieIrExpr *expr) {
  size_t total_slots = 0;
  for (size_t i = 0; i < expr->call_arg_count; i++) {
    total_slots += abi_slots_for_type(expr->call_args[i].expr->type,
                                      expr->call_args[i].expr->ref_inner_type);
  }

  if (total_slots > 6) {
    ctx->api->errorf(ctx->cg, "method call uses %zu slots; limit is 6",
                     total_slots);
    return 0;
  }

  if (total_slots % 2 != 0) {
    ctx->api->emit(ctx->cg, "    sub rsp, 8\n");
  }

  for (size_t i = 0; i < expr->call_arg_count; i++) {
    const PieIrExpr *arg = expr->call_args[i].expr;
    if (!ctx->api->emit_expr(ctx->cg, arg)) {
      return 0;
    }
    if (arg->type == PIE_IR_TYPE_FLOAT) {
      if (arg->type_width == PIE_WIDTH_32) {
        ctx->api->emit(ctx->cg, "    movd rax, xmm0\n");
      } else if (arg->type_width == PIE_WIDTH_WIDE) {
      } else {
        ctx->api->emit(ctx->cg, "    movq rax, xmm0\n");
      }
    }
    ctx->api->emit(ctx->cg, "    push rax\n");
    if (abi_slots_for_type(arg->type, arg->ref_inner_type) == 2) {
      ctx->api->emit(ctx->cg, "    push rdx\n");
    }
  }

  for (size_t i = total_slots; i > 0; i--) {
    ctx->api->emit(ctx->cg, "    pop %s\n", abi_arg_reg(i - 1));
  }

  ctx->api->emit(ctx->cg, "    call %s\n", expr->method_name);

  if (total_slots % 2 != 0) {
    ctx->api->emit(ctx->cg, "    add rsp, 8\n");
  }
  return 1;
}

PieAsmGenResult pie_feature_method_call_codegen_expr(PieAsmCodegenContext *ctx,
                                                     const PieIrExpr *expr) {
  if (expr->kind != PIE_IR_EXPR_METHOD_CALL) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  if (!expr->method_name) {
    ctx->api->error(ctx->cg, "method call missing runtime function name");
    return PIE_ASM_GEN_ERROR;
  }

  if (!emit_method_call(ctx, expr)) {
    return PIE_ASM_GEN_ERROR;
  }
  return PIE_ASM_GEN_OK;
}
