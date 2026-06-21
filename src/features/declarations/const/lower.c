#include "pie/core/lower/lower.h"
#include "pie/core/ast/ast.h"
#include "pie/core/parser/parser.h"

#include <string.h>

static PieIrTypeKind ast_kind_to_ir(PieAstTypeKind kind) {
  switch (kind) {
  case PIE_AST_TYPE_INT:
    return PIE_IR_TYPE_INT;
  case PIE_AST_TYPE_FLOAT:
    return PIE_IR_TYPE_FLOAT;
  case PIE_AST_TYPE_BOOL:
    return PIE_IR_TYPE_BOOL;
  case PIE_AST_TYPE_STRING:
    return PIE_IR_TYPE_STRING;
  case PIE_AST_TYPE_CHAR:
    return PIE_IR_TYPE_CHAR;
  case PIE_AST_TYPE_BYTE:
    return PIE_IR_TYPE_BYTE;
  default:
    return PIE_IR_TYPE_UNKNOWN;
  }
}

PieLowerResult pie_feature_const_lower_expr(PieLowerContext *ctx,
                                            const PieExpr *expr,
                                            PieIrExpr **out_expr) {
  if (expr->kind != PIE_EXPR_VAR) {
    return PIE_LOWER_NO_MATCH;
  }

  const PieConstDef *con =
      pie_program_find_const(ctx->api->program(ctx->lower), expr->name);
  if (!con) {
    return PIE_LOWER_NO_MATCH;
  }

  return ctx->api->lower_expr(ctx->lower, con->value, out_expr);
}

PieLowerResult pie_feature_const_lower_stmt(PieLowerContext *ctx,
                                            const PieStmt *stmt) {
  if (stmt->kind != PIE_STMT_CONST) {
    return PIE_LOWER_NO_MATCH;
  }

  PieIrExpr *init = NULL;
  if (ctx->api->lower_expr(ctx->lower, stmt->const_def->value, &init) !=
      PIE_LOWER_OK) {
    return PIE_LOWER_ERROR;
  }

  PieIrTypeKind local_type = ast_kind_to_ir(stmt->const_def->type.kind);
  if (local_type == PIE_IR_TYPE_UNKNOWN) {
    local_type = init->type;
  }

  size_t local_id = 0;
  if (!ctx->api->declare_local(ctx->lower, stmt->const_def->name, 0, local_type,
                               init->type_width, init->raw_pointee_type,
                               init->raw_pointee_width, init->ref_inner_type,
                               init->ref_inner_width, init->struct_name,
                               init->enum_name, &local_id)) {
    ctx->api->error(ctx->lower, "out of memory while adding const local");
    return PIE_LOWER_ERROR;
  }

  PieIrStmt ir_stmt;
  memset(&ir_stmt, 0, sizeof(ir_stmt));
  ir_stmt.kind = PIE_IR_STMT_LET;
  ir_stmt.local_id = local_id;
  ir_stmt.is_mut = 0;
  ir_stmt.expr = init;
  if (!ctx->api->push_stmt(ctx->lower, ir_stmt)) {
    ctx->api->error(ctx->lower, "out of memory while pushing const statement");
    return PIE_LOWER_ERROR;
  }

  return PIE_LOWER_OK;
}
