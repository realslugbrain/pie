#include "pie/backend/asm/asm_codegen.h"

PieAsmGenResult
pie_feature_string_interp_codegen_expr(PieAsmCodegenContext *ctx,
                                       const PieIrExpr *expr) {
  (void)ctx;
  (void)expr;
  return PIE_ASM_GEN_NO_MATCH;
}
