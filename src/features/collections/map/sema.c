#include "pie/core/sema/sema.h"

#include <string.h>

PieSemaResult pie_feature_map_sema_expr(PieSemaContext *ctx,
                                        const PieExpr *expr,
                                        PieType *out_type) {
  if (expr->kind == PIE_EXPR_MAP) {
    memset(out_type, 0, sizeof(*out_type));

    if (expr->map_entry_count == 0) {
      out_type->kind = PIE_TYPE_MAP;
      out_type->map_key_kind = PIE_TYPE_STRING;
      out_type->map_key_width = 0;
      out_type->map_value_kind = PIE_TYPE_INT;
      out_type->map_value_width = PIE_WIDTH_64;
      return PIE_SEMA_OK;
    }

    PieType key_type;
    if (ctx->api->check_expr(ctx->sema, expr->map_keys[0], &key_type) !=
        PIE_SEMA_OK) {
      return PIE_SEMA_ERROR;
    }
    PieType value_type;
    if (ctx->api->check_expr(ctx->sema, expr->map_values[0], &value_type) !=
        PIE_SEMA_OK) {
      return PIE_SEMA_ERROR;
    }

    out_type->kind = PIE_TYPE_MAP;
    out_type->map_key_kind = key_type.kind;
    out_type->map_key_width = key_type.type_width;
    out_type->map_value_kind = value_type.kind;
    out_type->map_value_width = value_type.type_width;

    for (size_t i = 1; i < expr->map_entry_count; i++) {
      PieType k;
      if (ctx->api->check_expr(ctx->sema, expr->map_keys[i], &k) !=
          PIE_SEMA_OK) {
        return PIE_SEMA_ERROR;
      }
      if (k.kind != out_type->map_key_kind) {
        ctx->api->errorf(ctx->sema, "map key %zu has type %s, expected %s", i,
                         ctx->api->type_name(k),
                         ctx->api->type_name(*out_type));
        return PIE_SEMA_ERROR;
      }
      PieType v;
      if (ctx->api->check_expr(ctx->sema, expr->map_values[i], &v) !=
          PIE_SEMA_OK) {
        return PIE_SEMA_ERROR;
      }
      if (v.kind != out_type->map_value_kind) {
        ctx->api->errorf(ctx->sema, "map value %zu has type %s, expected %s", i,
                         ctx->api->type_name(v),
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
    if (obj_type.kind != PIE_TYPE_MAP) {
      return PIE_SEMA_NO_MATCH;
    }

    PieType idx_type;
    if (ctx->api->check_expr(ctx->sema, expr->index_expr, &idx_type) !=
        PIE_SEMA_OK) {
      return PIE_SEMA_ERROR;
    }
    if (idx_type.kind != PIE_TYPE_STRING) {
      ctx->api->errorf(ctx->sema, "map index must be string, got %s",
                       ctx->api->type_name(idx_type));
      return PIE_SEMA_ERROR;
    }

    out_type->kind = obj_type.map_value_kind;
    out_type->type_width = obj_type.map_value_width;
    return PIE_SEMA_OK;
  }

  return PIE_SEMA_NO_MATCH;
}

PieSemaResult pie_feature_map_sema_index_assign(PieSemaContext *ctx,
                                                const PieStmt *stmt) {
  if (stmt->kind != PIE_STMT_INDEX_ASSIGN) {
    return PIE_SEMA_NO_MATCH;
  }

  PieType obj_type;
  if (ctx->api->check_expr(ctx->sema, stmt->index_target, &obj_type) !=
      PIE_SEMA_OK) {
    return PIE_SEMA_ERROR;
  }
  if (obj_type.kind != PIE_TYPE_MAP) {
    return PIE_SEMA_NO_MATCH;
  }

  PieType idx_type;
  if (ctx->api->check_expr(ctx->sema, stmt->index_expr, &idx_type) !=
      PIE_SEMA_OK) {
    return PIE_SEMA_ERROR;
  }
  if (idx_type.kind != PIE_TYPE_STRING) {
    ctx->api->errorf(ctx->sema, "map index must be string, got %s",
                     ctx->api->type_name(idx_type));
    return PIE_SEMA_ERROR;
  }

  PieType val_type;
  if (ctx->api->check_expr(ctx->sema, stmt->expr, &val_type) != PIE_SEMA_OK) {
    return PIE_SEMA_ERROR;
  }

  return PIE_SEMA_OK;
}
