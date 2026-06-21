#include "pie/core/sema/sema.h"

#include <string.h>

static int is_comparison_op(const char *op) {
  return strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
         strcmp(op, "<") == 0 || strcmp(op, "<=") == 0 ||
         strcmp(op, ">") == 0 || strcmp(op, ">=") == 0;
}

static int is_equality_op(const char *op) {
  return strcmp(op, "==") == 0 || strcmp(op, "!=") == 0;
}

PieSemaResult pie_feature_comparison_sema_expr(PieSemaContext *ctx,
                                               const PieExpr *expr,
                                               PieType *out_type) {
  if (expr->kind != PIE_EXPR_BINARY || !is_comparison_op(expr->op_text)) {
    return PIE_SEMA_NO_MATCH;
  }

  PieType left;
  PieType right;
  if (ctx->api->check_expr(ctx->sema, expr->left, &left) != PIE_SEMA_OK ||
      ctx->api->check_expr(ctx->sema, expr->right, &right) != PIE_SEMA_OK) {
    out_type->kind = PIE_TYPE_ERROR;
    return PIE_SEMA_ERROR;
  }

  if (left.kind != right.kind) {
    ctx->api->errorf(ctx->sema, "cannot compare %s with %s",
                     ctx->api->type_name(left), ctx->api->type_name(right));
    out_type->kind = PIE_TYPE_ERROR;
    return PIE_SEMA_ERROR;
  }

  if (is_equality_op(expr->op_text)) {
    if (left.kind != PIE_TYPE_INT && left.kind != PIE_TYPE_BOOL &&
        left.kind != PIE_TYPE_STRING && left.kind != PIE_TYPE_STRUCT) {
      ctx->api->errorf(ctx->sema, "operator '%s' does not support %s yet",
                       expr->op_text, ctx->api->type_name(left));
      out_type->kind = PIE_TYPE_ERROR;
      return PIE_SEMA_ERROR;
    }
  } else if (left.kind != PIE_TYPE_INT && left.kind != PIE_TYPE_FLOAT &&
             left.kind != PIE_TYPE_STRUCT) {
    ctx->api->errorf(ctx->sema,
                     "operator '%s' requires numeric operands, got %s",
                     expr->op_text, ctx->api->type_name(left));
    out_type->kind = PIE_TYPE_ERROR;
    return PIE_SEMA_ERROR;
  }

  out_type->kind = PIE_TYPE_BOOL;
  return PIE_SEMA_OK;
}
