#include "pie/core/lower/lower.h"

#include <stdlib.h>
#include <string.h>

static int resolve_auto_int_width(long long value) {
  if (value >= -128 && value <= 127)
    return PIE_WIDTH_8;
  if (value >= -32768 && value <= 32767)
    return PIE_WIDTH_16;
  if (value >= -2147483648LL && value <= 2147483647LL)
    return PIE_WIDTH_32;
  if (value >= -9223372036854775807LL - 1 && value <= 9223372036854775807LL)
    return PIE_WIDTH_64;
  return PIE_WIDTH_128;
}

static int resolve_auto_float_width(double value) {
  (void)value;
  return PIE_WIDTH_64;
}

static PieIrTypeKind type_from_annotation(PieAstType annotation,
                                          PieIrTypeKind fallback) {
  switch (annotation.kind) {
  case PIE_AST_TYPE_VOID:
    return PIE_IR_TYPE_VOID;
  case PIE_AST_TYPE_INT:
    return PIE_IR_TYPE_INT;
  case PIE_AST_TYPE_FLOAT:
    return PIE_IR_TYPE_FLOAT;
  case PIE_AST_TYPE_CHAR:
    return PIE_IR_TYPE_CHAR;
  case PIE_AST_TYPE_BYTE:
    return PIE_IR_TYPE_BYTE;
  case PIE_AST_TYPE_BOOL:
    return PIE_IR_TYPE_BOOL;
  case PIE_AST_TYPE_STRING:
    return PIE_IR_TYPE_STRING;
  case PIE_AST_TYPE_REF:
    return PIE_IR_TYPE_REF;
  case PIE_AST_TYPE_REF_MUT:
    return PIE_IR_TYPE_REF_MUT;
  case PIE_AST_TYPE_RAW_PTR:
    return PIE_IR_TYPE_RAW_PTR;
  case PIE_AST_TYPE_STRUCT:
    return PIE_IR_TYPE_STRUCT;
  case PIE_AST_TYPE_NULLABLE:
    return PIE_IR_TYPE_NULLABLE;
  case PIE_AST_TYPE_TUPLE:
    return PIE_IR_TYPE_TUPLE;
  case PIE_AST_TYPE_LIST:
    return PIE_IR_TYPE_LIST;
  case PIE_AST_TYPE_MAP:
    return PIE_IR_TYPE_MAP;
  case PIE_AST_TYPE_ENUM:
    return PIE_IR_TYPE_ENUM;
  case PIE_AST_TYPE_CLOSURE:
    return PIE_IR_TYPE_CLOSURE;
  case PIE_AST_TYPE_THREAD:
    return PIE_IR_TYPE_THREAD;
  case PIE_AST_TYPE_MUTEX:
    return PIE_IR_TYPE_MUTEX;
  case PIE_AST_TYPE_CHANNEL:
    return PIE_IR_TYPE_CHANNEL;
  case PIE_AST_TYPE_INFER:
    return fallback;
  }
  return fallback;
}

static PieIrTypeKind raw_pointee_type_from_annotation(PieAstType annotation,
                                                      PieIrTypeKind fallback) {
  if (annotation.kind != PIE_AST_TYPE_RAW_PTR) {
    return fallback;
  }
  return type_from_annotation(
      pie_ast_type(annotation.raw_pointee_kind, annotation.raw_pointee_width),
      PIE_IR_TYPE_UNKNOWN);
}

PieLowerResult pie_feature_bindings_lower_expr(PieLowerContext *ctx,
                                               const PieExpr *expr,
                                               PieIrExpr **out_expr) {
  if (expr->kind != PIE_EXPR_VAR) {
    return PIE_LOWER_NO_MATCH;
  }

  size_t local_id = 0;
  PieIrTypeKind type = PIE_IR_TYPE_UNKNOWN;
  int type_width = 0;
  PieIrTypeKind raw_pointee_type = PIE_IR_TYPE_UNKNOWN;
  int raw_pointee_width = PIE_WIDTH_INFER;
  PieIrTypeKind ref_inner_type = PIE_IR_TYPE_UNKNOWN;
  int ref_inner_width = PIE_WIDTH_INFER;
  const char *struct_name = NULL;
  const char *enum_name = NULL;
  if (!ctx->api->find_local(ctx->lower, expr->name, &local_id, NULL, &type,
                            &type_width, &raw_pointee_type, &raw_pointee_width,
                            &ref_inner_type, &ref_inner_width, &struct_name,
                            &enum_name)) {
    PieIrTypeKind cap_type;
    int cap_width;
    size_t env_offset;
    if (ctx->api->find_capture(ctx->lower, expr->name, &cap_type, &cap_width,
                               &env_offset)) {
      *out_expr = pie_ir_expr_raw_local(env_offset, cap_type, cap_width,
                                        PIE_IR_TYPE_UNKNOWN, PIE_WIDTH_INFER);
      if (!*out_expr) {
        ctx->api->error(ctx->lower,
                        "out of memory while lowering capture reference");
        return PIE_LOWER_ERROR;
      }
      return PIE_LOWER_OK;
    }
    ctx->api->errorf(ctx->lower, "undefined local '%s' during lowering",
                     expr->name);
    return PIE_LOWER_ERROR;
  }

  *out_expr = pie_ir_expr_raw_local(local_id, type, type_width,
                                    raw_pointee_type, raw_pointee_width);
  if (!*out_expr) {
    ctx->api->error(ctx->lower, "out of memory while lowering local reference");
    return PIE_LOWER_ERROR;
  }
  (*out_expr)->ref_inner_type = ref_inner_type;
  (*out_expr)->ref_inner_width = ref_inner_width;

  if (struct_name) {
    size_t len = strlen(struct_name);
    (*out_expr)->struct_name = malloc(len + 1);
    if ((*out_expr)->struct_name) {
      memcpy((*out_expr)->struct_name, struct_name, len + 1);
    }
  }
  if (enum_name) {
    size_t len = strlen(enum_name);
    (*out_expr)->enum_name = malloc(len + 1);
    if ((*out_expr)->enum_name) {
      memcpy((*out_expr)->enum_name, enum_name, len + 1);
    }
  }

  return PIE_LOWER_OK;
}

PieLowerResult pie_feature_bindings_lower_stmt(PieLowerContext *ctx,
                                               const PieStmt *stmt) {
  if (stmt->kind == PIE_STMT_LET) {
    PieIrExpr *init = NULL;
    if (ctx->api->lower_expr(ctx->lower, stmt->expr, &init) != PIE_LOWER_OK) {
      return PIE_LOWER_ERROR;
    }

    size_t local_id = 0;
    PieIrTypeKind local_type =
        type_from_annotation(stmt->type_annotation, init->type);
    int local_type_width = stmt->type_annotation.width;
    PieIrTypeKind raw_pointee_type = raw_pointee_type_from_annotation(
        stmt->type_annotation, init->raw_pointee_type);
    int raw_pointee_width = stmt->type_annotation.kind == PIE_AST_TYPE_RAW_PTR
                                ? stmt->type_annotation.raw_pointee_width
                                : init->raw_pointee_width;
    if (local_type_width == PIE_WIDTH_AUTO) {
      if (local_type == PIE_IR_TYPE_INT && stmt->expr->kind == PIE_EXPR_INT) {
        local_type_width = resolve_auto_int_width(stmt->expr->int_value);
      } else if (local_type == PIE_IR_TYPE_FLOAT &&
                 stmt->expr->kind == PIE_EXPR_FLOAT) {
        local_type_width = resolve_auto_float_width(stmt->expr->float_value);
      } else {
        local_type_width = PIE_WIDTH_64;
      }
    }
    init->type_width = local_type_width;
    if (local_type == PIE_IR_TYPE_RAW_PTR) {
      init->raw_pointee_type = raw_pointee_type;
      init->raw_pointee_width = raw_pointee_width;
    }

    const char *struct_name =
        (stmt->type_annotation.kind == PIE_AST_TYPE_STRUCT)
            ? stmt->type_annotation.struct_name
            : NULL;
    const char *enum_name = (stmt->type_annotation.kind == PIE_AST_TYPE_ENUM)
                                ? stmt->type_annotation.enum_name
                                : NULL;

    PieIrTypeKind ref_inner_type = PIE_IR_TYPE_UNKNOWN;
    int ref_inner_width = PIE_WIDTH_INFER;
    if (stmt->type_annotation.kind == PIE_AST_TYPE_REF ||
        stmt->type_annotation.kind == PIE_AST_TYPE_REF_MUT) {
      ref_inner_type = type_from_annotation(
          pie_ast_type(stmt->type_annotation.ref_inner_kind,
                       stmt->type_annotation.ref_inner_width),
          PIE_IR_TYPE_UNKNOWN);
      ref_inner_width = stmt->type_annotation.ref_inner_width;
    }

    if (!ctx->api->declare_local(
            ctx->lower, stmt->name, stmt->is_mut, local_type, local_type_width,
            raw_pointee_type, raw_pointee_width, ref_inner_type,
            ref_inner_width, struct_name, enum_name, &local_id)) {
      pie_ir_expr_free(init);
      return PIE_LOWER_ERROR;
    }

    if (stmt->type_annotation.kind == PIE_AST_TYPE_NULLABLE) {
      PieIrProgram *ir = ctx->api->root_ir(ctx->lower);
      if (local_id < ir->local_count) {
        ir->locals[local_id].nullable_inner_type = type_from_annotation(
            pie_ast_type(stmt->type_annotation.nullable_inner_kind,
                         stmt->type_annotation.nullable_inner_width),
            PIE_IR_TYPE_UNKNOWN);
        ir->locals[local_id].nullable_inner_width =
            stmt->type_annotation.nullable_inner_width;
      }
    }

    if (stmt->type_annotation.kind == PIE_AST_TYPE_REF ||
        stmt->type_annotation.kind == PIE_AST_TYPE_REF_MUT) {
      PieIrProgram *ir = ctx->api->root_ir(ctx->lower);
      if (local_id < ir->local_count) {
        ir->locals[local_id].ref_inner_type = type_from_annotation(
            pie_ast_type(stmt->type_annotation.ref_inner_kind,
                         stmt->type_annotation.ref_inner_width),
            PIE_IR_TYPE_UNKNOWN);
        ir->locals[local_id].ref_inner_width =
            stmt->type_annotation.ref_inner_width;
      }
    }

    PieIrStmt ir_stmt;
    memset(&ir_stmt, 0, sizeof(ir_stmt));
    ir_stmt.kind = PIE_IR_STMT_LET;
    ir_stmt.local_id = local_id;
    ir_stmt.is_mut = stmt->is_mut;
    ir_stmt.expr = init;
    if (!ctx->api->push_stmt(ctx->lower, ir_stmt)) {
      pie_ir_expr_free(init);
      return PIE_LOWER_ERROR;
    }
    return PIE_LOWER_OK;
  }

  if (stmt->kind == PIE_STMT_ASSIGN) {
    size_t local_id = 0;
    if (!ctx->api->find_local(ctx->lower, stmt->name, &local_id, NULL, NULL,
                              NULL, NULL, NULL, NULL, NULL, NULL, NULL)) {
      PieIrTypeKind cap_type;
      int cap_width;
      size_t env_offset;
      if (ctx->api->find_capture(ctx->lower, stmt->name, &cap_type, &cap_width,
                                 &env_offset)) {
        local_id = env_offset;
      } else {
        ctx->api->errorf(ctx->lower, "undefined local '%s' during lowering",
                         stmt->name);
        return PIE_LOWER_ERROR;
      }
    }

    PieIrExpr *value = NULL;
    if (ctx->api->lower_expr(ctx->lower, stmt->expr, &value) != PIE_LOWER_OK) {
      return PIE_LOWER_ERROR;
    }

    PieIrStmt ir_stmt;
    memset(&ir_stmt, 0, sizeof(ir_stmt));
    ir_stmt.kind = PIE_IR_STMT_ASSIGN;
    ir_stmt.local_id = local_id;
    ir_stmt.expr = value;
    strncpy(ir_stmt.assign_op, stmt->assign_op, sizeof(ir_stmt.assign_op) - 1);
    if (!ctx->api->push_stmt(ctx->lower, ir_stmt)) {
      pie_ir_expr_free(value);
      return PIE_LOWER_ERROR;
    }
    return PIE_LOWER_OK;
  }

  if (stmt->kind == PIE_STMT_ASSIGN_MULTI) {
    PieIrExpr *tmp_exprs[64];
    if (stmt->multi_count > 64) {
      ctx->api->errorf(
          ctx->lower, "multi-assignment supports at most 64 variables, got %zu",
          stmt->multi_count);
      return PIE_LOWER_ERROR;
    }

    for (size_t i = 0; i < stmt->multi_count; i++) {
      size_t lhs_local_id = 0;
      PieIrTypeKind lhs_type = PIE_IR_TYPE_UNKNOWN;
      int lhs_type_width = 0;
      PieIrTypeKind lhs_raw_pointee_type = PIE_IR_TYPE_UNKNOWN;
      int lhs_raw_pointee_width = PIE_WIDTH_INFER;
      PieIrTypeKind lhs_ref_inner_type = PIE_IR_TYPE_UNKNOWN;
      int lhs_ref_inner_width = PIE_WIDTH_INFER;
      const char *lhs_struct_name = NULL;
      const char *lhs_enum_name = NULL;
      if (!ctx->api->find_local(ctx->lower, stmt->multi_names[i], &lhs_local_id,
                                NULL, &lhs_type, &lhs_type_width,
                                &lhs_raw_pointee_type, &lhs_raw_pointee_width,
                                &lhs_ref_inner_type, &lhs_ref_inner_width,
                                &lhs_struct_name, &lhs_enum_name)) {
        ctx->api->errorf(ctx->lower, "undefined local '%s' during lowering",
                         stmt->multi_names[i]);
        return PIE_LOWER_ERROR;
      }

      PieIrExpr *value = NULL;
      if (ctx->api->lower_expr(ctx->lower, stmt->multi_exprs[i], &value) !=
          PIE_LOWER_OK) {
        return PIE_LOWER_ERROR;
      }
      value->type_width = lhs_type_width;
      if (lhs_type == PIE_IR_TYPE_RAW_PTR) {
        value->raw_pointee_type = lhs_raw_pointee_type;
        value->raw_pointee_width = lhs_raw_pointee_width;
      }

      char tmp_name[64];
      snprintf(tmp_name, sizeof(tmp_name), "__tmp_swap_%zu", i);
      size_t tmp_id = 0;
      if (!ctx->api->declare_local(
              ctx->lower, tmp_name, 1, lhs_type, lhs_type_width,
              lhs_raw_pointee_type, lhs_raw_pointee_width, PIE_IR_TYPE_UNKNOWN,
              PIE_WIDTH_INFER, lhs_struct_name, lhs_enum_name, &tmp_id)) {
        pie_ir_expr_free(value);
        return PIE_LOWER_ERROR;
      }

      PieIrStmt let_stmt;
      memset(&let_stmt, 0, sizeof(let_stmt));
      let_stmt.kind = PIE_IR_STMT_LET;
      let_stmt.local_id = tmp_id;
      let_stmt.is_mut = 1;
      let_stmt.expr = value;
      if (!ctx->api->push_stmt(ctx->lower, let_stmt)) {
        pie_ir_expr_free(value);
        return PIE_LOWER_ERROR;
      }
      tmp_exprs[i] =
          pie_ir_expr_raw_local(tmp_id, lhs_type, lhs_type_width,
                                lhs_raw_pointee_type, lhs_raw_pointee_width);
    }

    for (size_t i = 0; i < stmt->multi_count; i++) {
      size_t lhs_local_id = 0;
      if (!ctx->api->find_local(ctx->lower, stmt->multi_names[i], &lhs_local_id,
                                NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                NULL)) {
        ctx->api->errorf(ctx->lower, "undefined local '%s' during lowering",
                         stmt->multi_names[i]);
        return PIE_LOWER_ERROR;
      }

      PieIrStmt assign_stmt;
      memset(&assign_stmt, 0, sizeof(assign_stmt));
      assign_stmt.kind = PIE_IR_STMT_ASSIGN;
      assign_stmt.local_id = lhs_local_id;
      assign_stmt.expr = tmp_exprs[i];
      strncpy(assign_stmt.assign_op, "<-", sizeof(assign_stmt.assign_op) - 1);
      if (!ctx->api->push_stmt(ctx->lower, assign_stmt)) {
        pie_ir_expr_free(tmp_exprs[i]);
        return PIE_LOWER_ERROR;
      }
    }
    return PIE_LOWER_OK;
  }

  return PIE_LOWER_NO_MATCH;
}
