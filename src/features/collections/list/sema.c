#include "pie/core/sema/sema.h"

#include <string.h>

PieSemaResult pie_feature_list_sema_expr(PieSemaContext *ctx,
                                         const PieExpr *expr,
                                         PieType *out_type) {
  if (expr->kind == PIE_EXPR_LIST) {
    memset(out_type, 0, sizeof(*out_type));

    if (expr->list_element_count == 0) {
      out_type->kind = PIE_TYPE_LIST;
      out_type->list_element_kind = PIE_TYPE_INT;
      out_type->list_element_width = PIE_WIDTH_64;
      return PIE_SEMA_OK;
    }

    PieType elem_type;
    if (ctx->api->check_expr(ctx->sema, expr->list_elements[0], &elem_type) !=
        PIE_SEMA_OK) {
      return PIE_SEMA_ERROR;
    }

    out_type->kind = PIE_TYPE_LIST;
    out_type->list_element_kind = elem_type.kind;
    out_type->list_element_width = elem_type.type_width;

    for (size_t i = 1; i < expr->list_element_count; i++) {
      PieType current;
      if (ctx->api->check_expr(ctx->sema, expr->list_elements[i], &current) !=
          PIE_SEMA_OK) {
        return PIE_SEMA_ERROR;
      }
      if (current.kind != out_type->list_element_kind) {
        ctx->api->errorf(ctx->sema, "list element %zu has type %s, expected %s",
                         i, ctx->api->type_name(current),
                         ctx->api->type_name(*out_type));
        return PIE_SEMA_ERROR;
      }
    }

    return PIE_SEMA_OK;
  }

  if (expr->kind == PIE_EXPR_INDEX) {
    PieType obj_type;
    if (ctx->api->check_expr(ctx->sema, expr->index_object, &obj_type) !=
        PIE_SEMA_OK) {
      return PIE_SEMA_ERROR;
    }
    PieType idx_type;
    if (ctx->api->check_expr(ctx->sema, expr->index_expr, &idx_type) !=
        PIE_SEMA_OK) {
      return PIE_SEMA_ERROR;
    }
    if (obj_type.kind != PIE_TYPE_LIST && obj_type.kind != PIE_TYPE_STRING) {
      return PIE_SEMA_NO_MATCH;
    }
    if (idx_type.kind != PIE_TYPE_INT) {
      ctx->api->errorf(ctx->sema, "index must be int, got %s",
                       ctx->api->type_name(idx_type));
      return PIE_SEMA_ERROR;
    }
    if (obj_type.kind == PIE_TYPE_STRING) {
      out_type->kind = PIE_TYPE_CHAR;
      out_type->type_width = PIE_WIDTH_8;
      return PIE_SEMA_OK;
    }
    out_type->kind = obj_type.list_element_kind;
    out_type->type_width = obj_type.list_element_width;
    return PIE_SEMA_OK;
  }

  return PIE_SEMA_NO_MATCH;
}

PieSemaResult pie_feature_list_sema_index_assign(PieSemaContext *ctx,
                                                 const PieStmt *stmt) {
  if (stmt->kind != PIE_STMT_INDEX_ASSIGN) {
    return PIE_SEMA_NO_MATCH;
  }

  PieType obj_type;
  if (ctx->api->check_expr(ctx->sema, stmt->index_target, &obj_type) !=
      PIE_SEMA_OK) {
    return PIE_SEMA_ERROR;
  }
  if (obj_type.kind != PIE_TYPE_LIST) {
    return PIE_SEMA_NO_MATCH;
  }

  PieType idx_type;
  if (ctx->api->check_expr(ctx->sema, stmt->index_expr, &idx_type) !=
      PIE_SEMA_OK) {
    return PIE_SEMA_ERROR;
  }
  if (idx_type.kind != PIE_TYPE_INT) {
    ctx->api->errorf(ctx->sema, "list index must be int, got %s",
                     ctx->api->type_name(idx_type));
    return PIE_SEMA_ERROR;
  }

  PieType val_type;
  if (ctx->api->check_expr(ctx->sema, stmt->expr, &val_type) != PIE_SEMA_OK) {
    return PIE_SEMA_ERROR;
  }

  return PIE_SEMA_OK;
}
