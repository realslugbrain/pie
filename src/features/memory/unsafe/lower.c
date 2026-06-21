#include "pie/core/lower/lower.h"

#include <stdlib.h>
#include <string.h>

static void free_ir_body(PieIrProgram *program) {
  if (!program) {
    return;
  }
  pie_ir_program_free(program);
  free(program);
}

PieLowerResult pie_feature_unsafe_lower_stmt(PieLowerContext *ctx,
                                             const PieStmt *stmt) {
  if (stmt->kind == PIE_STMT_UNSAFE) {
    PieIrProgram *body = (PieIrProgram *)malloc(sizeof(PieIrProgram));
    if (!body) {
      ctx->api->error(ctx->lower, "out of memory while lowering unsafe body");
      return PIE_LOWER_ERROR;
    }
    if (!ctx->api->lower_block(ctx->lower, stmt->then_branch, body)) {
      free_ir_body(body);
      return PIE_LOWER_ERROR;
    }

    PieIrStmt ir_stmt;
    memset(&ir_stmt, 0, sizeof(ir_stmt));
    ir_stmt.kind = PIE_IR_STMT_UNSAFE;
    ir_stmt.then_branch = body;
    if (!ctx->api->push_stmt(ctx->lower, ir_stmt)) {
      free_ir_body(body);
      return PIE_LOWER_ERROR;
    }
    return PIE_LOWER_OK;
  }

  if (stmt->kind != PIE_STMT_RAW_STORE) {
    return PIE_LOWER_NO_MATCH;
  }

  PieIrExpr *target = NULL;
  if (ctx->api->lower_expr(ctx->lower, stmt->target, &target) != PIE_LOWER_OK) {
    return PIE_LOWER_ERROR;
  }

  PieIrExpr *value = NULL;
  if (ctx->api->lower_expr(ctx->lower, stmt->expr, &value) != PIE_LOWER_OK) {
    pie_ir_expr_free(target);
    return PIE_LOWER_ERROR;
  }
  value->type = target->raw_pointee_type;
  value->type_width = target->raw_pointee_width;

  PieIrStmt ir_stmt;
  memset(&ir_stmt, 0, sizeof(ir_stmt));
  ir_stmt.kind = PIE_IR_STMT_RAW_STORE;
  ir_stmt.target = target;
  ir_stmt.expr = value;
  if (!ctx->api->push_stmt(ctx->lower, ir_stmt)) {
    pie_ir_expr_free(target);
    pie_ir_expr_free(value);
    return PIE_LOWER_ERROR;
  }
  return PIE_LOWER_OK;
}

PieLowerResult pie_feature_unsafe_lower_expr(PieLowerContext *ctx,
                                             const PieExpr *expr,
                                             PieIrExpr **out_expr) {
  if (expr->kind == PIE_EXPR_BINARY && (expr->op == '+' || expr->op == '-')) {
    PieIrExpr *left = NULL;
    PieIrExpr *right = NULL;
    if (ctx->api->lower_expr(ctx->lower, expr->left, &left) != PIE_LOWER_OK) {
      return PIE_LOWER_ERROR;
    }
    if (ctx->api->lower_expr(ctx->lower, expr->right, &right) != PIE_LOWER_OK) {
      pie_ir_expr_free(left);
      return PIE_LOWER_ERROR;
    }

    PieIrExpr *ptr = left->type == PIE_IR_TYPE_RAW_PTR ? left : right;
    if (ptr->type != PIE_IR_TYPE_RAW_PTR) {
      pie_ir_expr_free(left);
      pie_ir_expr_free(right);
      return PIE_LOWER_NO_MATCH;
    }

    *out_expr = pie_ir_expr_binary_typed(expr->op_text, left, right,
                                         PIE_IR_TYPE_RAW_PTR);
    if (!*out_expr) {
      pie_ir_expr_free(left);
      pie_ir_expr_free(right);
      ctx->api->error(ctx->lower,
                      "out of memory while lowering raw pointer arithmetic");
      return PIE_LOWER_ERROR;
    }
    (*out_expr)->type_width = PIE_WIDTH_64;
    (*out_expr)->raw_pointee_type = ptr->raw_pointee_type;
    (*out_expr)->raw_pointee_width = ptr->raw_pointee_width;
    return PIE_LOWER_OK;
  }

  if (expr->kind != PIE_EXPR_UNARY || (strcmp(expr->op_text, "&raw") != 0 &&
                                       strcmp(expr->op_text, "*raw") != 0)) {
    return PIE_LOWER_NO_MATCH;
  }

  PieIrExpr *inner = NULL;
  if (ctx->api->lower_expr(ctx->lower, expr->right, &inner) != PIE_LOWER_OK) {
    return PIE_LOWER_ERROR;
  }

  int is_raw_addr = strcmp(expr->op_text, "&raw") == 0;
  PieIrTypeKind result_type =
      is_raw_addr ? PIE_IR_TYPE_RAW_PTR : inner->raw_pointee_type;
  if (!is_raw_addr && result_type == PIE_IR_TYPE_UNKNOWN) {
    result_type = PIE_IR_TYPE_INT;
  }
  *out_expr = pie_ir_expr_unary_typed(expr->op_text, inner, result_type);
  if (!*out_expr) {
    pie_ir_expr_free(inner);
    ctx->api->error(ctx->lower,
                    "out of memory while lowering raw pointer expression");
    return PIE_LOWER_ERROR;
  }
  if (is_raw_addr) {
    (*out_expr)->type_width = PIE_WIDTH_64;
    (*out_expr)->raw_pointee_type = (*out_expr)->right->type;
    (*out_expr)->raw_pointee_width = (*out_expr)->right->type_width;
  } else {
    (*out_expr)->type_width = (*out_expr)->right->raw_pointee_width;
    if ((*out_expr)->type_width == PIE_WIDTH_INFER &&
        ((*out_expr)->type == PIE_IR_TYPE_INT ||
         (*out_expr)->type == PIE_IR_TYPE_FLOAT)) {
      (*out_expr)->type_width = PIE_WIDTH_64;
    }
  }
  return PIE_LOWER_OK;
}
