#include "pie/backend/asm/asm_codegen.h"

static int is_string_like(PieIrTypeKind type, PieIrTypeKind ref_inner_type) {
  if (type == PIE_IR_TYPE_STRING)
    return 1;
  if (type == PIE_IR_TYPE_REF && ref_inner_type == PIE_IR_TYPE_STRING)
    return 1;
  return 0;
}

PieAsmGenResult pie_feature_print_codegen_stmt(PieAsmCodegenContext *ctx,
                                               const PieIrStmt *stmt) {
  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;

  if (stmt->kind != PIE_IR_STMT_PRINT) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  for (size_t i = 0; i < stmt->arg_count; i++) {
    const PieIrPrintArg *arg = &stmt->args[i];
    if (arg->is_string) {
      size_t id = 0;
      if (!api->add_string(cg, arg->text, arg->text_len, &id)) {
        return PIE_ASM_GEN_ERROR;
      }
      api->emit(cg, "    mov rdi, pie_str_%zu\n", id);
      api->emit(cg, "    mov rsi, %zu\n", arg->text_len);
      api->emit(cg, "    call pie_write\n");
    } else {
      if (!api->emit_expr(cg, arg->expr)) {
        return PIE_ASM_GEN_ERROR;
      }
      if (arg->expr->type == PIE_IR_TYPE_REF_MUT) {
        api->emit(cg, "    mov rbx, rax\n");
        api->emit(cg, "    mov rdi, [rbx]\n");
        api->emit(cg, "    mov rsi, [rbx+8]\n");
        api->emit(cg, "    call pie_write\n");
      } else if (is_string_like(arg->expr->type, arg->expr->ref_inner_type)) {
        api->emit(cg, "    mov rdi, rax\n");
        api->emit(cg, "    mov rsi, rdx\n");
        api->emit(cg, "    call pie_write\n");
      } else if (arg->expr->type == PIE_IR_TYPE_FLOAT) {
        if (arg->expr->type_width == PIE_WIDTH_WIDE) {
          api->emit(cg, "    mov rdi, rax\n");
          api->emit(cg, "    call pie_float_wide_print\n");
        } else if (arg->expr->type_width == PIE_WIDTH_32) {
          api->emit(cg, "    cvtss2sd xmm0, xmm0\n");
          api->emit(cg, "    call pie_print_float\n");
        } else {
          api->emit(cg, "    call pie_print_float\n");
        }
      } else if (arg->expr->type == PIE_IR_TYPE_CHAR) {
        api->emit(cg, "    sub rsp, 8\n");
        api->emit(cg, "    mov [rsp], al\n");
        api->emit(cg, "    mov rdi, rsp\n");
        api->emit(cg, "    mov rsi, 1\n");
        api->emit(cg, "    call pie_write\n");
        api->emit(cg, "    add rsp, 8\n");
      } else if (arg->expr->type == PIE_IR_TYPE_BOOL) {
        int false_label = api->next_label(cg);
        int end_label = api->next_label(cg);
        api->emit(cg, "    cmp rax, 0\n");
        api->emit(cg, "    je .Lbool_false_%d\n", false_label);
        api->emit(cg, "    mov rdi, pie_bool_true\n");
        api->emit(cg, "    mov rsi, 4\n");
        api->emit(cg, "    call pie_write\n");
        api->emit(cg, "    jmp .Lbool_end_%d\n", end_label);
        api->emit(cg, ".Lbool_false_%d:\n", false_label);
        api->emit(cg, "    mov rdi, pie_bool_false\n");
        api->emit(cg, "    mov rsi, 5\n");
        api->emit(cg, "    call pie_write\n");
        api->emit(cg, ".Lbool_end_%d:\n", end_label);
      } else {
        if (arg->expr->type_width == PIE_WIDTH_WIDE) {
          api->emit(cg, "    mov rdi, rax\n");
          api->emit(cg, "    call pie_int_wide_print\n");
        } else {
          api->emit(cg, "    mov rdi, rax\n");
          api->emit(cg, "    call pie_print_int\n");
        }
      }
    }
  }

  if (stmt->println) {
    api->emit(cg, "    call pie_print_newline\n");
  }
  return PIE_ASM_GEN_OK;
}
