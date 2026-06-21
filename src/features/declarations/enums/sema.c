#include "pie/core/sema/sema.h"

#include <stdlib.h>
#include <string.h>

static char *enum_sema_strdup(const char *s) {
  size_t len = strlen(s);
  char *copy = (char *)malloc(len + 1);
  if (!copy) {
    return NULL;
  }
  memcpy(copy, s, len + 1);
  return copy;
}

PieSemaResult pie_feature_enums_sema_stmt(PieSemaContext *ctx,
                                          const PieStmt *stmt) {
  if (stmt->kind == PIE_STMT_ENUM) {
    return PIE_SEMA_OK;
  }

  if (stmt->kind == PIE_STMT_MATCH) {
    PieType target_type;
    if (ctx->api->check_expr(ctx->sema, stmt->match_target, &target_type) !=
        PIE_SEMA_OK) {
      return PIE_SEMA_ERROR;
    }

    const char *enum_name =
        (target_type.kind == PIE_TYPE_ENUM)
            ? target_type.enum_name
            : ((target_type.kind == PIE_TYPE_STRUCT) ? target_type.struct_name
                                                     : NULL);

    if (!enum_name) {
      ctx->api->errorf(ctx->sema, "match target must be an enum, got %s",
                       ctx->api->type_name(target_type));
      return PIE_SEMA_ERROR;
    }

    const PieEnumDef *enum_def = ctx->api->find_enum(ctx->sema, enum_name);
    if (!enum_def) {
      ctx->api->errorf(ctx->sema, "undefined enum '%s'", enum_name);
      return PIE_SEMA_ERROR;
    }

    for (size_t i = 0; i < stmt->match_case_count; i++) {
      if (!ctx->api->enter_scope(ctx->sema)) {
        return PIE_SEMA_ERROR;
      }

      char *dot = strchr(stmt->match_case_names[i], '.');
      const char *variant_name = dot ? dot + 1 : stmt->match_case_names[i];
      const PieEnumVariant *variant = NULL;
      for (size_t v = 0; v < enum_def->variant_count; v++) {
        if (strcmp(enum_def->variants[v].name, variant_name) == 0) {
          variant = &enum_def->variants[v];
          break;
        }
      }

      if (variant) {
        if (stmt->match_case_binding_counts[i] != variant->payload_count) {
          ctx->api->errorf(
              ctx->sema, "variant '%s.%s' expects %zu binding(s), got %zu",
              enum_def->name, variant->name, variant->payload_count,
              stmt->match_case_binding_counts[i]);
          ctx->api->leave_scope(ctx->sema);
          return PIE_SEMA_ERROR;
        }

        for (size_t j = 0; j < stmt->match_case_binding_counts[i]; j++) {
          PieType bind_type;
          memset(&bind_type, 0, sizeof(bind_type));
          switch (variant->payload_kinds[j]) {
          case PIE_AST_TYPE_INT:
            bind_type.kind = PIE_TYPE_INT;
            break;
          case PIE_AST_TYPE_FLOAT:
            bind_type.kind = PIE_TYPE_FLOAT;
            break;
          case PIE_AST_TYPE_STRING:
            bind_type.kind = PIE_TYPE_STRING;
            break;
          case PIE_AST_TYPE_BOOL:
            bind_type.kind = PIE_TYPE_BOOL;
            break;
          case PIE_AST_TYPE_CHAR:
            bind_type.kind = PIE_TYPE_CHAR;
            break;
          case PIE_AST_TYPE_BYTE:
            bind_type.kind = PIE_TYPE_BYTE;
            break;
          default:
            bind_type.kind = PIE_TYPE_INT;
            break;
          }
          if (!ctx->api->declare_symbol(
                  ctx->sema, stmt->match_case_bindings[i][j], bind_type, 0)) {
            ctx->api->leave_scope(ctx->sema);
            return PIE_SEMA_ERROR;
          }
        }
      }

      PieProgram *body = stmt->match_case_bodies[i];
      for (size_t j = 0; j < body->stmt_count; j++) {
        if (ctx->api->check_stmt(ctx->sema, &body->stmts[j]) != PIE_SEMA_OK) {
          ctx->api->leave_scope(ctx->sema);
          return PIE_SEMA_ERROR;
        }
      }
      ctx->api->leave_scope(ctx->sema);
    }
    if (stmt->match_default) {
      if (ctx->api->check_block(ctx->sema, stmt->match_default) !=
          PIE_SEMA_OK) {
        return PIE_SEMA_ERROR;
      }
    }

    return PIE_SEMA_OK;
  }

  return PIE_SEMA_NO_MATCH;
}

PieSemaResult pie_feature_enums_sema_expr(PieSemaContext *ctx,
                                          const PieExpr *expr,
                                          PieType *out_type) {
  if (expr->kind == PIE_EXPR_VARIANT) {
    const PieEnumDef *enum_def =
        ctx->api->find_enum(ctx->sema, expr->enum_name);
    if (!enum_def) {
      ctx->api->errorf(ctx->sema, "undefined enum '%s'", expr->enum_name);
      return PIE_SEMA_ERROR;
    }

    int found = 0;
    for (size_t i = 0; i < enum_def->variant_count; i++) {
      if (strcmp(enum_def->variants[i].name, expr->variant_name) == 0) {
        const PieEnumVariant *variant = &enum_def->variants[i];
        if (expr->call_arg_count != variant->payload_count) {
          ctx->api->errorf(ctx->sema,
                           "variant '%s.%s' expects %zu payload(s), got %zu",
                           expr->enum_name, expr->variant_name,
                           variant->payload_count, expr->call_arg_count);
          return PIE_SEMA_ERROR;
        }
        for (size_t j = 0; j < expr->call_arg_count; j++) {
          PieType arg_type;
          if (ctx->api->check_expr(ctx->sema, expr->call_args[j].expr,
                                   &arg_type) != PIE_SEMA_OK) {
            return PIE_SEMA_ERROR;
          }

          PieTypeKind expected_kind;
          switch (variant->payload_kinds[j]) {
          case PIE_AST_TYPE_INT:
            expected_kind = PIE_TYPE_INT;
            break;
          case PIE_AST_TYPE_FLOAT:
            expected_kind = PIE_TYPE_FLOAT;
            break;
          case PIE_AST_TYPE_STRING:
            expected_kind = PIE_TYPE_STRING;
            break;
          case PIE_AST_TYPE_BOOL:
            expected_kind = PIE_TYPE_BOOL;
            break;
          case PIE_AST_TYPE_CHAR:
            expected_kind = PIE_TYPE_CHAR;
            break;
          case PIE_AST_TYPE_BYTE:
            expected_kind = PIE_TYPE_BYTE;
            break;
          default:
            expected_kind = PIE_TYPE_INT;
            break;
          }
          if (arg_type.kind != expected_kind) {
            PieType expected_type;
            memset(&expected_type, 0, sizeof(expected_type));
            expected_type.kind = expected_kind;
            ctx->api->errorf(
                ctx->sema, "payload %zu of variant '%s.%s' must be %s, got %s",
                j + 1, expr->enum_name, expr->variant_name,
                ctx->api->type_name(expected_type),
                ctx->api->type_name(arg_type));
            return PIE_SEMA_ERROR;
          }
        }
        found = 1;
        break;
      }
    }

    if (!found) {
      ctx->api->errorf(ctx->sema, "'%s' is not a variant of enum '%s'",
                       expr->variant_name, expr->enum_name);
      return PIE_SEMA_ERROR;
    }

    out_type->kind = PIE_TYPE_ENUM;
    out_type->enum_name = enum_sema_strdup(expr->enum_name);
    return PIE_SEMA_OK;
  }

  if (expr->kind == PIE_EXPR_MATCH) {
    PieType target_type;
    if (ctx->api->check_expr(ctx->sema, expr->match_expr_target,
                             &target_type) != PIE_SEMA_OK) {
      return PIE_SEMA_ERROR;
    }

    const char *enum_name =
        (target_type.kind == PIE_TYPE_ENUM)
            ? target_type.enum_name
            : ((target_type.kind == PIE_TYPE_STRUCT) ? target_type.struct_name
                                                     : NULL);

    if (!enum_name) {
      ctx->api->errorf(ctx->sema, "match target must be an enum, got %s",
                       ctx->api->type_name(target_type));
      return PIE_SEMA_ERROR;
    }

    const PieEnumDef *enum_def = ctx->api->find_enum(ctx->sema, enum_name);
    if (!enum_def) {
      ctx->api->errorf(ctx->sema, "undefined enum '%s'", enum_name);
      return PIE_SEMA_ERROR;
    }

    PieType value_type;
    memset(&value_type, 0, sizeof(value_type));
    int has_value = 0;

    for (size_t i = 0; i < expr->match_expr_case_count; i++) {
      if (!ctx->api->enter_scope(ctx->sema)) {
        return PIE_SEMA_ERROR;
      }

      char *dot = strchr(expr->match_expr_case_names[i], '.');
      const char *variant_name = dot ? dot + 1 : expr->match_expr_case_names[i];
      const PieEnumVariant *variant = NULL;
      for (size_t v = 0; v < enum_def->variant_count; v++) {
        if (strcmp(enum_def->variants[v].name, variant_name) == 0) {
          variant = &enum_def->variants[v];
          break;
        }
      }

      if (variant) {
        if (expr->match_expr_case_binding_counts[i] != variant->payload_count) {
          ctx->api->errorf(
              ctx->sema, "variant '%s.%s' expects %zu binding(s), got %zu",
              enum_def->name, variant->name, variant->payload_count,
              expr->match_expr_case_binding_counts[i]);
          ctx->api->leave_scope(ctx->sema);
          return PIE_SEMA_ERROR;
        }

        for (size_t j = 0; j < expr->match_expr_case_binding_counts[i]; j++) {
          PieType bind_type;
          memset(&bind_type, 0, sizeof(bind_type));
          switch (variant->payload_kinds[j]) {
          case PIE_AST_TYPE_INT:
            bind_type.kind = PIE_TYPE_INT;
            break;
          case PIE_AST_TYPE_FLOAT:
            bind_type.kind = PIE_TYPE_FLOAT;
            break;
          case PIE_AST_TYPE_STRING:
            bind_type.kind = PIE_TYPE_STRING;
            break;
          case PIE_AST_TYPE_BOOL:
            bind_type.kind = PIE_TYPE_BOOL;
            break;
          case PIE_AST_TYPE_CHAR:
            bind_type.kind = PIE_TYPE_CHAR;
            break;
          case PIE_AST_TYPE_BYTE:
            bind_type.kind = PIE_TYPE_BYTE;
            break;
          default:
            bind_type.kind = PIE_TYPE_INT;
            break;
          }
          if (!ctx->api->declare_symbol(ctx->sema,
                                        expr->match_expr_case_bindings[i][j],
                                        bind_type, 0)) {
            ctx->api->leave_scope(ctx->sema);
            return PIE_SEMA_ERROR;
          }
        }
      }

      PieProgram *body = expr->match_expr_case_bodies[i];
      for (size_t j = 0; j + 1 < body->stmt_count; j++) {
        if (ctx->api->check_stmt(ctx->sema, &body->stmts[j]) != PIE_SEMA_OK) {
          ctx->api->leave_scope(ctx->sema);
          return PIE_SEMA_ERROR;
        }
      }
      if (body->stmt_count > 0) {
        PieStmt *last = &body->stmts[body->stmt_count - 1];
        if (last->kind == PIE_STMT_EXPR && last->expr) {
          PieType case_type;
          if (ctx->api->check_expr(ctx->sema, last->expr, &case_type) ==
              PIE_SEMA_OK) {
            if (!has_value) {
              value_type = case_type;
              has_value = 1;
            }
          }
        } else {
          if (ctx->api->check_stmt(ctx->sema, last) != PIE_SEMA_OK) {
            ctx->api->leave_scope(ctx->sema);
            return PIE_SEMA_ERROR;
          }
        }
      }
      ctx->api->leave_scope(ctx->sema);
    }
    if (expr->match_expr_default) {
      if (ctx->api->check_block(ctx->sema, expr->match_expr_default) !=
          PIE_SEMA_OK) {
        return PIE_SEMA_ERROR;
      }
    }

    if (has_value) {
      *out_type = value_type;
    } else {
      out_type->kind = PIE_TYPE_VOID;
    }
    return PIE_SEMA_OK;
  }

  if (expr->kind == PIE_EXPR_TRY) {
    PieType inner_type;
    if (ctx->api->check_expr(ctx->sema, expr->right, &inner_type) !=
        PIE_SEMA_OK) {
      return PIE_SEMA_ERROR;
    }

    if (inner_type.kind != PIE_TYPE_ENUM) {
      ctx->api->errorf(ctx->sema,
                       "try/? operator requires Result or Option type, got %s",
                       ctx->api->type_name(inner_type));
      return PIE_SEMA_ERROR;
    }

    const char *name = inner_type.enum_name;
    int is_result = (name && strcmp(name, "Result") == 0);
    int is_option = (name && strcmp(name, "Option") == 0);

    if (!is_result && !is_option) {
      ctx->api->errorf(
          ctx->sema, "try/? operator requires Result or Option, got %s", name);
      return PIE_SEMA_ERROR;
    }

    out_type->kind = PIE_TYPE_INT;
    return PIE_SEMA_OK;
  }

  return PIE_SEMA_NO_MATCH;
}
