#include "pie/backend/asm/asm_codegen.h"

#include <stdlib.h>
#include <string.h>

static int ir_type_size(PieIrTypeKind type) {
  if (type == PIE_IR_TYPE_STRING || type == PIE_IR_TYPE_CLOSURE) {
    return 16;
  }
  return 8;
}

PieAsmGenResult pie_feature_closures_codegen_expr(PieAsmCodegenContext *ctx,
                                                  const PieIrExpr *expr) {
  if (expr->kind != PIE_IR_EXPR_CLOSURE) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  int label = ctx->api->next_label(ctx->cg);

  ctx->api->emit(ctx->cg, "    jmp .Lclosure_end_%d\n", label);
  ctx->api->emit(ctx->cg, ".Lclosure_fn_%d:\n", label);
  ctx->api->emit(ctx->cg, "    push rbp\n");
  ctx->api->emit(ctx->cg, "    mov rbp, rsp\n");
  ctx->api->emit(ctx->cg, "    push rbx\n");
  ctx->api->emit(ctx->cg, "    push r12\n");
  ctx->api->emit(ctx->cg, "    push r13\n");
  ctx->api->emit(ctx->cg, "    push r14\n");
  ctx->api->emit(ctx->cg, "    push r15\n");

  ctx->api->push_frame(ctx->cg);
  ctx->api->set_closure_context(ctx->cg, 1, expr->closure_captured_names,
                                expr->closure_captured_count);

  size_t param_slots = 0;
  for (size_t i = 0; i < expr->closure_param_count; i++) {
    param_slots += (expr->closure_param_types[i] == PIE_IR_TYPE_STRING ||
                    expr->closure_param_types[i] == PIE_IR_TYPE_REF)
                       ? 2
                       : 1;
  }

  size_t capture_total_size = 0;
  for (size_t i = 0; i < expr->closure_captured_count; i++) {
    capture_total_size += ir_type_size(expr->closure_capture_types[i]);
  }

  ctx->api->emit(ctx->cg, "    sub rsp, 4096\n");

  size_t current_param_slot = 0;
  for (size_t i = 0; i < expr->closure_param_count; i++) {
    int offset = (int)(48 + current_param_slot * 8);
    if (expr->closure_param_types[i] == PIE_IR_TYPE_STRING ||
        expr->closure_param_types[i] == PIE_IR_TYPE_REF) {
      const char *ptr_reg = (current_param_slot == 0)   ? "rsi"
                            : (current_param_slot == 1) ? "rdx"
                            : (current_param_slot == 2) ? "rcx"
                            : (current_param_slot == 3) ? "r8"
                            : (current_param_slot == 4) ? "r9"
                                                        : "stack";
      const char *len_reg = (current_param_slot + 1 == 1)   ? "rdx"
                            : (current_param_slot + 1 == 2) ? "rcx"
                            : (current_param_slot + 1 == 3) ? "r8"
                            : (current_param_slot + 1 == 4) ? "r9"
                                                            : "stack";

      ctx->api->emit(ctx->cg, "    mov [rbp - %d], %s\n", offset + 8, ptr_reg);
      ctx->api->emit(ctx->cg, "    mov [rbp - %d], %s\n", offset, len_reg);

      PieAsmSymbol symbol;
      ctx->api->add_local_at(ctx->cg, i, offset + 8, 0,
                             expr->closure_param_types[i], PIE_WIDTH_64,
                             &symbol);
      current_param_slot += 2;
    } else {
      const char *reg = (current_param_slot == 0)   ? "rsi"
                        : (current_param_slot == 1) ? "rdx"
                        : (current_param_slot == 2) ? "rcx"
                        : (current_param_slot == 3) ? "r8"
                        : (current_param_slot == 4) ? "r9"
                                                    : "r9";
      ctx->api->emit(ctx->cg, "    mov [rbp - %d], %s\n", offset, reg);

      PieAsmSymbol symbol;
      ctx->api->add_local_at(ctx->cg, i, offset, 0,
                             expr->closure_param_types[i], PIE_WIDTH_64,
                             &symbol);
      current_param_slot += 1;
    }
  }

  size_t env_offset = 0;
  size_t local_capture_base = 40 + 8 + param_slots * 8;
  for (size_t i = 0; i < expr->closure_captured_count; i++) {
    int size = ir_type_size(expr->closure_capture_types[i]);
    if (size == 16) {
      int local_offset = (int)(local_capture_base + env_offset + 8);
      ctx->api->emit(ctx->cg, "    mov rax, [rdi + %zu]\n", env_offset);
      ctx->api->emit(ctx->cg, "    mov rdx, [rdi + %zu]\n", env_offset + 8);
      ctx->api->emit(ctx->cg, "    mov [rbp - %d], rax\n", local_offset);
      ctx->api->emit(ctx->cg, "    mov [rbp - %d], rdx\n", local_offset - 8);

      PieAsmSymbol symbol;
      ctx->api->add_local_at(ctx->cg, expr->closure_param_count + i,
                             local_offset, 1, expr->closure_capture_types[i],
                             PIE_WIDTH_64, &symbol);
    } else {
      int local_offset = (int)(local_capture_base + env_offset);
      ctx->api->emit(ctx->cg, "    mov rax, [rdi + %zu]\n", env_offset);
      ctx->api->emit(ctx->cg, "    mov [rbp - %d], rax\n", local_offset);

      PieAsmSymbol symbol;
      ctx->api->add_local_at(ctx->cg, expr->closure_param_count + i,
                             local_offset, 1, expr->closure_capture_types[i],
                             PIE_WIDTH_64, &symbol);
    }
    env_offset += size;
  }

  ctx->api->set_stack_offset(ctx->cg, (int)(local_capture_base + env_offset));

  if (expr->closure_body) {
    for (size_t i = 0; i < expr->closure_body->stmt_count; i++) {
      if (!ctx->api->emit_stmt(ctx->cg, &expr->closure_body->stmts[i])) {
        ctx->api->set_closure_context(ctx->cg, 0, NULL, 0);
        ctx->api->pop_frame(ctx->cg);
        return PIE_ASM_GEN_ERROR;
      }
    }
  }

  ctx->api->set_closure_context(ctx->cg, 0, NULL, 0);
  ctx->api->pop_frame(ctx->cg);

  ctx->api->emit(ctx->cg, "    lea rsp, [rbp-40]\n");
  ctx->api->emit(ctx->cg, "    pop r15\n");
  ctx->api->emit(ctx->cg, "    pop r14\n");
  ctx->api->emit(ctx->cg, "    pop r13\n");
  ctx->api->emit(ctx->cg, "    pop r12\n");
  ctx->api->emit(ctx->cg, "    pop rbx\n");
  ctx->api->emit(ctx->cg, "    pop rbp\n");
  ctx->api->emit(ctx->cg, "    ret\n");
  ctx->api->emit(ctx->cg, ".Lclosure_end_%d:\n", label);

  ctx->api->emit(ctx->cg, "    lea rax, [rel .Lclosure_fn_%d]\n", label);

  if (expr->closure_captured_count > 0) {
    ctx->api->emit(ctx->cg, "    push rax\n");
    ctx->api->emit(ctx->cg, "    push r12\n");
    ctx->api->emit(ctx->cg, "    mov rdi, %zu\n", capture_total_size);
    ctx->api->emit(ctx->cg, "    call pie_malloc\n");
    ctx->api->emit(ctx->cg, "    mov r12, rax\n");

    size_t current_env_offset = 0;
    for (size_t i = 0; i < expr->closure_captured_count; i++) {
      size_t outer_id = expr->closure_capture_outer_ids
                            ? expr->closure_capture_outer_ids[i]
                            : 0;
      PieAsmSymbol outer_sym;
      if (!ctx->api->find_local(ctx->cg, outer_id, &outer_sym)) {
        ctx->api->errorf(ctx->cg, "failed to find outer local for capture '%s'",
                         expr->closure_captured_names
                             ? expr->closure_captured_names[i]
                             : "?");
        return PIE_ASM_GEN_ERROR;
      }

      int size = ir_type_size(expr->closure_capture_types[i]);
      if (size == 16) {
        ctx->api->emit(ctx->cg, "    mov rax, [rbp - %d]\n", outer_sym.offset);
        ctx->api->emit(ctx->cg, "    mov rdx, [rbp - %d]\n",
                       outer_sym.offset - 8);
        ctx->api->emit(ctx->cg, "    mov [r12 + %zu], rax\n",
                       current_env_offset);
        ctx->api->emit(ctx->cg, "    mov [r12 + %zu], rdx\n",
                       current_env_offset + 8);
      } else {
        ctx->api->emit(ctx->cg, "    mov rax, [rbp - %d]\n", outer_sym.offset);
        ctx->api->emit(ctx->cg, "    mov [r12 + %zu], rax\n",
                       current_env_offset);
      }
      current_env_offset += size;
    }

    ctx->api->emit(ctx->cg, "    mov rdx, r12\n");
    ctx->api->emit(ctx->cg, "    pop r12\n");
    ctx->api->emit(ctx->cg, "    pop rax\n");
  } else {
    ctx->api->emit(ctx->cg, "    xor rdx, rdx\n");
  }

  return PIE_ASM_GEN_OK;
}

static const char *abi_arg_reg(size_t index) {
  static const char *regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
  return index < 6 ? regs[index] : NULL;
}

PieAsmGenResult
pie_feature_closures_codegen_closure_call(PieAsmCodegenContext *ctx,
                                          const PieIrExpr *expr) {
  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;

  if (expr->kind != PIE_IR_EXPR_CLOSURE_CALL) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  if (!api->emit_expr(cg, expr->left)) {
    return PIE_ASM_GEN_ERROR;
  }

  api->emit(cg, "    push rbx\n");
  api->emit(cg, "    mov rbx, rax\n");
  api->emit(cg, "    push r12\n");
  api->emit(cg, "    mov r12, rdx\n");

  api->emit(cg, "    mov rdi, rdx\n");

  for (size_t i = 0; i < expr->call_arg_count; i++) {
    const PieIrExpr *arg = expr->call_args[i].expr;
    if (!api->emit_expr(cg, arg)) {
      return PIE_ASM_GEN_ERROR;
    }
    api->emit(cg, "    push rax\n");
    if (arg->type == PIE_IR_TYPE_STRING) {
      api->emit(cg, "    push rdx\n");
    }
  }

  for (size_t i = expr->call_arg_count; i > 0; i--) {
    const PieIrExpr *arg = expr->call_args[i - 1].expr;
    if (arg->type == PIE_IR_TYPE_STRING) {
      api->emit(cg, "    pop %s\n", abi_arg_reg(i));
      api->emit(cg, "    pop %s\n", abi_arg_reg(i - 1));
    } else {
      api->emit(cg, "    pop %s\n", abi_arg_reg(i));
    }
  }

  api->emit(cg, "    call rbx\n");

  api->emit(cg, "    pop r12\n");
  api->emit(cg, "    pop rbx\n");

  return PIE_ASM_GEN_OK;
}
