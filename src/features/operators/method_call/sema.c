#include "pie/core/sema/sema.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int find_method_return(const char *type_name, const char *method_name,
                              PieTypeKind *out_return, int *out_width) {
  if (!type_name || !method_name)
    return 0;

  if (strcmp(type_name, "map") == 0) {
    if (strcmp(method_name, "get") == 0) {
      *out_return = PIE_TYPE_INT;
      *out_width = PIE_WIDTH_INFER;
      return 1;
    }
    if (strcmp(method_name, "put") == 0) {
      *out_return = PIE_TYPE_VOID;
      *out_width = PIE_WIDTH_INFER;
      return 1;
    }
    if (strcmp(method_name, "len") == 0) {
      *out_return = PIE_TYPE_INT;
      *out_width = PIE_WIDTH_INFER;
      return 1;
    }
  }

  if (strcmp(type_name, "list") == 0) {
    if (strcmp(method_name, "len") == 0) {
      *out_return = PIE_TYPE_INT;
      *out_width = PIE_WIDTH_INFER;
      return 1;
    }
    if (strcmp(method_name, "push") == 0 ||
        strcmp(method_name, "insert") == 0 ||
        strcmp(method_name, "remove") == 0 ||
        strcmp(method_name, "reverse") == 0 ||
        strcmp(method_name, "sort") == 0) {
      *out_return = PIE_TYPE_VOID;
      *out_width = PIE_WIDTH_INFER;
      return 1;
    }
    if (strcmp(method_name, "pop") == 0) {
      *out_return = PIE_TYPE_INT;
      *out_width = PIE_WIDTH_INFER;
      return 1;
    }
  }

  if (strcmp(type_name, "string") == 0) {
    if (strcmp(method_name, "contains") == 0) {
      *out_return = PIE_TYPE_BOOL;
      *out_width = PIE_WIDTH_INFER;
      return 1;
    }
    if (strcmp(method_name, "upper") == 0 ||
        strcmp(method_name, "lower") == 0 || strcmp(method_name, "trim") == 0) {
      *out_return = PIE_TYPE_STRING;
      *out_width = PIE_WIDTH_INFER;
      return 1;
    }
    if (strcmp(method_name, "replace") == 0) {
      *out_return = PIE_TYPE_STRING;
      *out_width = PIE_WIDTH_INFER;
      return 1;
    }
    if (strcmp(method_name, "repeat") == 0) {
      *out_return = PIE_TYPE_STRING;
      *out_width = PIE_WIDTH_INFER;
      return 1;
    }
  }

  if (strcmp(type_name, "channel") == 0) {
    if (strcmp(method_name, "send") == 0) {
      *out_return = PIE_TYPE_VOID;
      *out_width = PIE_WIDTH_INFER;
      return 1;
    }
    if (strcmp(method_name, "recv") == 0) {
      *out_return = PIE_TYPE_INT;
      *out_width = PIE_WIDTH_INFER;
      return 1;
    }
    if (strcmp(method_name, "close") == 0) {
      *out_return = PIE_TYPE_VOID;
      *out_width = PIE_WIDTH_INFER;
      return 1;
    }
  }

  return 0;
}

static const char *type_name_from_kind(PieTypeKind kind,
                                       const PieType *type_info) {
  switch (kind) {
  case PIE_TYPE_STRUCT:
    if (type_info && type_info->struct_name) {
      return type_info->struct_name;
    }
    return "map";
  case PIE_TYPE_MAP:
    return "map";
  case PIE_TYPE_REF:
  case PIE_TYPE_REF_MUT:
    if (type_info && type_info->ref_inner_struct_name) {
      return type_info->ref_inner_struct_name;
    }
    if (type_info && type_info->ref_inner_kind == PIE_TYPE_STRING) {
      return "string";
    }
    return NULL;
  case PIE_TYPE_LIST:
    return "list";
  case PIE_TYPE_STRING:
    return "string";
  case PIE_TYPE_ENUM:
    if (type_info && type_info->enum_name) {
      return type_info->enum_name;
    }
    return "enum";
  case PIE_TYPE_CHANNEL:
    return "channel";
  case PIE_TYPE_THREAD:
    return "thread";
  case PIE_TYPE_MUTEX:
    return "mutex";
  default:
    return NULL;
  }
}

static int is_assignable(PieType target, PieType value) {
  if (target.kind == PIE_TYPE_RAW_PTR && value.kind == PIE_TYPE_RAW_PTR) {
    return target.raw_pointee_kind == value.raw_pointee_kind;
  }
  const char *target_name =
      (target.kind == PIE_TYPE_STRUCT)
          ? target.struct_name
          : (target.kind == PIE_TYPE_ENUM ? target.enum_name : NULL);
  const char *value_name =
      (value.kind == PIE_TYPE_STRUCT)
          ? value.struct_name
          : (value.kind == PIE_TYPE_ENUM ? value.enum_name : NULL);
  if (target_name && value_name) {
    return strcmp(target_name, value_name) == 0;
  }
  if (target.kind == value.kind) {
    return 1;
  }
  return target.kind == PIE_TYPE_REF && value.kind == PIE_TYPE_REF_MUT;
}

PieSemaResult pie_feature_method_call_sema_expr(PieSemaContext *ctx,
                                                const PieExpr *expr,
                                                PieType *out_type) {
  if (expr->kind != PIE_EXPR_METHOD_CALL) {
    return PIE_SEMA_NO_MATCH;
  }

  PieType obj_type;
  if (ctx->api->check_expr(ctx->sema, expr->left, &obj_type) != PIE_SEMA_OK) {
    return PIE_SEMA_ERROR;
  }

  const char *type_name = type_name_from_kind(obj_type.kind, &obj_type);

  PieTypeKind ret_kind = PIE_TYPE_ERROR;
  int ret_width = PIE_WIDTH_INFER;
  if (find_method_return(type_name, expr->method_name, &ret_kind, &ret_width)) {
    for (size_t i = 0; i < expr->call_arg_count; i++) {
      PieType arg_type;
      if (ctx->api->check_expr(ctx->sema, expr->call_args[i].expr, &arg_type) !=
          PIE_SEMA_OK) {
        return PIE_SEMA_ERROR;
      }
    }
    out_type->kind = ret_kind;
    out_type->type_width = ret_width;
    return PIE_SEMA_OK;
  }

  if (type_name && expr->method_name) {
    size_t mangled_len = strlen(type_name) + 1 + strlen(expr->method_name);
    char *mangled = (char *)malloc(mangled_len + 1);
    if (mangled) {
      memcpy(mangled, type_name, strlen(type_name));
      mangled[strlen(type_name)] = '_';
      memcpy(mangled + strlen(type_name) + 1, expr->method_name,
             strlen(expr->method_name));
      mangled[mangled_len] = '\0';

      PieFunctionInfo function;
      if (ctx->api->find_function(ctx->sema, mangled, &function)) {
        free(mangled);

        size_t expected_args =
            function.param_count > 0 ? function.param_count - 1 : 0;
        if (expr->call_arg_count != expected_args) {
          ctx->api->errorf(
              ctx->sema, "method '%s' expects %zu argument(s), got %zu",
              expr->method_name, expected_args, expr->call_arg_count);
          return PIE_SEMA_ERROR;
        }

        for (size_t i = 0; i < expr->call_arg_count; i++) {
          PieType arg_type;
          if (ctx->api->check_expr(ctx->sema, expr->call_args[i].expr,
                                   &arg_type) != PIE_SEMA_OK) {
            return PIE_SEMA_ERROR;
          }
          PieType param_type = function.param_types[i + 1];
          if (!is_assignable(param_type, arg_type)) {
            ctx->api->errorf(
                ctx->sema, "argument %zu of method '%s' must be %s, got %s",
                i + 1, expr->method_name, ctx->api->type_name(param_type),
                ctx->api->type_name(arg_type));
            return PIE_SEMA_ERROR;
          }
        }

        *out_type = function.return_type;
        return PIE_SEMA_OK;
      }
      free(mangled);
    }
  }

  ctx->api->errorf(ctx->sema, "unknown method '%s' on type '%s'",
                   expr->method_name ? expr->method_name : "?",
                   type_name ? type_name : "unknown");
  return PIE_SEMA_ERROR;
}
