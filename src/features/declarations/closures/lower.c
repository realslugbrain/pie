#include "pie/core/lower/lower.h"

#include <stdlib.h>
#include <string.h>

static PieIrTypeKind ast_to_ir_type(PieAstTypeKind kind) {
  switch (kind) {
  case PIE_AST_TYPE_INT:
    return PIE_IR_TYPE_INT;
  case PIE_AST_TYPE_FLOAT:
    return PIE_IR_TYPE_FLOAT;
  case PIE_AST_TYPE_STRING:
    return PIE_IR_TYPE_STRING;
  case PIE_AST_TYPE_BOOL:
    return PIE_IR_TYPE_BOOL;
  case PIE_AST_TYPE_CHAR:
    return PIE_IR_TYPE_CHAR;
  case PIE_AST_TYPE_BYTE:
    return PIE_IR_TYPE_BYTE;
  case PIE_AST_TYPE_VOID:
    return PIE_IR_TYPE_VOID;
  case PIE_AST_TYPE_LIST:
    return PIE_IR_TYPE_LIST;
  case PIE_AST_TYPE_MAP:
    return PIE_IR_TYPE_MAP;
  case PIE_AST_TYPE_THREAD:
    return PIE_IR_TYPE_THREAD;
  case PIE_AST_TYPE_MUTEX:
    return PIE_IR_TYPE_MUTEX;
  case PIE_AST_TYPE_CHANNEL:
    return PIE_IR_TYPE_CHANNEL;
  default:
    return PIE_IR_TYPE_INT;
  }
}

PieLowerResult pie_feature_closures_lower_expr(PieLowerContext *ctx,
                                               const PieExpr *expr,
                                               PieIrExpr **out_expr) {
  if (expr->kind != PIE_EXPR_CLOSURE) {
    return PIE_LOWER_NO_MATCH;
  }

  *out_expr = pie_ir_expr_closure();
  if (!*out_expr) {
    ctx->api->error(ctx->lower,
                    "out of memory while lowering closure expression");
    return PIE_LOWER_ERROR;
  }

  (*out_expr)->closure_param_count = expr->closure_param_count;
  if (expr->closure_param_count > 0) {
    (*out_expr)->closure_param_names =
        (char **)calloc(expr->closure_param_count, sizeof(char *));
    (*out_expr)->closure_param_types = (PieIrTypeKind *)calloc(
        expr->closure_param_count, sizeof(PieIrTypeKind));
    for (size_t i = 0; i < expr->closure_param_count; i++) {
      size_t len = strlen(expr->closure_param_names[i]);
      (*out_expr)->closure_param_names[i] = (char *)malloc(len + 1);
      if ((*out_expr)->closure_param_names[i]) {
        memcpy((*out_expr)->closure_param_names[i],
               expr->closure_param_names[i], len + 1);
      }
      (*out_expr)->closure_param_types[i] =
          ast_to_ir_type(expr->closure_param_types[i].kind);
    }
  }

  (*out_expr)->closure_return_type =
      ast_to_ir_type(expr->closure_return_type.kind);

  (*out_expr)->closure_captured_count = expr->closure_capture_count;
  if (expr->closure_capture_count > 0) {
    (*out_expr)->closure_captured_names =
        (char **)calloc(expr->closure_capture_count, sizeof(char *));
    (*out_expr)->closure_capture_types = (PieIrTypeKind *)calloc(
        expr->closure_capture_count, sizeof(PieIrTypeKind));
    (*out_expr)->closure_capture_outer_ids =
        (size_t *)calloc(expr->closure_capture_count, sizeof(size_t));
    for (size_t i = 0; i < expr->closure_capture_count; i++) {
      size_t len = strlen(expr->closure_capture_names[i]);
      (*out_expr)->closure_captured_names[i] = (char *)malloc(len + 1);
      if ((*out_expr)->closure_captured_names[i]) {
        memcpy((*out_expr)->closure_captured_names[i],
               expr->closure_capture_names[i], len + 1);
      }
      (*out_expr)->closure_capture_types[i] =
          ast_to_ir_type(expr->closure_capture_types[i]);

      size_t outer_id = 0;
      int outer_is_mut = 0;
      PieIrTypeKind outer_type = PIE_IR_TYPE_UNKNOWN;
      int outer_type_width = 0;
      PieIrTypeKind outer_raw = PIE_IR_TYPE_UNKNOWN;
      int outer_raw_width = PIE_WIDTH_INFER;
      PieIrTypeKind outer_ref_inner = PIE_IR_TYPE_UNKNOWN;
      int outer_ref_inner_width = PIE_WIDTH_INFER;
      ctx->api->find_local(
          ctx->lower, expr->closure_capture_names[i], &outer_id, &outer_is_mut,
          &outer_type, &outer_type_width, &outer_raw, &outer_raw_width,
          &outer_ref_inner, &outer_ref_inner_width, NULL, NULL);
      (*out_expr)->closure_capture_outer_ids[i] = outer_id;
    }
  }

  if (expr->closure_body) {
    PieIrProgram *closure_ir = (PieIrProgram *)malloc(sizeof(PieIrProgram));
    if (!closure_ir) {
      ctx->api->error(ctx->lower, "out of memory while lowering closure body");
      return PIE_LOWER_ERROR;
    }
    pie_ir_program_init(closure_ir);

    PieIrProgram *outer_ir = ctx->api->current_ir(ctx->lower);
    PieIrProgram *outer_root = ctx->api->root_ir(ctx->lower);
    ctx->api->set_current_ir(ctx->lower, closure_ir);
    ctx->api->set_root_ir(ctx->lower, closure_ir);
    ctx->api->enter_scope(ctx->lower);

    for (size_t i = 0; i < expr->closure_param_count; i++) {
      size_t id;
      PieIrTypeKind param_type =
          ast_to_ir_type(expr->closure_param_types[i].kind);
      if (!ctx->api->declare_local(
              ctx->lower, expr->closure_param_names[i], 0, param_type,
              PIE_WIDTH_64, PIE_IR_TYPE_UNKNOWN, PIE_WIDTH_INFER,
              PIE_IR_TYPE_UNKNOWN, PIE_WIDTH_INFER, NULL, NULL, &id)) {
        ctx->api->error(ctx->lower,
                        "failed to declare closure parameter during lowering");
        ctx->api->set_current_ir(ctx->lower, outer_ir);
        ctx->api->set_root_ir(ctx->lower, outer_root);
        ctx->api->leave_scope(ctx->lower);
        return PIE_LOWER_ERROR;
      }
    }
    for (size_t i = 0; i < expr->closure_capture_count; i++) {
      size_t id;
      if (!ctx->api->declare_local(ctx->lower, expr->closure_capture_names[i],
                                   1, (*out_expr)->closure_capture_types[i],
                                   PIE_WIDTH_64, PIE_IR_TYPE_UNKNOWN,
                                   PIE_WIDTH_INFER, PIE_IR_TYPE_UNKNOWN,
                                   PIE_WIDTH_INFER, NULL, NULL, &id)) {
        ctx->api->error(ctx->lower,
                        "failed to declare capture during lowering");
        ctx->api->set_current_ir(ctx->lower, outer_ir);
        ctx->api->set_root_ir(ctx->lower, outer_root);
        ctx->api->leave_scope(ctx->lower);
        return PIE_LOWER_ERROR;
      }
    }

    const PieProgram *prev_body = ctx->api->current_body(ctx->lower);
    ctx->api->set_current_body(ctx->lower, expr->closure_body);

    for (size_t i = 0; i < expr->closure_body->stmt_count; i++) {
      if (ctx->api->lower_stmt(ctx->lower, &expr->closure_body->stmts[i]) !=
          PIE_LOWER_OK) {
        ctx->api->error(ctx->lower, "failed to lower closure body statement");
        ctx->api->set_current_ir(ctx->lower, outer_ir);
        ctx->api->set_root_ir(ctx->lower, outer_root);
        ctx->api->set_current_body(ctx->lower, prev_body);
        ctx->api->leave_scope(ctx->lower);
        return PIE_LOWER_ERROR;
      }
    }

    (*out_expr)->closure_body = closure_ir;
    ctx->api->set_current_ir(ctx->lower, outer_ir);
    ctx->api->set_root_ir(ctx->lower, outer_root);
    ctx->api->set_current_body(ctx->lower, prev_body);
    ctx->api->leave_scope(ctx->lower);
  }

  return PIE_LOWER_OK;
}
