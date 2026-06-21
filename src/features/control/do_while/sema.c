#include "pie/core/sema/sema.h"

PieSemaResult pie_feature_do_while_sema_stmt(PieSemaContext *ctx,
                                             const PieStmt *stmt) {
  if (stmt->kind != PIE_STMT_DO_WHILE) {
    return PIE_SEMA_NO_MATCH;
  }

  ctx->api->enter_loop(ctx->sema);
  PieSemaResult body_result =
      ctx->api->check_block(ctx->sema, stmt->then_branch);
  ctx->api->leave_loop(ctx->sema);
  if (body_result != PIE_SEMA_OK) {
    return PIE_SEMA_ERROR;
  }

  PieType cond_type;
  if (ctx->api->check_expr(ctx->sema, stmt->expr, &cond_type) != PIE_SEMA_OK) {
    return PIE_SEMA_ERROR;
  }
  if (cond_type.kind != PIE_TYPE_BOOL) {
    ctx->api->errorf(ctx->sema, "do-while condition must be bool, got %s",
                     ctx->api->type_name(cond_type));
    return PIE_SEMA_ERROR;
  }

  return PIE_SEMA_OK;
}
