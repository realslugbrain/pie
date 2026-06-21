#include "pie/core/sema/sema.h"

#include <stdlib.h>
#include <string.h>

PieSemaResult pie_feature_threads_sema_expr(PieSemaContext *ctx,
                                            const PieExpr *expr,
                                            PieType *out_type) {
  if (expr->kind != PIE_EXPR_THREAD_CALL) {
    return PIE_SEMA_NO_MATCH;
  }

  for (size_t i = 0; i < expr->call_arg_count; i++) {
    PieType arg_type;
    if (ctx->api->check_expr(ctx->sema, expr->call_args[i].expr, &arg_type) !=
        PIE_SEMA_OK) {
      return PIE_SEMA_ERROR;
    }

    switch (expr->thread_op) {
    case PIE_THREAD_SPAWN:
      if (i == 0 && arg_type.kind != PIE_TYPE_CLOSURE) {
        ctx->api->errorf(ctx->sema, "thread.spawn() expects a closure, got %s",
                         ctx->api->type_name(arg_type));
        return PIE_SEMA_ERROR;
      }
      break;
    case PIE_THREAD_JOIN:
      if (i == 0 && arg_type.kind != PIE_TYPE_THREAD) {
        ctx->api->errorf(ctx->sema,
                         "thread.join() expects a thread handle, got %s",
                         ctx->api->type_name(arg_type));
        return PIE_SEMA_ERROR;
      }
      break;
    case PIE_THREAD_MUTEX_CREATE:
      break;
    case PIE_THREAD_MUTEX_LOCK:
    case PIE_THREAD_MUTEX_UNLOCK:
      if (i == 0 && arg_type.kind != PIE_TYPE_MUTEX) {
        ctx->api->errorf(ctx->sema, "thread.%s() expects a mutex, got %s",
                         expr->thread_op == PIE_THREAD_MUTEX_LOCK
                             ? "mutex_lock"
                             : "mutex_unlock",
                         ctx->api->type_name(arg_type));
        return PIE_SEMA_ERROR;
      }
      break;
    case PIE_THREAD_SLEEP:
      if (i == 0 && arg_type.kind != PIE_TYPE_INT) {
        ctx->api->errorf(ctx->sema,
                         "thread.sleep() expects an int (milliseconds), got %s",
                         ctx->api->type_name(arg_type));
        return PIE_SEMA_ERROR;
      }
      break;
    case PIE_THREAD_CHANNEL_CREATE:
      if (i == 0 && arg_type.kind != PIE_TYPE_INT) {
        ctx->api->errorf(ctx->sema,
                         "thread.channel() expects an int capacity, got %s",
                         ctx->api->type_name(arg_type));
        return PIE_SEMA_ERROR;
      }
      break;
    case PIE_THREAD_CHANNEL_SEND:
      if (i == 0 && arg_type.kind != PIE_TYPE_CHANNEL) {
        ctx->api->errorf(ctx->sema,
                         "thread.channel_send() expects a channel, got %s",
                         ctx->api->type_name(arg_type));
        return PIE_SEMA_ERROR;
      }
      break;
    case PIE_THREAD_CHANNEL_RECV:
      if (i == 0 && arg_type.kind != PIE_TYPE_CHANNEL) {
        ctx->api->errorf(ctx->sema,
                         "thread.channel_recv() expects a channel, got %s",
                         ctx->api->type_name(arg_type));
        return PIE_SEMA_ERROR;
      }
      break;
    case PIE_THREAD_CHANNEL_CLOSE:
      if (i == 0 && arg_type.kind != PIE_TYPE_CHANNEL) {
        ctx->api->errorf(ctx->sema,
                         "thread.channel_close() expects a channel, got %s",
                         ctx->api->type_name(arg_type));
        return PIE_SEMA_ERROR;
      }
      break;
    }
  }

  switch (expr->thread_op) {
  case PIE_THREAD_SPAWN:
    out_type->kind = PIE_TYPE_THREAD;
    break;
  case PIE_THREAD_JOIN:
    out_type->kind = PIE_TYPE_VOID;
    break;
  case PIE_THREAD_MUTEX_CREATE:
    out_type->kind = PIE_TYPE_MUTEX;
    break;
  case PIE_THREAD_MUTEX_LOCK:
  case PIE_THREAD_MUTEX_UNLOCK:
    out_type->kind = PIE_TYPE_VOID;
    break;
  case PIE_THREAD_SLEEP:
    out_type->kind = PIE_TYPE_VOID;
    break;
  case PIE_THREAD_CHANNEL_CREATE:
    out_type->kind = PIE_TYPE_CHANNEL;
    break;
  case PIE_THREAD_CHANNEL_SEND:
    out_type->kind = PIE_TYPE_VOID;
    break;
  case PIE_THREAD_CHANNEL_RECV:
    out_type->kind = PIE_TYPE_INT;
    break;
  case PIE_THREAD_CHANNEL_CLOSE:
    out_type->kind = PIE_TYPE_VOID;
    break;
  }

  return PIE_SEMA_OK;
}
