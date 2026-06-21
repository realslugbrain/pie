#include "pie/core/sema/sema.h"

static PieSemaResult check_branch(PieSemaContext *ctx,
                                  const PieProgram *program) {
  return ctx->api->check_block(ctx->sema, program);
}

PieSemaResult pie_feature_if_sema_stmt(PieSemaContext *ctx,
                                       const PieStmt *stmt) {
  if (stmt->kind != PIE_STMT_IF) {
    return PIE_SEMA_NO_MATCH;
  }

  PieType cond_type;
  if (ctx->api->check_expr(ctx->sema, stmt->expr, &cond_type) != PIE_SEMA_OK) {
    return PIE_SEMA_ERROR;
  }
  if (cond_type.kind != PIE_TYPE_BOOL) {
    ctx->api->errorf(ctx->sema, "if condition must be bool, got %s",
                     ctx->api->type_name(cond_type));
    return PIE_SEMA_ERROR;
  }
  if (stmt->then_branch &&
      check_branch(ctx, stmt->then_branch) != PIE_SEMA_OK) {
    return PIE_SEMA_ERROR;
  }
  if (stmt->else_branch &&
      check_branch(ctx, stmt->else_branch) != PIE_SEMA_OK) {
    return PIE_SEMA_ERROR;
  }
  return PIE_SEMA_OK;
}
