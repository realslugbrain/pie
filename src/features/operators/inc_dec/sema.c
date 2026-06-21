#include "pie/core/sema/sema.h"

static int require_int(PieSemaContext *ctx, PieType type, const char *what) {
  if (type.kind == PIE_TYPE_INT) {
    return 1;
  }
  ctx->api->errorf(ctx->sema, "%s must be int, got %s", what,
                   ctx->api->type_name(type));
  return 0;
}

static int require_lvalue(const PieExpr *expr) {
  return expr->kind == PIE_EXPR_VAR;
}

PieSemaResult pie_feature_incdec_sema_expr(PieSemaContext *ctx,
                                           const PieExpr *expr,
                                           PieType *out_type) {
  if (expr->kind == PIE_EXPR_UNARY) {
    if (expr->op_text[0] != '+' && expr->op_text[0] != '-') {
      return PIE_SEMA_NO_MATCH;
    }
    if (expr->op_text[1] != '+' && expr->op_text[1] != '-') {
      return PIE_SEMA_NO_MATCH;
    }

    PieType inner;
    if (ctx->api->check_expr(ctx->sema, expr->right, &inner) != PIE_SEMA_OK) {
      out_type->kind = PIE_TYPE_ERROR;
      return PIE_SEMA_ERROR;
    }
    if (!require_lvalue(expr->right)) {
      ctx->api->error(ctx->sema,
                      "prefix increment/decrement operand must be an lvalue");
      out_type->kind = PIE_TYPE_ERROR;
      return PIE_SEMA_ERROR;
    }
    if (!require_int(ctx, inner, "prefix inc/dec operand")) {
      out_type->kind = PIE_TYPE_ERROR;
      return PIE_SEMA_ERROR;
    }
    out_type->kind = inner.kind;
    return PIE_SEMA_OK;
  }

  if (expr->kind == PIE_EXPR_POSTFIX) {
    if (expr->op_text[0] != '+' && expr->op_text[0] != '-') {
      return PIE_SEMA_NO_MATCH;
    }
    if (expr->op_text[1] != '+' && expr->op_text[1] != '-') {
      return PIE_SEMA_NO_MATCH;
    }

    PieType inner;
    if (ctx->api->check_expr(ctx->sema, expr->right, &inner) != PIE_SEMA_OK) {
      out_type->kind = PIE_TYPE_ERROR;
      return PIE_SEMA_ERROR;
    }
    if (!require_lvalue(expr->right)) {
      ctx->api->error(ctx->sema,
                      "postfix increment/decrement operand must be an lvalue");
      out_type->kind = PIE_TYPE_ERROR;
      return PIE_SEMA_ERROR;
    }
    if (!require_int(ctx, inner, "postfix inc/dec operand")) {
      out_type->kind = PIE_TYPE_ERROR;
      return PIE_SEMA_ERROR;
    }
    out_type->kind = inner.kind;
    return PIE_SEMA_OK;
  }

  return PIE_SEMA_NO_MATCH;
}
