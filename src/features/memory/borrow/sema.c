#include "pie/core/sema/sema.h"

#include <string.h>

PieSemaResult pie_feature_borrow_sema_expr(PieSemaContext *ctx,
                                           const PieExpr *expr,
                                           PieType *out_type) {
  if (expr->kind != PIE_EXPR_UNARY ||
      (strcmp(expr->op_text, "&") != 0 && strcmp(expr->op_text, "&mut") != 0)) {
    return PIE_SEMA_NO_MATCH;
  }

  if (!expr->right || expr->right->kind != PIE_EXPR_VAR) {
    ctx->api->error(ctx->sema,
                    "borrow expressions currently require a local variable");
    out_type->kind = PIE_TYPE_ERROR;
    return PIE_SEMA_ERROR;
  }

  PieSymbolInfo symbol;
  if (!ctx->api->find_symbol(ctx->sema, expr->right->name, &symbol)) {
    ctx->api->errorf(ctx->sema, "undefined variable '%s'", expr->right->name);
    out_type->kind = PIE_TYPE_ERROR;
    return PIE_SEMA_ERROR;
  }

  if (strcmp(expr->op_text, "&mut") == 0) {
    if (!symbol.is_mut) {
      ctx->api->errorf(
          ctx->sema,
          "cannot mutably borrow immutable variable '%s'; declare it with mut",
          expr->right->name);
      out_type->kind = PIE_TYPE_ERROR;
      return PIE_SEMA_ERROR;
    }
    out_type->kind = PIE_TYPE_REF_MUT;
    out_type->ref_inner_kind = symbol.type.kind;
    out_type->ref_inner_width = symbol.type.type_width;
    return PIE_SEMA_OK;
  }

  out_type->kind = PIE_TYPE_REF;
  out_type->ref_inner_kind = symbol.type.kind;
  out_type->ref_inner_width = symbol.type.type_width;
  return PIE_SEMA_OK;
}
