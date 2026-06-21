#include "pie/backend/asm/asm_codegen.h"

#include <string.h>

PieAsmGenResult pie_feature_assert_codegen_stmt(PieAsmCodegenContext *ctx,
                                                const PieIrStmt *stmt) {
  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;

  if (stmt->kind == PIE_IR_STMT_ASSERT) {
    if (!api->emit_expr(cg, stmt->assert_cond)) {
      return PIE_ASM_GEN_ERROR;
    }

    int fail_label = api->next_label(cg);
    int end_label = api->next_label(cg);
    api->emit(cg, "    cmp rax, 0\n");
    api->emit(cg, "    je .Lassert_fail_%d\n", fail_label);
    api->emit(cg, "    jmp .Lassert_end_%d\n", end_label);
    api->emit(cg, ".Lassert_fail_%d:\n", fail_label);
    api->emit(cg, "    call pie_assert_fail\n");
    api->emit(cg, ".Lassert_end_%d:\n", end_label);
    return PIE_ASM_GEN_OK;
  }

  if (stmt->kind == PIE_IR_STMT_ASSERT_EQ) {
    if (!api->emit_expr(cg, stmt->assert_left)) {
      return PIE_ASM_GEN_ERROR;
    }
    api->emit(cg, "    push rax\n");

    if (!api->emit_expr(cg, stmt->assert_right)) {
      return PIE_ASM_GEN_ERROR;
    }
    api->emit(cg, "    pop rcx\n");

    api->emit(cg, "    cmp rcx, rax\n");

    int pass_label = api->next_label(cg);
    int end_label = api->next_label(cg);
    api->emit(cg, "    jne .Lassert_eq_fail_%d\n", pass_label);
    api->emit(cg, "    jmp .Lassert_end_%d\n", end_label);
    api->emit(cg, ".Lassert_eq_fail_%d:\n", pass_label);
    api->emit(cg, "    mov rdi, rcx\n");
    api->emit(cg, "    call pie_assert_eq_fail\n");
    api->emit(cg, ".Lassert_end_%d:\n", end_label);
    return PIE_ASM_GEN_OK;
  }

  return PIE_ASM_GEN_NO_MATCH;
}
