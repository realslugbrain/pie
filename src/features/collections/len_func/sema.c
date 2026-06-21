#include "pie/core/sema/sema.h"

#include <string.h>

PieSemaResult pie_feature_len_func_sema_expr(PieSemaContext *ctx,
                                             const PieExpr *expr,
                                             PieType *out_type) {
  if (expr->kind != PIE_EXPR_CALL || !expr->call_name ||
      strcmp(expr->call_name, "len") != 0) {
    return PIE_SEMA_NO_MATCH;
  }

  if (expr->call_arg_count != 1) {
    ctx->api->error(ctx->sema, "len() expects exactly 1 argument");
    out_type->kind = PIE_TYPE_ERROR;
    return PIE_SEMA_ERROR;
  }

  PieType arg_type;
  if (ctx->api->check_expr(ctx->sema, expr->call_args[0].expr, &arg_type) !=
      PIE_SEMA_OK) {
    out_type->kind = PIE_TYPE_ERROR;
    return PIE_SEMA_ERROR;
  }

  if (arg_type.kind != PIE_TYPE_LIST && arg_type.kind != PIE_TYPE_STRING &&
      !(arg_type.kind == PIE_TYPE_STRUCT && arg_type.struct_name &&
        strcmp(arg_type.struct_name, "map") == 0)) {
    ctx->api->errorf(ctx->sema, "len() expects list, string, or map, got %s",
                     ctx->api->type_name(arg_type));
    out_type->kind = PIE_TYPE_ERROR;
    return PIE_SEMA_ERROR;
  }

  out_type->kind = PIE_TYPE_INT;
  out_type->type_width = PIE_WIDTH_INFER;
  return PIE_SEMA_OK;
}
