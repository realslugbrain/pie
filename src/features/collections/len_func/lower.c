#include "pie/core/lower/lower.h"

#include <string.h>

PieLowerResult pie_feature_len_func_lower_expr(PieLowerContext *ctx,
                                               const PieExpr *expr,
                                               PieIrExpr **out_expr) {
  if (expr->kind != PIE_EXPR_CALL || !expr->call_name ||
      strcmp(expr->call_name, "len") != 0) {
    return PIE_LOWER_NO_MATCH;
  }

  if (expr->call_arg_count != 1) {
    ctx->api->error(ctx->lower, "len() expects exactly 1 argument");
    return PIE_LOWER_ERROR;
  }

  PieIrExpr *arg = NULL;
  if (ctx->api->lower_expr(ctx->lower, expr->call_args[0].expr, &arg) !=
      PIE_LOWER_OK) {
    return PIE_LOWER_ERROR;
  }

  const char *runtime_name = NULL;
  if (arg->type == PIE_IR_TYPE_LIST) {
    runtime_name = "pie_list_len";
  } else if (arg->type == PIE_IR_TYPE_STRING) {
    runtime_name = "pie_string_len";
  } else if (arg->type == PIE_IR_TYPE_STRUCT && arg->struct_name &&
             strcmp(arg->struct_name, "map") == 0) {
    runtime_name = "pie_map_len";
  } else {
    ctx->api->error(ctx->lower, "len() argument must be list, string, or map");
    pie_ir_expr_free(arg);
    return PIE_LOWER_ERROR;
  }

  PieIrExpr *call = pie_ir_expr_call(runtime_name, PIE_IR_TYPE_INT);
  if (!call) {
    pie_ir_expr_free(arg);
    ctx->api->error(ctx->lower, "out of memory while lowering len() call");
    return PIE_LOWER_ERROR;
  }
  if (!pie_ir_expr_call_add_arg(call, arg)) {
    pie_ir_expr_free(call);
    ctx->api->error(ctx->lower, "out of memory while lowering len() call");
    return PIE_LOWER_ERROR;
  }
  *out_expr = call;
  return PIE_LOWER_OK;
}
