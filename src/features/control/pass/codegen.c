#include "pie/backend/asm/asm_codegen.h"

PieAsmGenResult pie_feature_pass_codegen_stmt(PieAsmCodegenContext *ctx,
                                              const PieIrStmt *stmt) {
  (void)ctx;
  if (stmt->kind != PIE_IR_STMT_PASS) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  return PIE_ASM_GEN_OK;
}
