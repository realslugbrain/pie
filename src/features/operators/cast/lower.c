#include "pie/core/lower/lower.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *cast_strdup(const char *s) {
  if (!s)
    return NULL;
  size_t len = strlen(s);
  char *copy = (char *)malloc(len + 1);
  if (copy) {
    memcpy(copy, s, len + 1);
  }
  return copy;
}

static PieIrTypeKind lower_type_from_ast(PieAstTypeKind kind) {
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
    return PIE_IR_TYPE_BYTE;
  case PIE_AST_TYPE_BYTE:
    return PIE_IR_TYPE_BYTE;
  case PIE_AST_TYPE_VOID:
    return PIE_IR_TYPE_VOID;
  default:
    return PIE_IR_TYPE_UNKNOWN;
  }
}

static PieExpr *build_enum_to_string_match(const PieEnumDef *def,
                                           PieExpr *target_expr) {
  PieExpr *match = pie_expr_match(target_expr);
  if (!match)
    return NULL;

  match->match_expr_case_names =
      (char **)calloc(def->variant_count, sizeof(char *));
  match->match_expr_case_bodies =
      (PieProgram **)calloc(def->variant_count, sizeof(PieProgram *));
  match->match_expr_case_bindings =
      (char ***)calloc(def->variant_count, sizeof(char **));
  match->match_expr_case_binding_counts =
      (size_t *)calloc(def->variant_count, sizeof(size_t));
  match->match_expr_case_count = def->variant_count;

  for (size_t i = 0; i < def->variant_count; i++) {
    const PieEnumVariant *variant = &def->variants[i];

    size_t name_len = strlen(def->name) + 1 + strlen(variant->name);
    char *case_name = (char *)malloc(name_len + 1);
    if (case_name) {
      sprintf(case_name, "%s.%s", def->name, variant->name);
    }
    match->match_expr_case_names[i] = case_name;

    if (variant->payload_count > 0) {
      match->match_expr_case_bindings[i] =
          (char **)calloc(variant->payload_count, sizeof(char *));
      match->match_expr_case_binding_counts[i] = variant->payload_count;
      for (size_t j = 0; j < variant->payload_count; j++) {
        char bind_name[32];
        snprintf(bind_name, sizeof(bind_name), "__payload_%zu", j);
        match->match_expr_case_bindings[i][j] = cast_strdup(bind_name);
      }
    } else {
      match->match_expr_case_bindings[i] = NULL;
      match->match_expr_case_binding_counts[i] = 0;
    }

    PieExpr *body_expr = NULL;
    if (variant->payload_count == 1 &&
        variant->payload_kinds[0] == PIE_AST_TYPE_STRING) {
      char bind_name[32];
      snprintf(bind_name, sizeof(bind_name), "__payload_0");
      body_expr = pie_expr_var(bind_name);
    } else {
      body_expr = pie_expr_string(variant->name, strlen(variant->name));
      if (variant->payload_count > 0) {
        body_expr =
            pie_expr_binary_op("++", body_expr, pie_expr_string("(", 1));
        for (size_t j = 0; j < variant->payload_count; j++) {
          if (j > 0) {
            body_expr =
                pie_expr_binary_op("++", body_expr, pie_expr_string(", ", 2));
          }
          char bind_name[32];
          snprintf(bind_name, sizeof(bind_name), "__payload_%zu", j);
          PieExpr *arg_var = pie_expr_var(bind_name);
          PieExpr *cast_expr =
              pie_expr_cast(arg_var, PIE_AST_TYPE_STRING, PIE_WIDTH_INFER);
          body_expr = pie_expr_binary_op("++", body_expr, cast_expr);
        }
        body_expr =
            pie_expr_binary_op("++", body_expr, pie_expr_string(")", 1));
      }
    }

    PieProgram *body_prog = (PieProgram *)malloc(sizeof(PieProgram));
    if (body_prog) {
      pie_program_init(body_prog);
      PieStmt body_stmt;
      memset(&body_stmt, 0, sizeof(body_stmt));
      body_stmt.kind = PIE_STMT_EXPR;
      body_stmt.expr = body_expr;
      pie_program_push_stmt(body_prog, body_stmt);
    }
    match->match_expr_case_bodies[i] = body_prog;
  }

  return match;
}

PieLowerResult pie_feature_cast_lower_expr(PieLowerContext *ctx,
                                           const PieExpr *expr,
                                           PieIrExpr **out_expr) {
  if (expr->kind != PIE_EXPR_CAST) {
    return PIE_LOWER_NO_MATCH;
  }

  PieIrExpr *inner = NULL;
  if (ctx->api->lower_expr(ctx->lower, expr->cast_inner, &inner) !=
      PIE_LOWER_OK) {
    return PIE_LOWER_ERROR;
  }

  int is_enum = (inner->type == PIE_IR_TYPE_ENUM);
  const char *enum_name = NULL;
  if (inner->type == PIE_IR_TYPE_ENUM) {
    enum_name = inner->enum_name;
  } else if (inner->type == PIE_IR_TYPE_STRUCT && inner->struct_name) {
    if (ctx->api->find_enum(ctx->lower, inner->struct_name)) {
      is_enum = 1;
      enum_name = inner->struct_name;
    }
  }

  if (is_enum && expr->cast_target_kind == PIE_AST_TYPE_STRING) {
    const PieEnumDef *def = ctx->api->find_enum(ctx->lower, enum_name);
    pie_ir_expr_free(inner);

    if (!def) {
      ctx->api->errorf(ctx->lower, "failed to resolve enum definition for '%s'",
                       enum_name);
      return PIE_LOWER_ERROR;
    }

    PieExpr *match_expr = build_enum_to_string_match(def, expr->cast_inner);
    if (!match_expr) {
      ctx->api->error(ctx->lower,
                      "out of memory while building enum string cast match");
      return PIE_LOWER_ERROR;
    }

    PieLowerResult result =
        ctx->api->lower_expr(ctx->lower, match_expr, out_expr);

    match_expr->match_expr_target = NULL;
    pie_expr_free(match_expr);
    return result;
  }

  PieIrTypeKind target = lower_type_from_ast(expr->cast_target_kind);
  *out_expr = pie_ir_expr_cast(inner, target, expr->cast_target_width);
  if (!*out_expr) {
    pie_ir_expr_free(inner);
    ctx->api->error(ctx->lower, "out of memory while lowering cast expression");
    return PIE_LOWER_ERROR;
  }

  return PIE_LOWER_OK;
}