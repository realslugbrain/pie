#define _POSIX_C_SOURCE 200809L
#include "pie/core/lower/lower.h"

#include <stdlib.h>
#include <string.h>

static PieIrTypeKind local_ir_type_from_ast(PieAstTypeKind ast_type) {
  switch (ast_type) {
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
  default:
    return PIE_IR_TYPE_UNKNOWN;
  }
}

PieLowerResult pie_feature_enums_lower_stmt(PieLowerContext *ctx,
                                            const PieStmt *stmt) {
  if (stmt->kind == PIE_STMT_ENUM) {
    PieIrStmt ir_stmt;
    memset(&ir_stmt, 0, sizeof(ir_stmt));
    ir_stmt.kind = PIE_IR_STMT_STRUCT;
    return ctx->api->push_stmt(ctx->lower, ir_stmt) ? PIE_LOWER_OK
                                                    : PIE_LOWER_ERROR;
  }

  if (stmt->kind == PIE_STMT_MATCH) {
    PieIrExpr *target = NULL;
    if (ctx->api->lower_expr(ctx->lower, stmt->match_target, &target) !=
        PIE_LOWER_OK) {
      return PIE_LOWER_ERROR;
    }

    PieIrStmt ir_stmt;
    memset(&ir_stmt, 0, sizeof(ir_stmt));
    ir_stmt.kind = PIE_IR_STMT_MATCH;
    ir_stmt.match_target = target;

    ir_stmt.match_case_count = stmt->match_case_count;
    if (stmt->match_case_count > 0) {
      ir_stmt.match_case_names =
          (char **)calloc(stmt->match_case_count, sizeof(char *));
      ir_stmt.match_case_bodies = (PieIrProgram **)calloc(
          stmt->match_case_count, sizeof(PieIrProgram *));
      ir_stmt.match_case_bindings =
          (char ***)calloc(stmt->match_case_count, sizeof(char **));
      ir_stmt.match_case_binding_counts =
          (size_t *)calloc(stmt->match_case_count, sizeof(size_t));
      ir_stmt.match_case_tags =
          (int *)calloc(stmt->match_case_count, sizeof(int));
      ir_stmt.match_case_binding_ids =
          (size_t **)calloc(stmt->match_case_count, sizeof(size_t *));
      for (size_t i = 0; i < stmt->match_case_count; i++) {
        size_t len = strlen(stmt->match_case_names[i]);
        ir_stmt.match_case_names[i] = (char *)malloc(len + 1);
        if (ir_stmt.match_case_names[i]) {
          memcpy(ir_stmt.match_case_names[i], stmt->match_case_names[i],
                 len + 1);
        }

        ir_stmt.match_case_binding_counts[i] =
            stmt->match_case_binding_counts[i];
        if (stmt->match_case_binding_counts[i] > 0) {
          ir_stmt.match_case_bindings[i] = (char **)calloc(
              stmt->match_case_binding_counts[i], sizeof(char *));
          ir_stmt.match_case_binding_ids[i] = (size_t *)calloc(
              stmt->match_case_binding_counts[i], sizeof(size_t));
          for (size_t j = 0; j < stmt->match_case_binding_counts[i]; j++) {
            size_t bind_len = strlen(stmt->match_case_bindings[i][j]);
            ir_stmt.match_case_bindings[i][j] = (char *)malloc(bind_len + 1);
            if (ir_stmt.match_case_bindings[i][j]) {
              memcpy(ir_stmt.match_case_bindings[i][j],
                     stmt->match_case_bindings[i][j], bind_len + 1);
            }
          }
        } else {
          ir_stmt.match_case_bindings[i] = NULL;
          ir_stmt.match_case_binding_ids[i] = NULL;
        }

        ir_stmt.match_case_bodies[i] =
            (PieIrProgram *)malloc(sizeof(PieIrProgram));
        if (ir_stmt.match_case_bodies[i]) {
          ctx->api->enter_scope(ctx->lower);

          const PieEnumVariant *variant_def = NULL;
          char *dot = strchr(stmt->match_case_names[i], '.');
          if (dot) {
            size_t enum_len = dot - stmt->match_case_names[i];
            char *enum_name = (char *)malloc(enum_len + 1);
            if (enum_name) {
              memcpy(enum_name, stmt->match_case_names[i], enum_len);
              enum_name[enum_len] = '\0';
              const PieEnumDef *enum_def =
                  ctx->api->find_enum(ctx->lower, enum_name);
              if (enum_def) {
                const char *variant_name = dot + 1;
                for (size_t v = 0; v < enum_def->variant_count; v++) {
                  if (strcmp(enum_def->variants[v].name, variant_name) == 0) {
                    variant_def = &enum_def->variants[v];
                    break;
                  }
                }
              }
              free(enum_name);
            }
          }

          for (size_t j = 0; j < ir_stmt.match_case_binding_counts[i]; j++) {
            PieIrTypeKind type = PIE_IR_TYPE_INT;
            int width = 8;
            if (variant_def && j < variant_def->payload_count) {
              type = local_ir_type_from_ast(variant_def->payload_kinds[j]);
              width = variant_def->payload_widths[j];
            }

            ctx->api->declare_local(
                ctx->lower, ir_stmt.match_case_bindings[i][j], 0, type, width,
                PIE_IR_TYPE_UNKNOWN, 0, PIE_IR_TYPE_UNKNOWN, PIE_WIDTH_INFER,
                NULL, NULL, &ir_stmt.match_case_binding_ids[i][j]);
          }
          ctx->api->lower_block(ctx->lower, stmt->match_case_bodies[i],
                                ir_stmt.match_case_bodies[i]);
          ctx->api->leave_scope(ctx->lower);
        }

        char *dot = strchr(stmt->match_case_names[i], '.');
        if (dot) {
          size_t enum_len = dot - stmt->match_case_names[i];
          size_t variant_len = strlen(dot + 1);
          char *enum_name = (char *)malloc(enum_len + 1);
          char *variant_name = (char *)malloc(variant_len + 1);
          if (enum_name && variant_name) {
            memcpy(enum_name, stmt->match_case_names[i], enum_len);
            enum_name[enum_len] = '\0';
            memcpy(variant_name, dot + 1, variant_len);
            variant_name[variant_len] = '\0';
            int tag = 0;
            if (ctx->api->find_variant_tag(ctx->lower, enum_name, variant_name,
                                           &tag)) {
              ir_stmt.match_case_tags[i] = tag;
            }
            free(enum_name);
            free(variant_name);
          } else {
            free(enum_name);
            free(variant_name);
          }
        }
      }
    }

    if (stmt->match_default) {
      ir_stmt.match_default = (PieIrProgram *)malloc(sizeof(PieIrProgram));
      if (ir_stmt.match_default) {
        ctx->api->lower_block(ctx->lower, stmt->match_default,
                              ir_stmt.match_default);
      }
    }

    if (!ctx->api->push_stmt(ctx->lower, ir_stmt)) {
      return PIE_LOWER_ERROR;
    }
    return PIE_LOWER_OK;
  }

  return PIE_LOWER_NO_MATCH;
}

PieLowerResult pie_feature_enums_lower_expr(PieLowerContext *ctx,
                                            const PieExpr *expr,
                                            PieIrExpr **out_expr) {
  if (expr->kind == PIE_EXPR_VARIANT) {
    *out_expr = pie_ir_expr_variant(expr->enum_name, expr->variant_name);
    if (!*out_expr) {
      ctx->api->error(ctx->lower,
                      "out of memory while lowering variant expression");
      return PIE_LOWER_ERROR;
    }

    int tag = 0;
    if (ctx->api->find_variant_tag(ctx->lower, expr->enum_name,
                                   expr->variant_name, &tag)) {
      (*out_expr)->variant_tag = tag;
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
                        "out of memory while lowering variant argument");
        return PIE_LOWER_ERROR;
      }
    }

    return PIE_LOWER_OK;
  }

  if (expr->kind == PIE_EXPR_MATCH) {
    PieIrExpr *target = NULL;
    if (ctx->api->lower_expr(ctx->lower, expr->match_expr_target, &target) !=
        PIE_LOWER_OK) {
      return PIE_LOWER_ERROR;
    }

    *out_expr = pie_ir_expr_match(target);
    if (!*out_expr) {
      ctx->api->error(ctx->lower,
                      "out of memory while lowering match expression");
      return PIE_LOWER_ERROR;
    }

    (*out_expr)->match_expr_case_count = expr->match_expr_case_count;
    if (expr->match_expr_case_count > 0) {
      (*out_expr)->match_expr_case_names =
          (char **)calloc(expr->match_expr_case_count, sizeof(char *));
      (*out_expr)->match_expr_case_bodies = (PieIrProgram **)calloc(
          expr->match_expr_case_count, sizeof(PieIrProgram *));
      (*out_expr)->match_expr_case_bindings =
          (char ***)calloc(expr->match_expr_case_count, sizeof(char **));
      (*out_expr)->match_expr_case_binding_ids =
          (size_t **)calloc(expr->match_expr_case_count, sizeof(size_t *));
      (*out_expr)->match_expr_case_binding_counts =
          (size_t *)calloc(expr->match_expr_case_count, sizeof(size_t));
      (*out_expr)->match_expr_case_tags =
          (int *)calloc(expr->match_expr_case_count, sizeof(int));
      (*out_expr)->match_expr_value_exprs = (PieIrExpr **)calloc(
          expr->match_expr_case_count, sizeof(PieIrExpr *));
      for (size_t i = 0; i < expr->match_expr_case_count; i++) {
        size_t len = strlen(expr->match_expr_case_names[i]);
        (*out_expr)->match_expr_case_names[i] = (char *)malloc(len + 1);
        if ((*out_expr)->match_expr_case_names[i]) {
          memcpy((*out_expr)->match_expr_case_names[i],
                 expr->match_expr_case_names[i], len + 1);
        }

        (*out_expr)->match_expr_case_binding_counts[i] =
            expr->match_expr_case_binding_counts[i];
        if (expr->match_expr_case_binding_counts[i] > 0) {
          (*out_expr)->match_expr_case_bindings[i] = (char **)calloc(
              expr->match_expr_case_binding_counts[i], sizeof(char *));
          (*out_expr)->match_expr_case_binding_ids[i] = (size_t *)calloc(
              expr->match_expr_case_binding_counts[i], sizeof(size_t));
          for (size_t j = 0; j < expr->match_expr_case_binding_counts[i]; j++) {
            size_t bind_len = strlen(expr->match_expr_case_bindings[i][j]);
            (*out_expr)->match_expr_case_bindings[i][j] =
                (char *)malloc(bind_len + 1);
            if ((*out_expr)->match_expr_case_bindings[i][j]) {
              memcpy((*out_expr)->match_expr_case_bindings[i][j],
                     expr->match_expr_case_bindings[i][j], bind_len + 1);
            }
          }
        } else {
          (*out_expr)->match_expr_case_bindings[i] = NULL;
          (*out_expr)->match_expr_case_binding_ids[i] = NULL;
        }

        (*out_expr)->match_expr_case_bodies[i] =
            (PieIrProgram *)malloc(sizeof(PieIrProgram));
        if ((*out_expr)->match_expr_case_bodies[i]) {
          ctx->api->enter_scope(ctx->lower);

          const PieEnumVariant *variant_def = NULL;
          char *dot = strchr(expr->match_expr_case_names[i], '.');
          if (dot) {
            size_t enum_len = dot - expr->match_expr_case_names[i];
            char *enum_name = (char *)malloc(enum_len + 1);
            if (enum_name) {
              memcpy(enum_name, expr->match_expr_case_names[i], enum_len);
              enum_name[enum_len] = '\0';
              const PieEnumDef *enum_def =
                  ctx->api->find_enum(ctx->lower, enum_name);
              if (enum_def) {
                const char *variant_name = dot + 1;
                for (size_t v = 0; v < enum_def->variant_count; v++) {
                  if (strcmp(enum_def->variants[v].name, variant_name) == 0) {
                    variant_def = &enum_def->variants[v];
                    break;
                  }
                }
              }
              free(enum_name);
            }
          }

          for (size_t j = 0; j < (*out_expr)->match_expr_case_binding_counts[i];
               j++) {
            PieIrTypeKind type = PIE_IR_TYPE_INT;
            int width = 8;
            if (variant_def && j < variant_def->payload_count) {
              type = local_ir_type_from_ast(variant_def->payload_kinds[j]);
              width = variant_def->payload_widths[j];
            }

            ctx->api->declare_local(
                ctx->lower, (*out_expr)->match_expr_case_bindings[i][j], 0,
                type, width, PIE_IR_TYPE_UNKNOWN, 0, PIE_IR_TYPE_UNKNOWN,
                PIE_WIDTH_INFER, NULL, NULL,
                &(*out_expr)->match_expr_case_binding_ids[i][j]);
          }
          PieProgram *ast_body = expr->match_expr_case_bodies[i];
          int has_value_expr = 0;
          PieIrExpr *val = NULL;
          if (ast_body->stmt_count > 0) {
            PieStmt *last = &ast_body->stmts[ast_body->stmt_count - 1];
            if (last->kind == PIE_STMT_EXPR && last->expr) {
              has_value_expr = 1;
              if (ctx->api->lower_expr(ctx->lower, last->expr, &val) ==
                      PIE_LOWER_OK &&
                  val) {
                (*out_expr)->match_expr_value_exprs[i] = val;
              }
              ast_body->stmt_count--;
            }
          }
          ctx->api->lower_block(ctx->lower, ast_body,
                                (*out_expr)->match_expr_case_bodies[i]);
          if (has_value_expr) {
            ast_body->stmt_count++;
          }

          ctx->api->leave_scope(ctx->lower);
        }

        char *dot = strchr(expr->match_expr_case_names[i], '.');
        if (dot) {
          size_t enum_len = dot - expr->match_expr_case_names[i];
          size_t variant_len = strlen(dot + 1);
          char *enum_name = (char *)malloc(enum_len + 1);
          char *variant_name = (char *)malloc(variant_len + 1);
          if (enum_name && variant_name) {
            memcpy(enum_name, expr->match_expr_case_names[i], enum_len);
            enum_name[enum_len] = '\0';
            memcpy(variant_name, dot + 1, variant_len);
            variant_name[variant_len] = '\0';
            int tag = 0;
            if (ctx->api->find_variant_tag(ctx->lower, enum_name, variant_name,
                                           &tag)) {
              (*out_expr)->match_expr_case_tags[i] = tag;
            }
            free(enum_name);
            free(variant_name);
          } else {
            free(enum_name);
            free(variant_name);
          }
        }
      }
    }

    if (expr->match_expr_default) {
      (*out_expr)->match_expr_default =
          (PieIrProgram *)malloc(sizeof(PieIrProgram));
      if ((*out_expr)->match_expr_default) {
        PieProgram *ast_default = expr->match_expr_default;
        int has_value_expr = 0;
        if (ast_default->stmt_count > 0) {
          PieStmt *last = &ast_default->stmts[ast_default->stmt_count - 1];
          if (last->kind == PIE_STMT_EXPR && last->expr) {
            has_value_expr = 1;
            PieIrExpr *val = NULL;
            if (ctx->api->lower_expr(ctx->lower, last->expr, &val) ==
                    PIE_LOWER_OK &&
                val) {
              (*out_expr)->match_expr_default_value = val;
            }
            ast_default->stmt_count--;
          }
        }
        ctx->api->lower_block(ctx->lower, ast_default,
                              (*out_expr)->match_expr_default);
        if (has_value_expr) {
          ast_default->stmt_count++;
        }
      }
    }

    return PIE_LOWER_OK;
  }

  if (expr->kind == PIE_EXPR_TRY) {
    PieIrExpr *inner = NULL;
    if (ctx->api->lower_expr(ctx->lower, expr->right, &inner) != PIE_LOWER_OK ||
        !inner) {
      return PIE_LOWER_ERROR;
    }

    PieIrExpr *match_expr = pie_ir_expr_match(inner);
    if (!match_expr) {
      pie_ir_expr_free(inner);
      return PIE_LOWER_ERROR;
    }

    match_expr->match_expr_case_count = 2;
    match_expr->match_expr_case_names = (char **)calloc(2, sizeof(char *));
    match_expr->match_expr_case_bodies =
        (PieIrProgram **)calloc(2, sizeof(PieIrProgram *));
    match_expr->match_expr_case_bindings = (char ***)calloc(2, sizeof(char **));
    match_expr->match_expr_case_binding_ids =
        (size_t **)calloc(2, sizeof(size_t *));
    match_expr->match_expr_case_binding_counts =
        (size_t *)calloc(2, sizeof(size_t));
    match_expr->match_expr_case_tags = (int *)calloc(2, sizeof(int));
    match_expr->match_expr_value_exprs =
        (PieIrExpr **)calloc(2, sizeof(PieIrExpr *));

    match_expr->match_expr_case_names[0] = strdup("Result.Ok");
    match_expr->match_expr_case_bindings[0] =
        (char **)calloc(1, sizeof(char *));
    match_expr->match_expr_case_bindings[0][0] = strdup("_try_val");
    match_expr->match_expr_case_binding_ids[0] =
        (size_t *)calloc(1, sizeof(size_t));
    match_expr->match_expr_case_binding_counts[0] = 1;

    match_expr->match_expr_case_names[1] = strdup("Result.Err");
    match_expr->match_expr_case_bindings[1] =
        (char **)calloc(1, sizeof(char *));
    match_expr->match_expr_case_bindings[1][0] = strdup("_try_err");
    match_expr->match_expr_case_binding_ids[1] =
        (size_t *)calloc(1, sizeof(size_t));
    match_expr->match_expr_case_binding_counts[1] = 1;

    ctx->api->find_variant_tag(ctx->lower, "Result", "Ok",
                               &match_expr->match_expr_case_tags[0]);
    ctx->api->find_variant_tag(ctx->lower, "Result", "Err",
                               &match_expr->match_expr_case_tags[1]);

    ctx->api->enter_scope(ctx->lower);
    size_t ok_local_id = 0;
    ctx->api->declare_local(ctx->lower, "_try_val", 0, PIE_IR_TYPE_INT, 8,
                            PIE_IR_TYPE_UNKNOWN, 0, PIE_IR_TYPE_UNKNOWN,
                            PIE_WIDTH_INFER, NULL, NULL, &ok_local_id);
    match_expr->match_expr_case_binding_ids[0][0] = ok_local_id;

    size_t err_local_id = 0;
    ctx->api->declare_local(ctx->lower, "_try_err", 0, PIE_IR_TYPE_INT, 8,
                            PIE_IR_TYPE_UNKNOWN, 0, PIE_IR_TYPE_UNKNOWN,
                            PIE_WIDTH_INFER, NULL, NULL, &err_local_id);
    match_expr->match_expr_case_binding_ids[1][0] = err_local_id;

    match_expr->match_expr_case_bodies[0] =
        (PieIrProgram *)calloc(1, sizeof(PieIrProgram));
    pie_ir_program_init(match_expr->match_expr_case_bodies[0]);

    PieIrExpr *ok_val = pie_ir_expr_local(ok_local_id, PIE_IR_TYPE_INT, 8,
                                          PIE_IR_TYPE_UNKNOWN, 0);
    match_expr->match_expr_value_exprs[0] = ok_val;

    match_expr->match_expr_case_bodies[1] =
        (PieIrProgram *)calloc(1, sizeof(PieIrProgram));
    pie_ir_program_init(match_expr->match_expr_case_bodies[1]);

    PieIrExpr *err_val = pie_ir_expr_local(err_local_id, PIE_IR_TYPE_INT, 8,
                                           PIE_IR_TYPE_UNKNOWN, 0);
    PieIrExpr *err_variant = pie_ir_expr_variant("Result", "Err");
    pie_ir_expr_call_add_arg(err_variant, err_val);

    PieIrStmt ret_stmt;
    memset(&ret_stmt, 0, sizeof(ret_stmt));
    ret_stmt.kind = PIE_IR_STMT_RETURN;
    ret_stmt.expr = err_variant;
    pie_ir_program_push_stmt(match_expr->match_expr_case_bodies[1], ret_stmt);
    match_expr->match_expr_value_exprs[1] = NULL;

    ctx->api->leave_scope(ctx->lower);

    match_expr->match_expr_default = NULL;

    *out_expr = match_expr;
    return PIE_LOWER_OK;
  }

  return PIE_LOWER_NO_MATCH;
}
