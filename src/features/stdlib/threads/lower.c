#include "pie/core/lower/lower.h"

#include <stdlib.h>
#include <string.h>

static PieIrExprKind ir_kind_for_thread_op(PieThreadOp op) {
  switch (op) {
  case PIE_THREAD_SPAWN:
    return PIE_IR_EXPR_THREAD_SPAWN;
  case PIE_THREAD_JOIN:
    return PIE_IR_EXPR_THREAD_JOIN;
  case PIE_THREAD_MUTEX_CREATE:
    return PIE_IR_EXPR_MUTEX_CREATE;
  case PIE_THREAD_MUTEX_LOCK:
    return PIE_IR_EXPR_MUTEX_LOCK;
  case PIE_THREAD_MUTEX_UNLOCK:
    return PIE_IR_EXPR_MUTEX_UNLOCK;
  case PIE_THREAD_SLEEP:
    return PIE_IR_EXPR_THREAD_SLEEP;
  case PIE_THREAD_CHANNEL_CREATE:
    return PIE_IR_EXPR_CHANNEL_CREATE;
  case PIE_THREAD_CHANNEL_SEND:
    return PIE_IR_EXPR_CHANNEL_SEND;
  case PIE_THREAD_CHANNEL_RECV:
    return PIE_IR_EXPR_CHANNEL_RECV;
  case PIE_THREAD_CHANNEL_CLOSE:
    return PIE_IR_EXPR_CHANNEL_CLOSE;
  }
  return PIE_IR_EXPR_CALL;
}

static PieIrTypeKind ir_type_for_thread_op(PieThreadOp op) {
  switch (op) {
  case PIE_THREAD_SPAWN:
    return PIE_IR_TYPE_THREAD;
  case PIE_THREAD_JOIN:
    return PIE_IR_TYPE_VOID;
  case PIE_THREAD_MUTEX_CREATE:
    return PIE_IR_TYPE_MUTEX;
  case PIE_THREAD_MUTEX_LOCK:
  case PIE_THREAD_MUTEX_UNLOCK:
    return PIE_IR_TYPE_VOID;
  case PIE_THREAD_SLEEP:
    return PIE_IR_TYPE_VOID;
  case PIE_THREAD_CHANNEL_CREATE:
    return PIE_IR_TYPE_CHANNEL;
  case PIE_THREAD_CHANNEL_SEND:
    return PIE_IR_TYPE_VOID;
  case PIE_THREAD_CHANNEL_RECV:
    return PIE_IR_TYPE_INT;
  case PIE_THREAD_CHANNEL_CLOSE:
    return PIE_IR_TYPE_VOID;
  }
  return PIE_IR_TYPE_VOID;
}

PieLowerResult pie_feature_threads_lower_expr(PieLowerContext *ctx,
                                              const PieExpr *expr,
                                              PieIrExpr **out_expr) {
  if (expr->kind != PIE_EXPR_THREAD_CALL) {
    return PIE_LOWER_NO_MATCH;
  }

  PieIrExpr *ir = (PieIrExpr *)calloc(1, sizeof(PieIrExpr));
  if (!ir) {
    ctx->api->error(ctx->lower, "out of memory while lowering thread call");
    return PIE_LOWER_ERROR;
  }

  ir->kind = ir_kind_for_thread_op(expr->thread_op);
  ir->type = ir_type_for_thread_op(expr->thread_op);

  for (size_t i = 0; i < expr->call_arg_count; i++) {
    PieIrExpr *arg = NULL;
    if (ctx->api->lower_expr(ctx->lower, expr->call_args[i].expr, &arg) !=
        PIE_LOWER_OK) {
      pie_ir_expr_free(ir);
      return PIE_LOWER_ERROR;
    }
    if (!pie_ir_expr_call_add_arg(ir, arg)) {
      pie_ir_expr_free(arg);
      pie_ir_expr_free(ir);
      ctx->api->error(ctx->lower,
                      "out of memory while lowering thread call argument");
      return PIE_LOWER_ERROR;
    }
  }

  *out_expr = ir;
  return PIE_LOWER_OK;
}
