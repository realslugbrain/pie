#include "pie/core/sema/sema.h"

#include <stdlib.h>
#include <string.h>

static char *feature_strdup(const char *s) {
  if (!s)
    return NULL;
  size_t len = strlen(s);
  char *copy = malloc(len + 1);
  if (copy) {
    memcpy(copy, s, len + 1);
  }
  return copy;
}

PieSemaResult pie_feature_structs_sema_stmt(PieSemaContext *ctx,
                                            const PieStmt *stmt) {
  if (stmt->kind == PIE_STMT_STRUCT) {
    return PIE_SEMA_OK;
  }
  if (stmt->kind == PIE_STMT_FIELD_ASSIGN) {
    if (!stmt->field_target || !stmt->field_target->left ||
        !stmt->field_target->field_name) {
      ctx->api->error(ctx->sema, "invalid field assignment");
      return PIE_SEMA_ERROR;
    }
    PieType obj_type;
    if (ctx->api->check_expr(ctx->sema, stmt->field_target->left, &obj_type) !=
        PIE_SEMA_OK) {
      return PIE_SEMA_ERROR;
    }
    if (obj_type.kind != PIE_TYPE_STRUCT) {
      if ((obj_type.kind == PIE_TYPE_REF ||
           obj_type.kind == PIE_TYPE_REF_MUT) &&
          obj_type.ref_inner_kind == PIE_TYPE_STRUCT &&
          obj_type.ref_inner_struct_name) {
        obj_type.kind = PIE_TYPE_STRUCT;
        obj_type.struct_name = obj_type.ref_inner_struct_name;
      } else {
        ctx->api->errorf(ctx->sema, "cannot assign field on non-struct type %s",
                         ctx->api->type_name(obj_type));
        return PIE_SEMA_ERROR;
      }
    }
    PieType val_type;
    if (ctx->api->check_expr(ctx->sema, stmt->expr, &val_type) != PIE_SEMA_OK) {
      return PIE_SEMA_ERROR;
    }
    return PIE_SEMA_OK;
  }
  return PIE_SEMA_NO_MATCH;
}

PieSemaResult pie_feature_structs_sema_expr(PieSemaContext *ctx,
                                            const PieExpr *expr,
                                            PieType *out_type) {
  if (expr->kind == PIE_EXPR_NEW) {
    out_type->kind = PIE_TYPE_STRUCT;
    out_type->struct_name = NULL;
    if (expr->new_type_name) {
      out_type->struct_name = feature_strdup(expr->new_type_name);
    }
    return PIE_SEMA_OK;
  }
  if (expr->kind == PIE_EXPR_FIELD) {
    if (expr->left && expr->left->kind == PIE_EXPR_VAR && expr->field_name) {
      const PieEnumDef *enum_def =
          ctx->api->find_enum(ctx->sema, expr->left->name);
      if (enum_def) {
        for (size_t i = 0; i < enum_def->variant_count; i++) {
          if (strcmp(enum_def->variants[i].name, expr->field_name) == 0) {
            if (enum_def->variants[i].payload_count > 0) {
              ctx->api->errorf(ctx->sema, "variant '%s.%s' requires payloads",
                               expr->left->name, expr->field_name);
              return PIE_SEMA_ERROR;
            }
            out_type->kind = PIE_TYPE_ENUM;
            out_type->enum_name = feature_strdup(expr->left->name);
            return PIE_SEMA_OK;
          }
        }
        ctx->api->errorf(ctx->sema, "'%s' is not a variant of enum '%s'",
                         expr->field_name, expr->left->name);
        out_type->kind = PIE_TYPE_ERROR;
        return PIE_SEMA_ERROR;
      }
    }

    PieType obj_type;
    if (ctx->api->check_expr(ctx->sema, expr->left, &obj_type) != PIE_SEMA_OK) {
      return PIE_SEMA_ERROR;
    }
    if (obj_type.kind == PIE_TYPE_TUPLE) {
      long long idx = -1;
      if (expr->field_name) {
        idx = 0;
        for (const char *p = expr->field_name; *p >= '0' && *p <= '9'; p++) {
          idx = idx * 10 + (*p - '0');
        }
        if (idx < 0 || (size_t)idx >= obj_type.tuple_element_count) {
          ctx->api->errorf(
              ctx->sema,
              "tuple index %lld out of range (tuple has %zu elements)", idx,
              obj_type.tuple_element_count);
          return PIE_SEMA_ERROR;
        }
      }
      if (idx >= 0 && (size_t)idx < obj_type.tuple_element_count) {
        out_type->kind = obj_type.tuple_element_kinds[idx];
        out_type->type_width = obj_type.tuple_element_widths[idx];
        return PIE_SEMA_OK;
      }
      ctx->api->errorf(ctx->sema, "cannot access field '%s' on tuple type",
                       expr->field_name ? expr->field_name : "?");
      return PIE_SEMA_ERROR;
    }
    if (obj_type.kind != PIE_TYPE_STRUCT) {
      if ((obj_type.kind == PIE_TYPE_REF ||
           obj_type.kind == PIE_TYPE_REF_MUT) &&
          obj_type.ref_inner_kind == PIE_TYPE_STRUCT &&
          obj_type.ref_inner_struct_name) {
        obj_type.kind = PIE_TYPE_STRUCT;
        obj_type.struct_name = obj_type.ref_inner_struct_name;
      } else {
        ctx->api->errorf(ctx->sema,
                         "cannot access field '%s' on non-struct type %s",
                         expr->field_name ? expr->field_name : "?",
                         ctx->api->type_name(obj_type));
        return PIE_SEMA_ERROR;
      }
    }

    if (!obj_type.struct_name) {
      ctx->api->error(ctx->sema, "anonymous struct type has no fields");
      return PIE_SEMA_ERROR;
    }

    const PieStructDef *def =
        ctx->api->find_struct(ctx->sema, obj_type.struct_name);
    if (!def) {
      ctx->api->errorf(ctx->sema, "undefined struct '%s'",
                       obj_type.struct_name);
      return PIE_SEMA_ERROR;
    }

    for (size_t i = 0; i < def->field_count; i++) {
      if (strcmp(def->fields[i].name, expr->field_name) == 0) {
        PieAstType ast = def->fields[i].type;
        memset(out_type, 0, sizeof(*out_type));
        switch (ast.kind) {
        case PIE_AST_TYPE_INT:
          out_type->kind = PIE_TYPE_INT;
          break;
        case PIE_AST_TYPE_FLOAT:
          out_type->kind = PIE_TYPE_FLOAT;
          break;
        case PIE_AST_TYPE_STRING:
          out_type->kind = PIE_TYPE_STRING;
          break;
        case PIE_AST_TYPE_BOOL:
          out_type->kind = PIE_TYPE_BOOL;
          break;
        case PIE_AST_TYPE_CHAR:
          out_type->kind = PIE_TYPE_CHAR;
          break;
        case PIE_AST_TYPE_BYTE:
          out_type->kind = PIE_TYPE_BYTE;
          break;
        case PIE_AST_TYPE_STRUCT:
          out_type->kind = PIE_TYPE_STRUCT;
          out_type->struct_name = feature_strdup(ast.struct_name);
          break;
        default:
          out_type->kind = PIE_TYPE_INT;
          break;
        }
        out_type->type_width = ast.width;
        return PIE_SEMA_OK;
      }
    }

    ctx->api->errorf(ctx->sema, "struct '%s' has no field '%s'",
                     obj_type.struct_name, expr->field_name);
    return PIE_SEMA_ERROR;
  }
  return PIE_SEMA_NO_MATCH;
}
