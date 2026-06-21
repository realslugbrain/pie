#include "pie/core/lower/lower.h"

#include <stdlib.h>
#include <string.h>

static int ir_ast_type_size(PieAstType type);

PieLowerResult pie_feature_structs_lower_stmt(PieLowerContext *ctx,
                                              const PieStmt *stmt) {
  if (stmt->kind == PIE_STMT_STRUCT) {
    if (!stmt->struct_def) {
      return PIE_LOWER_OK;
    }
    PieIrProgram *ir = ctx->api->ir(ctx->lower);
    size_t idx = ir->struct_count;
    if (ir->struct_count == ir->struct_capacity) {
      size_t next_cap = ir->struct_capacity ? ir->struct_capacity * 2 : 8;
      PieStructDef *next =
          realloc(ir->structs, next_cap * sizeof(PieStructDef));
      if (!next) {
        ctx->api->error(ctx->lower, "out of memory");
        return PIE_LOWER_ERROR;
      }
      ir->structs = next;
      ir->struct_capacity = next_cap;
    }
    PieStructDef *dest = &ir->structs[idx];
    memset(dest, 0, sizeof(*dest));
    if (stmt->struct_def->name) {
      size_t len = strlen(stmt->struct_def->name);
      dest->name = malloc(len + 1);
      if (dest->name)
        memcpy(dest->name, stmt->struct_def->name, len + 1);
    }
    dest->field_count = stmt->struct_def->field_count;
    if (dest->field_count > 0) {
      dest->fields = malloc(dest->field_count * sizeof(PieStructField));
      if (dest->fields) {
        memcpy(dest->fields, stmt->struct_def->fields,
               dest->field_count * sizeof(PieStructField));
      }
    }
    ir->struct_count++;
    return PIE_LOWER_OK;
  }
  if (stmt->kind == PIE_STMT_FIELD_ASSIGN) {
    if (!stmt->field_target || !stmt->field_target->left ||
        !stmt->field_target->field_name) {
      ctx->api->error(ctx->lower, "invalid field assignment");
      return PIE_LOWER_ERROR;
    }
    PieIrExpr *target = NULL;
    if (ctx->api->lower_expr(ctx->lower, stmt->field_target->left, &target) !=
        PIE_LOWER_OK) {
      return PIE_LOWER_ERROR;
    }
    PieIrExpr *value = NULL;
    if (ctx->api->lower_expr(ctx->lower, stmt->expr, &value) != PIE_LOWER_OK) {
      pie_ir_expr_free(target);
      return PIE_LOWER_ERROR;
    }
    PieIrExpr *field =
        pie_ir_expr_field(target, stmt->field_target->field_name);
    if (target->struct_name && stmt->field_target->field_name) {
      const PieStructDef *def =
          ctx->api->find_struct(ctx->lower, target->struct_name);
      if (def) {
        int offset = 0;
        for (size_t j = 0; j < def->field_count; j++) {
          if (strcmp(def->fields[j].name, stmt->field_target->field_name) ==
              0) {
            field->field_offset = offset;
            PieAstType ast = def->fields[j].type;
            switch (ast.kind) {
            case PIE_AST_TYPE_INT:
              field->type = PIE_IR_TYPE_INT;
              break;
            case PIE_AST_TYPE_FLOAT:
              field->type = PIE_IR_TYPE_FLOAT;
              break;
            case PIE_AST_TYPE_STRING:
              field->type = PIE_IR_TYPE_STRING;
              break;
            case PIE_AST_TYPE_BOOL:
              field->type = PIE_IR_TYPE_BOOL;
              break;
            case PIE_AST_TYPE_CHAR:
              field->type = PIE_IR_TYPE_CHAR;
              break;
            case PIE_AST_TYPE_BYTE:
              field->type = PIE_IR_TYPE_BYTE;
              break;
            default:
              field->type = PIE_IR_TYPE_INT;
              break;
            }
            break;
          }
          offset += ir_ast_type_size(def->fields[j].type);
        }
      }
    }
    PieIrStmt ir_stmt;
    memset(&ir_stmt, 0, sizeof(ir_stmt));
    ir_stmt.kind = PIE_IR_STMT_FIELD_ASSIGN;
    ir_stmt.field_target = field;
    ir_stmt.expr = value;
    strncpy(ir_stmt.assign_op, stmt->assign_op, sizeof(ir_stmt.assign_op) - 1);
    if (!ctx->api->push_stmt(ctx->lower, ir_stmt)) {
      pie_ir_expr_free(field);
      pie_ir_expr_free(value);
      return PIE_LOWER_ERROR;
    }
    return PIE_LOWER_OK;
  }
  return PIE_LOWER_NO_MATCH;
}

static int ir_ast_type_size(PieAstType type) {
  switch (type.kind) {
  case PIE_AST_TYPE_STRING:
  case PIE_AST_TYPE_REF:
  case PIE_AST_TYPE_REF_MUT:
    return 16;
  case PIE_AST_TYPE_CLOSURE:
    return 16;
  case PIE_AST_TYPE_TUPLE:
    return 8 * (int)type.tuple_element_count;
  default:
    return 8;
  }
}

PieLowerResult pie_feature_structs_lower_expr(PieLowerContext *ctx,
                                              const PieExpr *expr,
                                              PieIrExpr **out_expr) {
  if (expr->kind == PIE_EXPR_NEW) {
    *out_expr = pie_ir_expr_new(expr->new_type_name);
    if (!*out_expr) {
      return PIE_LOWER_ERROR;
    }
    for (size_t i = 0; i < expr->call_arg_count; i++) {
      PieIrExpr *arg = NULL;
      if (ctx->api->lower_expr(ctx->lower, expr->call_args[i].expr->right,
                               &arg) != PIE_LOWER_OK) {
        pie_ir_expr_free(*out_expr);
        return PIE_LOWER_ERROR;
      }
      if (!pie_ir_expr_call_add_arg(*out_expr, arg)) {
        pie_ir_expr_free(arg);
        pie_ir_expr_free(*out_expr);
        return PIE_LOWER_ERROR;
      }
    }
    return PIE_LOWER_OK;
  }
  if (expr->kind == PIE_EXPR_FIELD) {
    if (expr->left && expr->left->kind == PIE_EXPR_VAR && expr->field_name) {
      const PieEnumDef *enum_def =
          ctx->api->find_enum(ctx->lower, expr->left->name);
      if (enum_def) {
        for (size_t i = 0; i < enum_def->variant_count; i++) {
          if (strcmp(enum_def->variants[i].name, expr->field_name) == 0) {
            *out_expr = pie_ir_expr_variant(expr->left->name, expr->field_name);
            if (!*out_expr) {
              return PIE_LOWER_ERROR;
            }
            (*out_expr)->variant_tag = (int)i;
            return PIE_LOWER_OK;
          }
        }
      }
    }

    PieIrExpr *object = NULL;
    if (ctx->api->lower_expr(ctx->lower, expr->left, &object) != PIE_LOWER_OK) {
      return PIE_LOWER_ERROR;
    }
    *out_expr = pie_ir_expr_field(object, expr->field_name);
    if (!*out_expr) {
      pie_ir_expr_free(object);
      return PIE_LOWER_ERROR;
    }
    if (object->type == PIE_IR_TYPE_TUPLE && expr->field_name) {
      long long idx = 0;
      for (const char *p = expr->field_name; *p >= '0' && *p <= '9'; p++) {
        idx = idx * 10 + (*p - '0');
      }
      int offset = 0;
      for (long long i = 0; i < idx; i++) {
        offset += 8;
      }
      (*out_expr)->field_offset = offset;
      if ((size_t)idx < object->tuple_element_count) {
        (*out_expr)->type = object->tuple_element_types[idx];
        (*out_expr)->type_width = object->tuple_element_widths[idx];
      }
    } else if (object->struct_name && expr->field_name) {
      const PieStructDef *def =
          ctx->api->find_struct(ctx->lower, object->struct_name);
      if (def) {
        int offset = 0;
        for (size_t j = 0; j < def->field_count; j++) {
          if (strcmp(def->fields[j].name, expr->field_name) == 0) {
            (*out_expr)->field_offset = offset;
            PieAstType ast = def->fields[j].type;
            switch (ast.kind) {
            case PIE_AST_TYPE_INT:
              (*out_expr)->type = PIE_IR_TYPE_INT;
              break;
            case PIE_AST_TYPE_FLOAT:
              (*out_expr)->type = PIE_IR_TYPE_FLOAT;
              break;
            case PIE_AST_TYPE_STRING:
              (*out_expr)->type = PIE_IR_TYPE_STRING;
              break;
            case PIE_AST_TYPE_BOOL:
              (*out_expr)->type = PIE_IR_TYPE_BOOL;
              break;
            case PIE_AST_TYPE_CHAR:
              (*out_expr)->type = PIE_IR_TYPE_CHAR;
              break;
            case PIE_AST_TYPE_BYTE:
              (*out_expr)->type = PIE_IR_TYPE_BYTE;
              break;
            case PIE_AST_TYPE_STRUCT:
              (*out_expr)->type = PIE_IR_TYPE_STRUCT;
              if (ast.struct_name) {
                (*out_expr)->struct_name = malloc(strlen(ast.struct_name) + 1);
                if ((*out_expr)->struct_name) {
                  strcpy((*out_expr)->struct_name, ast.struct_name);
                }
              }
              break;
            default:
              (*out_expr)->type = PIE_IR_TYPE_INT;
              break;
            }
            (*out_expr)->type_width = ast.width;
            break;
          }
          offset += ir_ast_type_size(def->fields[j].type);
        }
      }
    }
    return PIE_LOWER_OK;
  }
  return PIE_LOWER_NO_MATCH;
}
