#include "pie/core/sema/sema.h"
#include "pie/core/ast/ast.h"
#include <stdlib.h>

static PieType type_from_annotation(PieAstType annotation) {
  PieType type;
  memset(&type, 0, sizeof(type));
  switch (annotation.kind) {
  case PIE_AST_TYPE_INT:
    type.kind = PIE_TYPE_INT;
    type.type_width = annotation.width;
    return type;
  case PIE_AST_TYPE_FLOAT:
    type.kind = PIE_TYPE_FLOAT;
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
  case PIE_AST_TYPE_CHAR:
    type.kind = PIE_TYPE_CHAR;
    type.type_width = annotation.width;
    return type;
  case PIE_AST_TYPE_BYTE:
    type.kind = PIE_TYPE_BYTE;
    type.type_width = annotation.width;
    return type;
  default:
    return type;
  }
}

PieSemaResult pie_feature_const_sema_stmt(PieSemaContext *ctx,
                                          const PieStmt *stmt) {
  if (stmt->kind != PIE_STMT_CONST) {
    return PIE_SEMA_NO_MATCH;
  }

  PieType init_type;
  if (ctx->api->check_expr(ctx->sema, stmt->const_def->value, &init_type) !=
      PIE_SEMA_OK) {
    return PIE_SEMA_ERROR;
  }

  PieType declared_type = type_from_annotation(stmt->const_def->type);
  if (declared_type.kind == PIE_TYPE_ERROR) {
    declared_type = init_type;
  }

  if (declared_type.kind != PIE_TYPE_ERROR &&
      init_type.kind != PIE_TYPE_ERROR &&
      declared_type.kind != init_type.kind) {
    ctx->api->errorf(
        ctx->sema, "type mismatch in const '%s': expected type %d, got type %d",
        stmt->const_def->name, declared_type.kind, init_type.kind);
    return PIE_SEMA_ERROR;
  }

  if (!ctx->api->declare_symbol(ctx->sema, stmt->const_def->name, declared_type,
                                0)) {
    return PIE_SEMA_ERROR;
  }

  return PIE_SEMA_OK;
}
