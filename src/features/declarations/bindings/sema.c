#include "pie/core/sema/sema.h"

#include <stdlib.h>
#include <string.h>

static int compatible_width(PieTypeKind kind, int a, int b) {
  if (a == b) {
    return 1;
  }
  if ((kind == PIE_TYPE_INT || kind == PIE_TYPE_FLOAT) &&
      ((a == PIE_WIDTH_INFER && b == PIE_WIDTH_64) ||
       (a == PIE_WIDTH_64 && b == PIE_WIDTH_INFER))) {
    return 1;
  }
  return 0;
}

static int same_type(PieType a, PieType b) {
  if (a.kind == PIE_TYPE_RAW_PTR && b.kind == PIE_TYPE_RAW_PTR) {
    return a.raw_pointee_kind == b.raw_pointee_kind &&
           compatible_width(a.raw_pointee_kind, a.raw_pointee_width,
                            b.raw_pointee_width);
  }
  if (a.kind == PIE_TYPE_NULLABLE && b.kind == PIE_TYPE_NULLABLE) {
    return a.nullable_inner_kind == b.nullable_inner_kind;
  }
  if (a.kind == PIE_TYPE_TUPLE && b.kind == PIE_TYPE_TUPLE) {
    if (a.tuple_element_count != b.tuple_element_count)
      return 0;
    for (size_t i = 0; i < a.tuple_element_count; i++) {
      if (a.tuple_element_kinds[i] != b.tuple_element_kinds[i])
        return 0;
    }
    return 1;
  }
  if (a.kind == PIE_TYPE_LIST && b.kind == PIE_TYPE_LIST) {
    return a.list_element_kind == b.list_element_kind;
  }
  return a.kind == b.kind;
}

static int is_assignable_type(PieType target, PieType value) {
  if (same_type(target, value)) {
    return 1;
  }
  if (target.kind == PIE_TYPE_NULLABLE && value.kind == PIE_TYPE_NULL) {
    return 1;
  }
  if (target.kind == PIE_TYPE_BYTE && value.kind == PIE_TYPE_INT) {
    return 1;
  }
  return target.kind == PIE_TYPE_REF && value.kind == PIE_TYPE_REF_MUT;
}

static int resolve_auto_int_width(long long value) {
  if (value >= -128 && value <= 127)
    return PIE_WIDTH_8;
  if (value >= -32768 && value <= 32767)
    return PIE_WIDTH_16;
  if (value >= -2147483648LL && value <= 2147483647LL)
    return PIE_WIDTH_32;
  if (value >= -9223372036854775807LL - 1 && value <= 9223372036854775807LL)
    return PIE_WIDTH_64;
  return PIE_WIDTH_128;
}

static int resolve_auto_float_width(double value) {
  (void)value;
  return PIE_WIDTH_64;
}

static PieType type_from_annotation(PieAstType annotation) {
  PieType type;
  memset(&type, 0, sizeof(type));
  switch (annotation.kind) {
  case PIE_AST_TYPE_VOID:
    type.kind = PIE_TYPE_VOID;
    type.type_width = annotation.width;
    return type;
  case PIE_AST_TYPE_INT:
    type.kind = PIE_TYPE_INT;
    type.type_width = annotation.width;
    return type;
  case PIE_AST_TYPE_FLOAT:
    type.kind = PIE_TYPE_FLOAT;
    type.type_width = annotation.width;
    return type;
  case PIE_AST_TYPE_CHAR:
    type.kind = PIE_TYPE_CHAR;
    type.type_width = annotation.width;
    return type;
  case PIE_AST_TYPE_BYTE:
    type.kind = PIE_TYPE_BYTE;
    type.type_width = annotation.width;
    return type;
  case PIE_AST_TYPE_BOOL:
    type.kind = PIE_TYPE_BOOL;
    type.type_width = annotation.width;
    return type;
  case PIE_AST_TYPE_STRING:
    type.kind = PIE_TYPE_STRING;
    type.type_width = annotation.width;
    return type;
  case PIE_AST_TYPE_REF:
    type.kind = PIE_TYPE_REF;
    type.type_width = annotation.width;
    type.ref_inner_kind =
        type_from_annotation(
            pie_ast_type(annotation.ref_inner_kind, annotation.ref_inner_width))
            .kind;
    type.ref_inner_width = annotation.ref_inner_width;
    return type;
  case PIE_AST_TYPE_REF_MUT:
    type.kind = PIE_TYPE_REF_MUT;
    type.type_width = annotation.width;
    type.ref_inner_kind =
        type_from_annotation(
            pie_ast_type(annotation.ref_inner_kind, annotation.ref_inner_width))
            .kind;
    type.ref_inner_width = annotation.ref_inner_width;
    return type;
  case PIE_AST_TYPE_RAW_PTR:
    type.kind = PIE_TYPE_RAW_PTR;
    type.type_width = annotation.width;
    type.raw_pointee_kind =
        type_from_annotation(pie_ast_type(annotation.raw_pointee_kind,
                                          annotation.raw_pointee_width))
            .kind;
    type.raw_pointee_width = annotation.raw_pointee_width;
    return type;
  case PIE_AST_TYPE_NULLABLE: {
    PieType inner = type_from_annotation(pie_ast_type(
        annotation.nullable_inner_kind, annotation.nullable_inner_width));
    type.kind = PIE_TYPE_NULLABLE;
    type.nullable_inner_kind = inner.kind;
    type.nullable_inner_width = inner.type_width;
    return type;
  }
  case PIE_AST_TYPE_TUPLE: {
    type.kind = PIE_TYPE_TUPLE;
    type.tuple_element_count = annotation.tuple_element_count;
    for (size_t i = 0; i < annotation.tuple_element_count; i++) {
      PieType elem = type_from_annotation(
          pie_ast_type(annotation.tuple_element_kinds[i],
                       annotation.tuple_element_widths[i]));
      type.tuple_element_kinds[i] = elem.kind;
      type.tuple_element_widths[i] = elem.type_width;
    }
    return type;
  }
  case PIE_AST_TYPE_LIST: {
    PieType elem = type_from_annotation(pie_ast_type(
        annotation.list_element_kind, annotation.list_element_width));
    type.kind = PIE_TYPE_LIST;
    type.list_element_kind = elem.kind;
    type.list_element_width = elem.type_width;
    return type;
  }
  case PIE_AST_TYPE_MAP: {
    PieType key = type_from_annotation(
        pie_ast_type(annotation.map_key_kind, annotation.map_key_width));
    PieType value = type_from_annotation(
        pie_ast_type(annotation.map_value_kind, annotation.map_value_width));
    type.kind = PIE_TYPE_MAP;
    type.map_key_kind = key.kind;
    type.map_key_width = key.type_width;
    type.map_value_kind = value.kind;
    type.map_value_width = value.type_width;
    return type;
  }
  case PIE_AST_TYPE_ENUM:
    type.kind = PIE_TYPE_ENUM;
    type.enum_name = annotation.enum_name;
    return type;
  case PIE_AST_TYPE_STRUCT:
    type.kind = PIE_TYPE_ERROR;
    type.type_width = annotation.width;
    return type;
  case PIE_AST_TYPE_INFER:
    type.kind = PIE_TYPE_ERROR;
    type.type_width = PIE_WIDTH_INFER;
    return type;
  case PIE_AST_TYPE_CLOSURE:
    type.kind = PIE_TYPE_CLOSURE;
    type.func_param_count = annotation.func_param_count;
    if (annotation.func_param_count > 0) {
      type.func_param_kinds = (PieTypeKind *)malloc(
          annotation.func_param_count * sizeof(PieTypeKind));
      type.func_param_widths =
          (int *)malloc(annotation.func_param_count * sizeof(int));
      if (type.func_param_kinds && type.func_param_widths) {
        for (size_t i = 0; i < annotation.func_param_count; i++) {
          type.func_param_kinds[i] =
              (PieTypeKind)annotation.func_param_kinds[i];
          type.func_param_widths[i] = annotation.func_param_widths[i];
        }
      }
    }
    type.func_return_kind = (PieTypeKind)annotation.func_return_kind;
    type.func_return_width = annotation.func_return_width;
    return type;
  case PIE_AST_TYPE_THREAD:
    type.kind = PIE_TYPE_THREAD;
    return type;
  case PIE_AST_TYPE_MUTEX:
    type.kind = PIE_TYPE_MUTEX;
    return type;
  case PIE_AST_TYPE_CHANNEL:
    type.kind = PIE_TYPE_CHANNEL;
    return type;
  }
  type.kind = PIE_TYPE_ERROR;
  type.type_width = PIE_WIDTH_INFER;
  return type;
}

static int require_assignable_type(PieSemaContext *ctx, const char *name,
                                   PieType target, PieType value) {
  if (is_assignable_type(target, value)) {
    return 1;
  }
  if (target.kind == PIE_TYPE_RAW_PTR && value.kind == PIE_TYPE_RAW_PTR) {
    PieType target_pointee;
    PieType value_pointee;
    memset(&target_pointee, 0, sizeof(target_pointee));
    memset(&value_pointee, 0, sizeof(value_pointee));
    target_pointee.kind = target.raw_pointee_kind;
    target_pointee.type_width = target.raw_pointee_width;
    value_pointee.kind = value.raw_pointee_kind;
    value_pointee.type_width = value.raw_pointee_width;
    ctx->api->errorf(ctx->sema,
                     "cannot assign raw pointer to '%s': expected pointer to "
                     "%s, got pointer to %s",
                     name, ctx->api->type_name(target_pointee),
                     ctx->api->type_name(value_pointee));
    return 0;
  }
  ctx->api->errorf(ctx->sema, "cannot assign %s to '%s' of type %s",
                   ctx->api->type_name(value), name,
                   ctx->api->type_name(target));
  return 0;
}

PieSemaResult pie_feature_bindings_sema_expr(PieSemaContext *ctx,
                                             const PieExpr *expr,
                                             PieType *out_type) {
  if (expr->kind != PIE_EXPR_VAR) {
    return PIE_SEMA_NO_MATCH;
  }

  PieSymbolInfo symbol;
  if (!ctx->api->find_symbol(ctx->sema, expr->name, &symbol)) {
    ctx->api->errorf(ctx->sema, "undefined variable '%s'", expr->name);
    out_type->kind = PIE_TYPE_ERROR;
    return PIE_SEMA_ERROR;
  }

  *out_type = symbol.type;
  return PIE_SEMA_OK;
}

PieSemaResult pie_feature_bindings_sema_stmt(PieSemaContext *ctx,
                                             const PieStmt *stmt) {
  if (stmt->kind == PIE_STMT_LET) {
    PieType init_type;
    if (ctx->api->check_expr(ctx->sema, stmt->expr, &init_type) !=
        PIE_SEMA_OK) {
      return PIE_SEMA_ERROR;
    }
    PieType declared_type = type_from_annotation(stmt->type_annotation);
    if (declared_type.kind == PIE_TYPE_ERROR) {
      declared_type = init_type;
    } else if (declared_type.kind == PIE_TYPE_MAP &&
               init_type.kind == PIE_TYPE_MAP &&
               declared_type.map_value_kind == PIE_TYPE_ERROR) {
      declared_type.map_key_kind = init_type.map_key_kind;
      declared_type.map_key_width = init_type.map_key_width;
      declared_type.map_value_kind = init_type.map_value_kind;
      declared_type.map_value_width = init_type.map_value_width;
    }
    if (declared_type.kind == PIE_TYPE_INT &&
        declared_type.type_width == PIE_WIDTH_AUTO) {
      if (stmt->expr->kind == PIE_EXPR_INT) {
        declared_type.type_width =
            resolve_auto_int_width(stmt->expr->int_value);
      }
    }
    if (declared_type.kind == PIE_TYPE_FLOAT &&
        declared_type.type_width == PIE_WIDTH_AUTO) {
      if (stmt->expr->kind == PIE_EXPR_FLOAT) {
        declared_type.type_width =
            resolve_auto_float_width(stmt->expr->float_value);
      }
    }
    if (!require_assignable_type(ctx, stmt->name, declared_type, init_type)) {
      return PIE_SEMA_ERROR;
    }
    if (!ctx->api->declare_symbol(ctx->sema, stmt->name, declared_type,
                                  stmt->is_mut)) {
      return PIE_SEMA_ERROR;
    }
    return PIE_SEMA_OK;
  }

  if (stmt->kind == PIE_STMT_ASSIGN) {
    PieSymbolInfo symbol;
    if (!ctx->api->find_symbol(ctx->sema, stmt->name, &symbol)) {
      ctx->api->errorf(ctx->sema, "cannot assign undefined variable '%s'",
                       stmt->name);
      return PIE_SEMA_ERROR;
    }
    if (!symbol.is_mut && symbol.type.kind != PIE_TYPE_REF_MUT) {
      ctx->api->errorf(
          ctx->sema,
          "cannot assign immutable variable '%s'; declare it with mut",
          stmt->name);
      return PIE_SEMA_ERROR;
    }

    PieType value_type;
    if (ctx->api->check_expr(ctx->sema, stmt->expr, &value_type) !=
        PIE_SEMA_OK) {
      return PIE_SEMA_ERROR;
    }
    if (symbol.type.kind == PIE_TYPE_REF_MUT) {
      if (strcmp(stmt->assign_op, "<-") != 0) {
        ctx->api->errorf(ctx->sema,
                         "compound assignment '%s' requires int, got %s",
                         stmt->assign_op, ctx->api->type_name(symbol.type));
        return PIE_SEMA_ERROR;
      }
      if (value_type.kind != PIE_TYPE_STRING &&
          value_type.kind != PIE_TYPE_REF &&
          value_type.kind != PIE_TYPE_REF_MUT) {
        ctx->api->errorf(ctx->sema, "cannot assign %s through '%s' of type %s",
                         ctx->api->type_name(value_type), stmt->name,
                         ctx->api->type_name(symbol.type));
        return PIE_SEMA_ERROR;
      }
      return PIE_SEMA_OK;
    }
    if (!require_assignable_type(ctx, stmt->name, symbol.type, value_type)) {
      return PIE_SEMA_ERROR;
    }
    if (strcmp(stmt->assign_op, "<-") != 0 &&
        symbol.type.kind != PIE_TYPE_INT) {
      ctx->api->errorf(ctx->sema,
                       "compound assignment '%s' requires int, got %s",
                       stmt->assign_op, ctx->api->type_name(symbol.type));
      return PIE_SEMA_ERROR;
    }
    return PIE_SEMA_OK;
  }

  if (stmt->kind == PIE_STMT_ASSIGN_MULTI) {
    for (size_t i = 0; i < stmt->multi_count; i++) {
      PieSymbolInfo symbol;
      if (!ctx->api->find_symbol(ctx->sema, stmt->multi_names[i], &symbol)) {
        ctx->api->errorf(ctx->sema, "cannot assign undefined variable '%s'",
                         stmt->multi_names[i]);
        return PIE_SEMA_ERROR;
      }
      if (!symbol.is_mut) {
        ctx->api->errorf(
            ctx->sema,
            "cannot assign immutable variable '%s'; declare it with mut",
            stmt->multi_names[i]);
        return PIE_SEMA_ERROR;
      }

      PieType value_type;
      if (ctx->api->check_expr(ctx->sema, stmt->multi_exprs[i], &value_type) !=
          PIE_SEMA_OK) {
        return PIE_SEMA_ERROR;
      }
      if (!require_assignable_type(ctx, stmt->multi_names[i], symbol.type,
                                   value_type)) {
        return PIE_SEMA_ERROR;
      }
    }
    return PIE_SEMA_OK;
  }

  return PIE_SEMA_NO_MATCH;
}
