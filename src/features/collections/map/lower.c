#include "pie/core/lower/lower.h"

PieLowerResult pie_feature_map_lower_expr(PieLowerContext *ctx,
                                          const PieExpr *expr,
                                          PieIrExpr **out_expr) {
  if (expr->kind == PIE_EXPR_MAP) {
    *out_expr = pie_ir_expr_map();
    if (!*out_expr) {
      ctx->api->error(ctx->lower, "out of memory while lowering map literal");
      return PIE_LOWER_ERROR;
    }

    for (size_t i = 0; i < expr->map_entry_count; i++) {
      PieIrExpr *key = NULL;
      if (ctx->api->lower_expr(ctx->lower, expr->map_keys[i], &key) !=
          PIE_LOWER_OK) {
        return PIE_LOWER_ERROR;
      }
      PieIrExpr *value = NULL;
      if (ctx->api->lower_expr(ctx->lower, expr->map_values[i], &value) !=
          PIE_LOWER_OK) {
        return PIE_LOWER_ERROR;
      }
      if (!pie_ir_expr_map_add(*out_expr, key, value)) {
        ctx->api->error(ctx->lower, "out of memory while adding map entry");
        return PIE_LOWER_ERROR;
      }
    }

    return PIE_LOWER_OK;
  }

  if (expr->kind == PIE_EXPR_INDEX) {
    PieIrExpr *obj = NULL;
    if (ctx->api->lower_expr(ctx->lower, expr->index_object, &obj) !=
        PIE_LOWER_OK) {
      return PIE_LOWER_ERROR;
    }
    if (obj->type != PIE_IR_TYPE_MAP) {
      pie_ir_expr_free(obj);
      return PIE_LOWER_NO_MATCH;
    }
    PieIrExpr *idx = NULL;
    if (ctx->api->lower_expr(ctx->lower, expr->index_expr, &idx) !=
        PIE_LOWER_OK) {
      pie_ir_expr_free(obj);
      return PIE_LOWER_ERROR;
    }
    *out_expr = pie_ir_expr_index(obj, idx);
    if (!*out_expr) {
      pie_ir_expr_free(obj);
      pie_ir_expr_free(idx);
      ctx->api->error(ctx->lower,
                      "out of memory while lowering map index expression");
      return PIE_LOWER_ERROR;
    }
    return PIE_LOWER_OK;
  }

  return PIE_LOWER_NO_MATCH;
}

PieLowerResult pie_feature_map_lower_index_assign(PieLowerContext *ctx,
                                                  const PieStmt *stmt) {
  if (stmt->kind != PIE_STMT_INDEX_ASSIGN) {
    return PIE_LOWER_NO_MATCH;
  }

  PieIrExpr *obj = NULL;
  if (ctx->api->lower_expr(ctx->lower, stmt->index_target, &obj) !=
      PIE_LOWER_OK) {
    return PIE_LOWER_ERROR;
  }
  if (obj->type != PIE_IR_TYPE_MAP) {
    pie_ir_expr_free(obj);
    return PIE_LOWER_NO_MATCH;
  }

  PieIrExpr *idx = NULL;
  if (ctx->api->lower_expr(ctx->lower, stmt->index_expr, &idx) !=
      PIE_LOWER_OK) {
    pie_ir_expr_free(obj);
    return PIE_LOWER_ERROR;
  }
  PieIrExpr *val = NULL;
  if (ctx->api->lower_expr(ctx->lower, stmt->expr, &val) != PIE_LOWER_OK) {
    pie_ir_expr_free(obj);
    pie_ir_expr_free(idx);
    return PIE_LOWER_ERROR;
  }

  PieIrStmt ir_stmt;
  memset(&ir_stmt, 0, sizeof(ir_stmt));
  ir_stmt.kind = PIE_IR_STMT_INDEX_ASSIGN;
  ir_stmt.index_target = obj;
  ir_stmt.index_expr = idx;
  ir_stmt.expr = val;
  strncpy(ir_stmt.assign_op, stmt->assign_op, sizeof(ir_stmt.assign_op) - 1);
  if (!ctx->api->push_stmt(ctx->lower, ir_stmt)) {
    pie_ir_expr_free(obj);
    pie_ir_expr_free(idx);
    pie_ir_expr_free(val);
    ctx->api->error(ctx->lower,
                    "out of memory while lowering map index assign");
    return PIE_LOWER_ERROR;
  }
  return PIE_LOWER_OK;
}
