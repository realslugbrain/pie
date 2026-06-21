#include "pie/core/lower/lower.h"

#include <stdlib.h>
#include <string.h>

static int needs_mut_to_shared_string_ref(PieIrTypeKind expected,
                                          PieIrTypeKind actual) {
  return expected == PIE_IR_TYPE_REF && actual == PIE_IR_TYPE_REF_MUT;
}

static int coerce_if_needed(PieLowerContext *ctx, PieIrTypeKind expected,
                            PieIrExpr **in_out_expr) {
  if (!in_out_expr || !*in_out_expr) {
    return 1;
  }
  if (!needs_mut_to_shared_string_ref(expected, (*in_out_expr)->type)) {
    return 1;
  }

  PieIrExpr *coerced =
      pie_ir_expr_unary_typed("refview", *in_out_expr, PIE_IR_TYPE_REF);
  if (!coerced) {
    ctx->api->error(ctx->lower,
                    "out of memory while lowering reference coercion");
    return 0;
  }
  coerced->ref_inner_type = (*in_out_expr)->ref_inner_type;
  coerced->ref_inner_width = (*in_out_expr)->ref_inner_width;
  *in_out_expr = coerced;
  return 1;
}

PieLowerResult pie_feature_functions_lower_stmt(PieLowerContext *ctx,
                                                const PieStmt *stmt) {
  if (stmt->kind == PIE_STMT_EXPR) {
    PieIrStmt ir_stmt;
    memset(&ir_stmt, 0, sizeof(ir_stmt));
    ir_stmt.kind = PIE_IR_STMT_EXPR;
    if (ctx->api->lower_expr(ctx->lower, stmt->expr, &ir_stmt.expr) !=
        PIE_LOWER_OK) {
      return PIE_LOWER_ERROR;
    }
    if (!ctx->api->push_stmt(ctx->lower, ir_stmt)) {
      pie_ir_expr_free(ir_stmt.expr);
      return PIE_LOWER_ERROR;
    }
    return PIE_LOWER_OK;
  }

  if (stmt->kind != PIE_STMT_RETURN) {
    return PIE_LOWER_NO_MATCH;
  }

  PieIrStmt ir_stmt;
  memset(&ir_stmt, 0, sizeof(ir_stmt));
  ir_stmt.kind = PIE_IR_STMT_RETURN;
  if (stmt->expr) {
    if (ctx->api->lower_expr(ctx->lower, stmt->expr, &ir_stmt.expr) !=
        PIE_LOWER_OK) {
      return PIE_LOWER_ERROR;
    }
    if (!coerce_if_needed(ctx, ctx->api->current_return_type(ctx->lower),
                          &ir_stmt.expr)) {
      pie_ir_expr_free(ir_stmt.expr);
      return PIE_LOWER_ERROR;
    }
  }

  if (!ctx->api->push_stmt(ctx->lower, ir_stmt)) {
    pie_ir_expr_free(ir_stmt.expr);
    return PIE_LOWER_ERROR;
  }
  return PIE_LOWER_OK;
}

PieLowerResult pie_feature_functions_lower_expr(PieLowerContext *ctx,
                                                const PieExpr *expr,
                                                PieIrExpr **out_expr) {
  if (expr->kind != PIE_EXPR_CALL) {
    return PIE_LOWER_NO_MATCH;
  }

  if (strcmp(expr->call_name, "maybe") == 0) {
    if (expr->call_arg_count != 0) {
      ctx->api->errorf(ctx->lower,
                       "builtin 'maybe' expects 0 argument(s), got %zu",
                       expr->call_arg_count);
      return PIE_LOWER_ERROR;
    }
    *out_expr = pie_ir_expr_maybe();
    if (!*out_expr) {
      ctx->api->error(ctx->lower, "out of memory while lowering maybe builtin");
      return PIE_LOWER_ERROR;
    }
    return PIE_LOWER_OK;
  }

  if (strcmp(expr->call_name, "format") == 0) {
    if (expr->call_arg_count < 1) {
      ctx->api->errorf(ctx->lower,
                       "builtin 'format' expects at least 1 argument, got %zu",
                       expr->call_arg_count);
      return PIE_LOWER_ERROR;
    }

    PieIrExpr *result = NULL;
    for (size_t i = 0; i < expr->call_arg_count; i++) {
      PieIrExpr *part = NULL;
      if (ctx->api->lower_expr(ctx->lower, expr->call_args[i].expr, &part) !=
          PIE_LOWER_OK) {
        pie_ir_expr_free(result);
        return PIE_LOWER_ERROR;
      }
      if (!result) {
        result = part;
      } else {
        PieIrExpr *new_result =
            pie_ir_expr_binary_typed("++", result, part, PIE_IR_TYPE_STRING);
        if (!new_result) {
          pie_ir_expr_free(part);
          pie_ir_expr_free(result);
          return PIE_LOWER_ERROR;
        }
        result = new_result;
      }
    }
    *out_expr = result;
    return PIE_LOWER_OK;
  }

  PieLowerFunctionInfo function;
  if (ctx->api->find_function(ctx->lower, expr->call_name, &function)) {
    if (function.type_param_count > 0) {
      PieIrTypeKind concrete_types[16];
      int concrete_type_widths[16];
      const char *type_param_names[16];
      size_t type_param_count = function.type_param_count;

      if (type_param_count > 16) {
        ctx->api->error(ctx->lower, "too many type parameters (max 16)");
        return PIE_LOWER_ERROR;
      }

      for (size_t i = 0; i < type_param_count; i++) {
        type_param_names[i] = function.type_param_names[i];
        concrete_types[i] = PIE_IR_TYPE_UNKNOWN;
        concrete_type_widths[i] = PIE_WIDTH_INFER;
      }

      for (size_t i = 0; i < function.param_count && i < expr->call_arg_count;
           i++) {
        const char *param_struct_name = NULL;
        if (function.param_types[i] == PIE_IR_TYPE_STRUCT) {
        }

        for (size_t t = 0; t < type_param_count; t++) {
          if (strcmp(function.param_names[i], type_param_names[t]) == 0 ||
              (function.param_types[i] == PIE_IR_TYPE_STRUCT &&
               param_struct_name &&
               strcmp(param_struct_name, type_param_names[t]) == 0)) {
            PieIrExpr *arg_expr = NULL;
            if (ctx->api->lower_expr(ctx->lower, expr->call_args[i].expr,
                                     &arg_expr) != PIE_LOWER_OK) {
              return PIE_LOWER_ERROR;
            }
            concrete_types[t] = arg_expr->type;
            concrete_type_widths[t] = arg_expr->type_width;
            pie_ir_expr_free(arg_expr);
            break;
          }
        }
      }

      PieIrTypeKind return_type = function.return_type;
      int return_type_width = function.return_type_width;
      if (return_type == PIE_IR_TYPE_STRUCT) {
        for (size_t t = 0; t < type_param_count; t++) {
          if (concrete_types[t] != PIE_IR_TYPE_UNKNOWN) {
            return_type = concrete_types[t];
            return_type_width = concrete_type_widths[t];
            break;
          }
        }
      }

      *out_expr = pie_ir_expr_call(expr->call_name, return_type);
      if (!*out_expr) {
        ctx->api->error(ctx->lower,
                        "out of memory while lowering function call");
        return PIE_LOWER_ERROR;
      }
      (*out_expr)->type_width = return_type_width;

      for (size_t i = 0; i < expr->call_arg_count; i++) {
        PieIrExpr *arg = NULL;
        if (ctx->api->lower_expr(ctx->lower, expr->call_args[i].expr, &arg) !=
            PIE_LOWER_OK) {
          pie_ir_expr_free(*out_expr);
          *out_expr = NULL;
          return PIE_LOWER_ERROR;
        }
        if (!pie_ir_expr_call_add_arg(*out_expr, arg)) {
          pie_ir_expr_free(arg);
          pie_ir_expr_free(*out_expr);
          *out_expr = NULL;
          ctx->api->error(
              ctx->lower,
              "out of memory while lowering function call argument");
          return PIE_LOWER_ERROR;
        }
      }
      return PIE_LOWER_OK;
    }

    *out_expr = pie_ir_expr_call(expr->call_name, function.return_type);
    if (!*out_expr) {
      ctx->api->error(ctx->lower, "out of memory while lowering function call");
      return PIE_LOWER_ERROR;
    }
    (*out_expr)->type_width = function.return_type_width;
    (*out_expr)->raw_pointee_type = function.return_raw_pointee_type;
    (*out_expr)->raw_pointee_width = function.return_raw_pointee_width;
    (*out_expr)->ref_inner_type = function.return_ref_inner_type;
    (*out_expr)->ref_inner_width = function.return_ref_inner_width;
    for (size_t i = 0; i < expr->call_arg_count; i++) {
      PieIrExpr *arg = NULL;
      if (ctx->api->lower_expr(ctx->lower, expr->call_args[i].expr, &arg) !=
          PIE_LOWER_OK) {
        pie_ir_expr_free(*out_expr);
        *out_expr = NULL;
        return PIE_LOWER_ERROR;
      }
      if (i < function.param_count &&
          !coerce_if_needed(ctx, function.param_types[i], &arg)) {
        pie_ir_expr_free(arg);
        pie_ir_expr_free(*out_expr);
        *out_expr = NULL;
        return PIE_LOWER_ERROR;
      }
      if (!pie_ir_expr_call_add_arg(*out_expr, arg)) {
        pie_ir_expr_free(arg);
        pie_ir_expr_free(*out_expr);
        *out_expr = NULL;
        ctx->api->error(ctx->lower,
                        "out of memory while lowering function call argument");
        return PIE_LOWER_ERROR;
      }
    }
    return PIE_LOWER_OK;
  }

  size_t local_id = 0;
  int is_mut = 0;
  PieIrTypeKind local_type = PIE_IR_TYPE_UNKNOWN;
  int local_type_width = 0;
  PieIrTypeKind raw_pointee_type = PIE_IR_TYPE_UNKNOWN;
  int raw_pointee_width = PIE_WIDTH_INFER;
  if (ctx->api->find_local(ctx->lower, expr->call_name, &local_id, &is_mut,
                           &local_type, &local_type_width, &raw_pointee_type,
                           &raw_pointee_width, NULL, NULL, NULL, NULL)) {
    if (local_type == PIE_IR_TYPE_CLOSURE) {
      PieIrExpr *closure_ref =
          pie_ir_expr_raw_local(local_id, local_type, local_type_width,
                                raw_pointee_type, raw_pointee_width);
      if (!closure_ref) {
        ctx->api->error(ctx->lower,
                        "out of memory while lowering closure reference");
        return PIE_LOWER_ERROR;
      }

      *out_expr = pie_ir_expr_closure_call(closure_ref, PIE_IR_TYPE_INT);
      if (!*out_expr) {
        pie_ir_expr_free(closure_ref);
        ctx->api->error(ctx->lower,
                        "out of memory while lowering closure call");
        return PIE_LOWER_ERROR;
      }

      for (size_t i = 0; i < expr->call_arg_count; i++) {
        PieIrExpr *arg = NULL;
        if (ctx->api->lower_expr(ctx->lower, expr->call_args[i].expr, &arg) !=
            PIE_LOWER_OK) {
          pie_ir_expr_free(*out_expr);
          *out_expr = NULL;
          return PIE_LOWER_ERROR;
        }
        if (!pie_ir_expr_call_add_arg(*out_expr, arg)) {
          pie_ir_expr_free(arg);
          pie_ir_expr_free(*out_expr);
          *out_expr = NULL;
          ctx->api->error(ctx->lower,
                          "out of memory while lowering closure call argument");
          return PIE_LOWER_ERROR;
        }
      }
      return PIE_LOWER_OK;
    }
  }

  ctx->api->errorf(ctx->lower, "undefined function '%s' during lowering",
                   expr->call_name);
  return PIE_LOWER_ERROR;
}
