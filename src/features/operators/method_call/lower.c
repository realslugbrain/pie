#include "pie/core/lower/lower.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *map_method_runtime(const char *method_name) {
  if (!method_name)
    return NULL;
  if (strcmp(method_name, "get") == 0)
    return "pie_map_get";
  if (strcmp(method_name, "put") == 0)
    return "pie_map_put";
  if (strcmp(method_name, "len") == 0)
    return "pie_map_len";
  return NULL;
}

static const char *list_method_runtime(const char *method_name) {
  if (!method_name)
    return NULL;
  if (strcmp(method_name, "len") == 0)
    return "pie_list_len";
  if (strcmp(method_name, "push") == 0)
    return "pie_list_push";
  if (strcmp(method_name, "pop") == 0)
    return "pie_list_pop";
  if (strcmp(method_name, "insert") == 0)
    return "pie_list_insert";
  if (strcmp(method_name, "remove") == 0)
    return "pie_list_remove";
  if (strcmp(method_name, "reverse") == 0)
    return "pie_list_reverse";
  if (strcmp(method_name, "sort") == 0)
    return "pie_list_sort";
  return NULL;
}

static const char *string_method_runtime(const char *method_name) {
  if (!method_name)
    return NULL;
  if (strcmp(method_name, "contains") == 0)
    return "pie_string_contains";
  if (strcmp(method_name, "upper") == 0)
    return "pie_string_upper";
  if (strcmp(method_name, "lower") == 0)
    return "pie_string_lower";
  if (strcmp(method_name, "trim") == 0)
    return "pie_string_trim";
  if (strcmp(method_name, "replace") == 0)
    return "pie_string_replace";
  if (strcmp(method_name, "repeat") == 0)
    return "pie_string_repeat";
  return NULL;
}

static const char *channel_method_runtime(const char *method_name) {
  if (!method_name)
    return NULL;
  if (strcmp(method_name, "send") == 0)
    return "pie_channel_send";
  if (strcmp(method_name, "recv") == 0)
    return "pie_channel_recv";
  if (strcmp(method_name, "close") == 0)
    return "pie_channel_close";
  return NULL;
}

static const char *find_struct_name_for_receiver(PieLowerContext *ctx,
                                                 const PieExpr *ast_expr) {
  if (!ast_expr)
    return NULL;
  if (ast_expr->kind == PIE_EXPR_VAR) {
    size_t local_id = 0;
    int is_mut = 0;
    PieIrTypeKind local_type = PIE_IR_TYPE_UNKNOWN;
    int local_type_width = 0;
    PieIrTypeKind raw_pointee_type = PIE_IR_TYPE_UNKNOWN;
    int raw_pointee_width = PIE_WIDTH_INFER;
    const char *struct_name = NULL;
    const char *enum_name = NULL;
    if (ctx->api->find_local(ctx->lower, ast_expr->name, &local_id, &is_mut,
                             &local_type, &local_type_width, &raw_pointee_type,
                             &raw_pointee_width, NULL, NULL, &struct_name,
                             &enum_name)) {
      return struct_name;
    }
  }
  return NULL;
}

PieLowerResult pie_feature_method_call_lower_expr(PieLowerContext *ctx,
                                                  const PieExpr *expr,
                                                  PieIrExpr **out_expr) {
  if (expr->kind != PIE_EXPR_METHOD_CALL) {
    return PIE_LOWER_NO_MATCH;
  }

  PieIrExpr *obj = NULL;
  if (ctx->api->lower_expr(ctx->lower, expr->left, &obj) != PIE_LOWER_OK) {
    return PIE_LOWER_ERROR;
  }

  const char *runtime_name = NULL;

  if (expr->left && expr->left->kind == PIE_EXPR_MAP) {
    runtime_name = map_method_runtime(expr->method_name);
  } else if (obj && obj->type == PIE_IR_TYPE_LIST) {
    runtime_name = list_method_runtime(expr->method_name);
  } else if (obj && obj->type == PIE_IR_TYPE_STRING) {
    runtime_name = string_method_runtime(expr->method_name);
  } else if (obj && obj->type == PIE_IR_TYPE_CHANNEL) {
    runtime_name = channel_method_runtime(expr->method_name);
  }

  if (!runtime_name) {
    runtime_name = map_method_runtime(expr->method_name);
    if (!runtime_name) {
      runtime_name = list_method_runtime(expr->method_name);
    }
    if (!runtime_name) {
      runtime_name = string_method_runtime(expr->method_name);
    }
    if (!runtime_name) {
      runtime_name = channel_method_runtime(expr->method_name);
    }
  }

  if (runtime_name) {
    PieIrTypeKind ret_type = PIE_IR_TYPE_VOID;
    if (strcmp(runtime_name, "pie_list_len") == 0 ||
        strcmp(runtime_name, "pie_list_pop") == 0 ||
        strcmp(runtime_name, "pie_list_get") == 0) {
      ret_type = PIE_IR_TYPE_INT;
    } else if (strcmp(runtime_name, "pie_string_contains") == 0) {
      ret_type = PIE_IR_TYPE_BOOL;
    } else if (strcmp(runtime_name, "pie_string_upper") == 0 ||
               strcmp(runtime_name, "pie_string_lower") == 0 ||
               strcmp(runtime_name, "pie_string_trim") == 0 ||
               strcmp(runtime_name, "pie_string_replace") == 0 ||
               strcmp(runtime_name, "pie_string_repeat") == 0) {
      ret_type = PIE_IR_TYPE_STRING;
    }
    PieIrExpr *call = pie_ir_expr_method_call(NULL, runtime_name, ret_type);
    if (!call) {
      ctx->api->error(ctx->lower, "out of memory");
      return PIE_LOWER_ERROR;
    }
    if (!pie_ir_expr_call_add_arg(call, obj)) {
      ctx->api->error(ctx->lower, "out of memory");
      return PIE_LOWER_ERROR;
    }
    for (size_t i = 0; i < expr->call_arg_count; i++) {
      PieIrExpr *arg = NULL;
      if (ctx->api->lower_expr(ctx->lower, expr->call_args[i].expr, &arg) !=
          PIE_LOWER_OK) {
        return PIE_LOWER_ERROR;
      }
      if (!pie_ir_expr_call_add_arg(call, arg)) {
        ctx->api->error(ctx->lower, "out of memory");
        return PIE_LOWER_ERROR;
      }
    }
    *out_expr = call;
    return PIE_LOWER_OK;
  }

  const char *struct_name = find_struct_name_for_receiver(ctx, expr->left);
  if (struct_name && expr->method_name) {
    size_t mangled_len = strlen(struct_name) + 1 + strlen(expr->method_name);
    char *mangled = (char *)malloc(mangled_len + 1);
    if (!mangled) {
      ctx->api->error(ctx->lower, "out of memory while building method name");
      return PIE_LOWER_ERROR;
    }
    memcpy(mangled, struct_name, strlen(struct_name));
    mangled[strlen(struct_name)] = '_';
    memcpy(mangled + strlen(struct_name) + 1, expr->method_name,
           strlen(expr->method_name));
    mangled[mangled_len] = '\0';

    PieLowerFunctionInfo function;
    if (ctx->api->find_function(ctx->lower, mangled, &function)) {
      *out_expr = pie_ir_expr_call(mangled, function.return_type);
      if (!*out_expr) {
        free(mangled);
        ctx->api->error(ctx->lower, "out of memory while lowering method call");
        return PIE_LOWER_ERROR;
      }
      (*out_expr)->type_width = function.return_type_width;

      if (!pie_ir_expr_call_add_arg(*out_expr, obj)) {
        free(mangled);
        ctx->api->error(ctx->lower, "out of memory");
        return PIE_LOWER_ERROR;
      }

      for (size_t i = 0; i < expr->call_arg_count; i++) {
        PieIrExpr *arg = NULL;
        if (ctx->api->lower_expr(ctx->lower, expr->call_args[i].expr, &arg) !=
            PIE_LOWER_OK) {
          free(mangled);
          pie_ir_expr_free(*out_expr);
          *out_expr = NULL;
          return PIE_LOWER_ERROR;
        }
        if (!pie_ir_expr_call_add_arg(*out_expr, arg)) {
          free(mangled);
          pie_ir_expr_free(arg);
          pie_ir_expr_free(*out_expr);
          *out_expr = NULL;
          ctx->api->error(ctx->lower, "out of memory");
          return PIE_LOWER_ERROR;
        }
      }
      free(mangled);
      return PIE_LOWER_OK;
    }
    free(mangled);
  }

  ctx->api->errorf(ctx->lower, "no runtime for method '%s'",
                   expr->method_name ? expr->method_name : "?");
  return PIE_LOWER_ERROR;
}
