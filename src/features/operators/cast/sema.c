#include "pie/core/sema/sema.h"

#include <stdlib.h>
#include <string.h>

static char *cast_sema_strdup(const char *s) {
  if (!s)
    return NULL;
  size_t len = strlen(s);
  char *copy = (char *)malloc(len + 1);
  if (copy) {
    memcpy(copy, s, len + 1);
  }
  return copy;
}

static PieTypeKind sema_type_from_ast(PieAstTypeKind kind) {
  switch (kind) {
  case PIE_AST_TYPE_INT:
    return PIE_TYPE_INT;
  case PIE_AST_TYPE_FLOAT:
    return PIE_TYPE_FLOAT;
  case PIE_AST_TYPE_STRING:
    return PIE_TYPE_STRING;
  case PIE_AST_TYPE_BOOL:
    return PIE_TYPE_BOOL;
  case PIE_AST_TYPE_CHAR:
    return PIE_TYPE_CHAR;
  case PIE_AST_TYPE_BYTE:
    return PIE_TYPE_BYTE;
  case PIE_AST_TYPE_VOID:
    return PIE_TYPE_VOID;
  default:
    return PIE_TYPE_ERROR;
  }
}

static int is_numeric_type(PieTypeKind kind) {
  return kind == PIE_TYPE_INT || kind == PIE_TYPE_FLOAT ||
         kind == PIE_TYPE_CHAR || kind == PIE_TYPE_BYTE;
}

PieSemaResult pie_feature_cast_sema_expr(PieSemaContext *ctx,
                                         const PieExpr *expr,
                                         PieType *out_type) {
  if (expr->kind != PIE_EXPR_CAST) {
    return PIE_SEMA_NO_MATCH;
  }

  PieType inner_type;
  if (ctx->api->check_expr(ctx->sema, expr->cast_inner, &inner_type) !=
      PIE_SEMA_OK) {
    return PIE_SEMA_ERROR;
  }

  PieTypeKind target = sema_type_from_ast(expr->cast_target_kind);

  if (inner_type.kind == target) {
    out_type->kind = target;
    return PIE_SEMA_OK;
  }

  if (is_numeric_type(inner_type.kind) && is_numeric_type(target)) {
    out_type->kind = target;
    return PIE_SEMA_OK;
  }

  if (inner_type.kind == PIE_TYPE_STRING &&
      (target == PIE_TYPE_INT || target == PIE_TYPE_FLOAT)) {
    out_type->kind = target;
    return PIE_SEMA_OK;
  }

  if ((inner_type.kind == PIE_TYPE_INT || inner_type.kind == PIE_TYPE_FLOAT) &&
      target == PIE_TYPE_STRING) {
    out_type->kind = PIE_TYPE_STRING;
    return PIE_SEMA_OK;
  }

  int is_enum = (inner_type.kind == PIE_TYPE_ENUM);
  const char *enum_name = NULL;
  if (inner_type.kind == PIE_TYPE_ENUM) {
    enum_name = inner_type.enum_name;
  } else if (inner_type.kind == PIE_TYPE_STRUCT && inner_type.struct_name) {
    if (ctx->api->find_enum(ctx->sema, inner_type.struct_name)) {
      is_enum = 1;
      enum_name = inner_type.struct_name;
    }
  }

  if (is_enum && target == PIE_TYPE_STRING) {
    out_type->kind = PIE_TYPE_STRING;
    out_type->enum_name = cast_sema_strdup(enum_name);
    return PIE_SEMA_OK;
  }

  ctx->api->errorf(ctx->sema, "cannot cast from %s to %s",
                   ctx->api->type_name(inner_type),
                   ctx->api->type_name(*out_type));
  out_type->kind = PIE_TYPE_ERROR;
  return PIE_SEMA_ERROR;
}