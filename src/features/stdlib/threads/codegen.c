#include "pie/backend/asm/asm_codegen.h"

#include <string.h>

static const char *thread_runtime_name(PieIrExprKind kind) {
  switch (kind) {
  case PIE_IR_EXPR_THREAD_SPAWN:
    return "pie_thread_spawn";
  case PIE_IR_EXPR_THREAD_JOIN:
    return "pie_thread_join";
  case PIE_IR_EXPR_MUTEX_CREATE:
    return "pie_thread_mutex_create";
  case PIE_IR_EXPR_MUTEX_LOCK:
    return "pie_thread_mutex_lock";
  case PIE_IR_EXPR_MUTEX_UNLOCK:
    return "pie_thread_mutex_unlock";
  case PIE_IR_EXPR_THREAD_SLEEP:
    return "pie_thread_sleep_ms";
  case PIE_IR_EXPR_CHANNEL_CREATE:
    return "pie_channel_create";
  case PIE_IR_EXPR_CHANNEL_SEND:
    return "pie_channel_send";
  case PIE_IR_EXPR_CHANNEL_RECV:
    return "pie_channel_recv";
  case PIE_IR_EXPR_CHANNEL_CLOSE:
    return "pie_channel_close";
  default:
    return NULL;
  }
}

static const char *abi_arg_reg(size_t index) {
  static const char *regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
  return index < 6 ? regs[index] : NULL;
}

static size_t count_arg_slots(const PieIrExpr *expr) {
  size_t total = 0;
  for (size_t i = 0; i < expr->call_arg_count; i++) {
    const PieIrExpr *arg = expr->call_args[i].expr;
    if (arg->type == PIE_IR_TYPE_STRING || arg->type == PIE_IR_TYPE_REF) {
      total += 2;
    } else {
      total += 1;
    }
  }
  return total;
}

static PieAsmGenResult emit_channel_create(PieAsmCodegenContext *ctx,
                                           const PieIrExpr *expr) {
  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;

  if (!api->emit_expr(cg, expr->call_args[0].expr)) {
    return PIE_ASM_GEN_ERROR;
  }
  api->emit(cg, "    push rax\n");
  api->emit(cg, "    mov rdi, 8\n");
  api->emit(cg, "    pop rsi\n");
  api->emit(cg, "    call pie_channel_create\n");
  return PIE_ASM_GEN_OK;
}

static PieAsmGenResult emit_channel_send(PieAsmCodegenContext *ctx,
                                         const PieIrExpr *expr) {
  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;

  if (!api->emit_expr(cg, expr->call_args[0].expr)) {
    return PIE_ASM_GEN_ERROR;
  }
  api->emit(cg, "    push rax\n");

  if (!api->emit_expr(cg, expr->call_args[1].expr)) {
    return PIE_ASM_GEN_ERROR;
  }
  api->emit(cg, "    push rax\n");

  api->emit(cg, "    mov rdi, [rsp + 8]\n");
  api->emit(cg, "    mov rsi, rsp\n");
  api->emit(cg, "    call pie_channel_send\n");
  api->emit(cg, "    add rsp, 16\n");
  return PIE_ASM_GEN_OK;
}

static PieAsmGenResult emit_channel_recv(PieAsmCodegenContext *ctx,
                                         const PieIrExpr *expr) {
  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;

  if (!api->emit_expr(cg, expr->call_args[0].expr)) {
    return PIE_ASM_GEN_ERROR;
  }
  api->emit(cg, "    push rax\n");
  api->emit(cg, "    sub rsp, 8\n");
  api->emit(cg, "    mov rdi, [rsp + 8]\n");
  api->emit(cg, "    mov rsi, rsp\n");
  api->emit(cg, "    call pie_channel_recv\n");
  api->emit(cg, "    pop rax\n");
  api->emit(cg, "    add rsp, 8\n");
  return PIE_ASM_GEN_OK;
}

PieAsmGenResult pie_feature_threads_codegen_expr(PieAsmCodegenContext *ctx,
                                                 const PieIrExpr *expr) {
  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;

  if (expr->kind != PIE_IR_EXPR_THREAD_SPAWN &&
      expr->kind != PIE_IR_EXPR_THREAD_JOIN &&
      expr->kind != PIE_IR_EXPR_MUTEX_CREATE &&
      expr->kind != PIE_IR_EXPR_MUTEX_LOCK &&
      expr->kind != PIE_IR_EXPR_MUTEX_UNLOCK &&
      expr->kind != PIE_IR_EXPR_THREAD_SLEEP &&
      expr->kind != PIE_IR_EXPR_CHANNEL_CREATE &&
      expr->kind != PIE_IR_EXPR_CHANNEL_SEND &&
      expr->kind != PIE_IR_EXPR_CHANNEL_RECV &&
      expr->kind != PIE_IR_EXPR_CHANNEL_CLOSE) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  if (expr->kind == PIE_IR_EXPR_CHANNEL_CREATE) {
    return emit_channel_create(ctx, expr);
  }
  if (expr->kind == PIE_IR_EXPR_CHANNEL_SEND) {
    return emit_channel_send(ctx, expr);
  }
  if (expr->kind == PIE_IR_EXPR_CHANNEL_RECV) {
    return emit_channel_recv(ctx, expr);
  }

  const char *runtime_name = thread_runtime_name(expr->kind);
  if (!runtime_name) {
    api->error(cg, "unknown thread operation");
    return PIE_ASM_GEN_ERROR;
  }

  size_t total_slots = count_arg_slots(expr);

  if (total_slots > 6) {
    api->errorf(cg, "thread call uses %zu register slots; limit is 6",
                total_slots);
    return PIE_ASM_GEN_ERROR;
  }

  if (total_slots % 2 != 0) {
    api->emit(cg, "    sub rsp, 8\n");
  }

  for (size_t i = 0; i < expr->call_arg_count; i++) {
    const PieIrExpr *arg = expr->call_args[i].expr;
    if (!api->emit_expr(cg, arg)) {
      return PIE_ASM_GEN_ERROR;
    }
    if (arg->type == PIE_IR_TYPE_FLOAT) {
      if (arg->type_width == PIE_WIDTH_32) {
        api->emit(cg, "    movd rax, xmm0\n");
      } else {
        api->emit(cg, "    movq rax, xmm0\n");
      }
    }
    api->emit(cg, "    push rax\n");
    if (arg->type == PIE_IR_TYPE_STRING || arg->type == PIE_IR_TYPE_REF) {
      api->emit(cg, "    push rdx\n");
    }
  }

  size_t reg_idx = total_slots;
  for (size_t i = expr->call_arg_count; i > 0; i--) {
    const PieIrExpr *arg = expr->call_args[i - 1].expr;
    if (arg->type == PIE_IR_TYPE_STRING || arg->type == PIE_IR_TYPE_REF) {
      reg_idx--;
      api->emit(cg, "    pop %s\n", abi_arg_reg(reg_idx));
      reg_idx--;
      api->emit(cg, "    pop %s\n", abi_arg_reg(reg_idx));
    } else {
      reg_idx--;
      api->emit(cg, "    pop %s\n", abi_arg_reg(reg_idx));
    }
  }

  api->emit(cg, "    call %s\n", runtime_name);

  if (total_slots % 2 != 0) {
    api->emit(cg, "    add rsp, 8\n");
  }

  return PIE_ASM_GEN_OK;
}
