#include "pie/backend/asm/asm_codegen.h"

#include <stdint.h>
#include <string.h>

PieAsmGenResult pie_feature_float_codegen_expr(PieAsmCodegenContext *ctx,
                                               const PieIrExpr *expr) {
  if (expr->kind != PIE_IR_EXPR_FLOAT) {
    return PIE_ASM_GEN_NO_MATCH;
  }

  if (expr->type_width == PIE_WIDTH_WIDE) {
    uint64_t bits = 0;
    memcpy(&bits, &expr->float_value, sizeof(bits));
    ctx->api->emit(ctx->cg, "    mov rax, 0x%016llx\n",
                   (unsigned long long)bits);
    ctx->api->emit(ctx->cg, "    movq xmm0, rax\n");
    ctx->api->emit(ctx->cg, "    call pie_float_wide_new\n");
  } else if (expr->type_width == PIE_WIDTH_32) {
    float f = (float)expr->float_value;
    uint32_t bits = 0;
    memcpy(&bits, &f, sizeof(bits));
    ctx->api->emit(ctx->cg, "    mov eax, 0x%08x\n", bits);
    ctx->api->emit(ctx->cg, "    movd xmm0, eax\n");
  } else {
    uint64_t bits = 0;
    memcpy(&bits, &expr->float_value, sizeof(bits));
    ctx->api->emit(ctx->cg, "    mov rax, 0x%016llx\n",
                   (unsigned long long)bits);
    ctx->api->emit(ctx->cg, "    movq xmm0, rax\n");
  }
  return PIE_ASM_GEN_OK;
}
