#include "pie/core/sema/sema.h"

PieSemaResult pie_feature_print_sema_stmt(PieSemaContext *ctx,
                                          const PieStmt *stmt) {
  if (stmt->kind != PIE_STMT_PRINT) {
    return PIE_SEMA_NO_MATCH;
  }

  for (size_t i = 0; i < stmt->arg_count; i++) {
    const PiePrintArg *arg = &stmt->args[i];
    if (arg->is_string) {
      continue;
    }

    PieType arg_type;
    if (ctx->api->check_expr(ctx->sema, arg->expr, &arg_type) != PIE_SEMA_OK) {
      return PIE_SEMA_ERROR;
    }
    if (arg_type.kind != PIE_TYPE_INT && arg_type.kind != PIE_TYPE_FLOAT &&
        arg_type.kind != PIE_TYPE_CHAR && arg_type.kind != PIE_TYPE_BYTE &&
        arg_type.kind != PIE_TYPE_BOOL && arg_type.kind != PIE_TYPE_STRING &&
        arg_type.kind != PIE_TYPE_REF && arg_type.kind != PIE_TYPE_REF_MUT) {
      ctx->api->errorf(ctx->sema,
                       "print expression must be int, float, char, byte, bool, "
                       "string, &string, or &mut string, got %s",
                       ctx->api->type_name(arg_type));
      return PIE_SEMA_ERROR;
    }
  }

  return PIE_SEMA_OK;
}
