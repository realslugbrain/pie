#include "pie/backend/asm/asm_codegen.h"

PieAsmGenResult pie_feature_range_codegen_expr(PieAsmCodegenContext *ctx,
                                               const PieIrExpr *expr) {
  if (expr->kind != PIE_IR_EXPR_RANGE) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  if (!ctx->api->emit_expr(ctx->cg, expr->range_start)) {
    return PIE_ASM_GEN_ERROR;
  }

  return PIE_ASM_GEN_OK;
}
