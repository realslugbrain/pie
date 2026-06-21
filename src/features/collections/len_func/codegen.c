#include "pie/backend/asm/asm_codegen.h"

#include <string.h>

static PieAsmGenResult codegen_len_call(PieAsmCodegenContext *ctx,
                                        const PieIrExpr *expr) {
  const PieAsmCodegenApi *api = ctx->api;
  PieAsmCodegen *cg = ctx->cg;

  if (expr->call_arg_count != 1) {
    api->error(ctx->cg, "len() expects exactly 1 argument");
    return PIE_ASM_GEN_ERROR;
  }

  if (!api->emit_expr(cg, expr->call_args[0].expr)) {
    return PIE_ASM_GEN_ERROR;
  }
  api->emit(cg, "    mov rdi, rax\n");

  if (strcmp(expr->call_name, "pie_list_len") == 0) {
    api->emit(cg, "    call pie_list_len\n");
  } else if (strcmp(expr->call_name, "pie_string_len") == 0) {
    api->emit(cg, "    mov rax, rdx\n");
  } else if (strcmp(expr->call_name, "pie_map_len") == 0) {
    api->emit(cg, "    call pie_map_len\n");
  }

  return PIE_ASM_GEN_OK;
}

PieAsmGenResult pie_feature_len_func_codegen_expr(PieAsmCodegenContext *ctx,
                                                  const PieIrExpr *expr) {
  if (expr->kind != PIE_IR_EXPR_CALL || !expr->call_name) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  if (strcmp(expr->call_name, "pie_list_len") == 0 ||
      strcmp(expr->call_name, "pie_string_len") == 0 ||
      strcmp(expr->call_name, "pie_map_len") == 0) {
    return codegen_len_call(ctx, expr);
  }

  return PIE_ASM_GEN_NO_MATCH;
}
