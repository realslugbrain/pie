#include "pie/core/lower/lower.h"
#include <stdio.h>

static int is_incdec(const PieExpr *expr) {
  if (expr->kind == PIE_EXPR_UNARY) {
    return (expr->op_text[0] == '+' || expr->op_text[0] == '-') &&
           (expr->op_text[1] == '+' || expr->op_text[1] == '-');
  }
  if (expr->kind == PIE_EXPR_POSTFIX) {
    return (expr->op_text[0] == '+' || expr->op_text[0] == '-') &&
           (expr->op_text[1] == '+' || expr->op_text[1] == '-');
  }
  return 0;
}

static int is_prefix(const PieExpr *expr) {
  return expr->kind == PIE_EXPR_UNARY;
}

static int is_increment(const PieExpr *expr) { return expr->op_text[0] == '+'; }

PieLowerResult pie_feature_incdec_lower_expr(PieLowerContext *ctx,
                                             const PieExpr *expr,
                                             PieIrExpr **out_expr) {
  if (!is_incdec(expr)) {
    return PIE_LOWER_NO_MATCH;
  }

  int prefix = is_prefix(expr);
  int increment = is_increment(expr);
  const PieExpr *inner = expr->right;

  PieIrExpr *target = NULL;
  if (ctx->api->lower_expr(ctx->lower, inner, &target) != PIE_LOWER_OK) {
    return PIE_LOWER_ERROR;
  }

  PieIrTypeKind type = target->type;
  int width = target->type_width;

  PieIrExpr *one = NULL;
  if (type == PIE_IR_TYPE_FLOAT) {
    one = pie_ir_expr_float(1.0);
  } else {
    one = pie_ir_expr_int(1);
  }
  if (!one) {
    pie_ir_expr_free(target);
    ctx->api->error(ctx->lower, "out of memory while building inc/dec literal");
    return PIE_LOWER_ERROR;
  }

  PieIrExpr *inc_expr =
      pie_ir_expr_binary_typed(increment ? "+" : "-", target, one, type);
  if (!inc_expr) {
    pie_ir_expr_free(target);
    pie_ir_expr_free(one);
    ctx->api->error(ctx->lower,
                    "out of memory while building inc/dec expression");
    return PIE_LOWER_ERROR;
  }

  if (prefix) {

    PieIrExpr *target_copy = NULL;
    if (ctx->api->lower_expr(ctx->lower, inner, &target_copy) != PIE_LOWER_OK) {
      pie_ir_expr_free(inc_expr);
      return PIE_LOWER_ERROR;
    }

    PieIrStmt assign_stmt;
    memset(&assign_stmt, 0, sizeof(assign_stmt));
    assign_stmt.kind = PIE_IR_STMT_ASSIGN;
    PieIrExpr *target_for_store = NULL;
    if (ctx->api->lower_expr(ctx->lower, inner, &target_for_store) !=
        PIE_LOWER_OK) {
      pie_ir_expr_free(target_copy);
      pie_ir_expr_free(inc_expr);
      return PIE_LOWER_ERROR;
    }
    assign_stmt.target = target_for_store;
    assign_stmt.expr = inc_expr;
    strncpy(assign_stmt.assign_op, "<-", sizeof(assign_stmt.assign_op) - 1);
    if (!ctx->api->push_stmt(ctx->lower, assign_stmt)) {
      pie_ir_expr_free(target_for_store);
      pie_ir_expr_free(inc_expr);
      return PIE_LOWER_ERROR;
    }

    *out_expr = target_copy;
    return PIE_LOWER_OK;
  }

  char tmp_name[64];
  static int tmp_counter = 0;
  snprintf(tmp_name, sizeof(tmp_name), "__tmp_incdec_%d", tmp_counter++);

  size_t tmp_id = 0;
  if (!ctx->api->declare_local(ctx->lower, tmp_name, 1, type, width,
                               PIE_IR_TYPE_UNKNOWN, PIE_WIDTH_INFER,
                               PIE_IR_TYPE_UNKNOWN, PIE_WIDTH_INFER, NULL, NULL,
                               &tmp_id)) {
    pie_ir_expr_free(inc_expr);
    ctx->api->error(ctx->lower,
                    "out of memory while declaring temp for inc/dec");
    return PIE_LOWER_ERROR;
  }

  PieIrExpr *target_for_temp = NULL;
  if (ctx->api->lower_expr(ctx->lower, inner, &target_for_temp) !=
      PIE_LOWER_OK) {
    pie_ir_expr_free(inc_expr);
    return PIE_LOWER_ERROR;
  }

  PieIrStmt temp_assign;
  memset(&temp_assign, 0, sizeof(temp_assign));
  temp_assign.kind = PIE_IR_STMT_ASSIGN;
  temp_assign.local_id = tmp_id;
  temp_assign.is_mut = 1;
  temp_assign.expr = target_for_temp;
  strncpy(temp_assign.assign_op, "<-", sizeof(temp_assign.assign_op) - 1);
  if (!ctx->api->push_stmt(ctx->lower, temp_assign)) {
    pie_ir_expr_free(target_for_temp);
    pie_ir_expr_free(inc_expr);
    return PIE_LOWER_ERROR;
  }

  PieIrStmt inc_assign;
  memset(&inc_assign, 0, sizeof(inc_assign));
  inc_assign.kind = PIE_IR_STMT_ASSIGN;
  PieIrExpr *target_for_inc = NULL;
  if (ctx->api->lower_expr(ctx->lower, inner, &target_for_inc) !=
      PIE_LOWER_OK) {
    return PIE_LOWER_ERROR;
  }
  inc_assign.target = target_for_inc;
  inc_assign.expr = inc_expr;
  strncpy(inc_assign.assign_op, "<-", sizeof(inc_assign.assign_op) - 1);
  if (!ctx->api->push_stmt(ctx->lower, inc_assign)) {
    pie_ir_expr_free(target_for_inc);
    pie_ir_expr_free(inc_expr);
    return PIE_LOWER_ERROR;
  }

  *out_expr = pie_ir_expr_raw_local(tmp_id, type, width, PIE_IR_TYPE_UNKNOWN,
                                    PIE_WIDTH_INFER);
  if (!*out_expr) {
    ctx->api->error(ctx->lower,
                    "out of memory while building inc/dec temp reference");
    return PIE_LOWER_ERROR;
  }
  return PIE_LOWER_OK;
}
