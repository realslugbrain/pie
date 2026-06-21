#include "pie/core/sema/sema.h"

PieSemaResult pie_feature_assert_sema_stmt(PieSemaContext *ctx,
                                           const PieStmt *stmt) {
  if (stmt->kind == PIE_STMT_ASSERT) {
    PieType cond_type;
    if (ctx->api->check_expr(ctx->sema, stmt->target, &cond_type) !=
        PIE_SEMA_OK) {
      return PIE_SEMA_ERROR;
    }
    if (cond_type.kind != PIE_TYPE_BOOL) {
      ctx->api->errorf(ctx->sema, "assert condition must be bool, got %s",
                       ctx->api->type_name(cond_type));
      return PIE_SEMA_ERROR;
    }
    return PIE_SEMA_OK;
  }

  if (stmt->kind == PIE_STMT_ASSERT_EQ) {
    PieType left_type;
    if (ctx->api->check_expr(ctx->sema, stmt->target, &left_type) !=
        PIE_SEMA_OK) {
      return PIE_SEMA_ERROR;
    }
    PieType right_type;
    if (ctx->api->check_expr(ctx->sema, stmt->expr, &right_type) !=
        PIE_SEMA_OK) {
      return PIE_SEMA_ERROR;
    }
    if (left_type.kind != right_type.kind) {
      ctx->api->errorf(ctx->sema, "assert_eq types must match, got %s and %s",
                       ctx->api->type_name(left_type),
                       ctx->api->type_name(right_type));
      return PIE_SEMA_ERROR;
    }
    return PIE_SEMA_OK;
  }

  return PIE_SEMA_NO_MATCH;
}
