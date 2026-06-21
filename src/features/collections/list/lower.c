#include "pie/core/lower/lower.h"

#include <string.h>

PieLowerResult pie_feature_list_lower_expr(PieLowerContext *ctx,
                                           const PieExpr *expr,
                                           PieIrExpr **out_expr) {
  if (expr->kind == PIE_EXPR_LIST) {
    *out_expr = pie_ir_expr_list(0);
    if (!*out_expr) {
      ctx->api->error(ctx->lower, "out of memory while lowering list");
      return PIE_LOWER_ERROR;
    }

    for (size_t i = 0; i < expr->list_element_count; i++) {
      PieIrExpr *elem = NULL;
      if (ctx->api->lower_expr(ctx->lower, expr->list_elements[i], &elem) !=
          PIE_LOWER_OK) {
        pie_ir_expr_free(*out_expr);
        *out_expr = NULL;
        return PIE_LOWER_ERROR;
      }
      if (!pie_ir_expr_list_add_element(*out_expr, elem)) {
        pie_ir_expr_free(elem);
        pie_ir_expr_free(*out_expr);
        *out_expr = NULL;
        ctx->api->error(ctx->lower,
                        "out of memory while lowering list element");
        return PIE_LOWER_ERROR;
      }
      (*out_expr)->list_element_type = elem->type;
      (*out_expr)->list_element_width = elem->type_width;
    }

    return PIE_LOWER_OK;
  }

  if (expr->kind == PIE_EXPR_INDEX) {
    PieIrExpr *obj = NULL;
    if (ctx->api->lower_expr(ctx->lower, expr->index_object, &obj) !=
        PIE_LOWER_OK) {
      return PIE_LOWER_ERROR;
    }
    if (obj->type != PIE_IR_TYPE_LIST && obj->type != PIE_IR_TYPE_STRING) {
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
                      "out of memory while lowering index expression");
      return PIE_LOWER_ERROR;
    }
    if (obj->type == PIE_IR_TYPE_STRING) {
      (*out_expr)->type = PIE_IR_TYPE_CHAR;
      (*out_expr)->type_width = PIE_WIDTH_8;
    }
    return PIE_LOWER_OK;
  }

  return PIE_LOWER_NO_MATCH;
}

PieLowerResult pie_feature_list_lower_index_assign(PieLowerContext *ctx,
                                                   const PieStmt *stmt) {
  if (stmt->kind != PIE_STMT_INDEX_ASSIGN) {
    return PIE_LOWER_NO_MATCH;
  }

  PieIrExpr *obj = NULL;
  if (ctx->api->lower_expr(ctx->lower, stmt->index_target, &obj) !=
      PIE_LOWER_OK) {
    return PIE_LOWER_ERROR;
  }
  if (obj->type != PIE_IR_TYPE_LIST) {
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
    ctx->api->error(ctx->lower, "out of memory while lowering index assign");
    return PIE_LOWER_ERROR;
  }
  return PIE_LOWER_OK;
}
