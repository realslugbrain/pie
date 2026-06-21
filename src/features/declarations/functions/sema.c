#define _POSIX_C_SOURCE 200809L
#include "pie/core/sema/sema.h"
#include "pie/core/ast/ast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int satisfies_constraint(PieType type, const char *constraint) {
  if (!constraint)
    return 1;

  if (strcmp(constraint, "numeric") == 0) {
    return type.kind == PIE_TYPE_INT || type.kind == PIE_TYPE_FLOAT;
  }
  if (strcmp(constraint, "comparable") == 0) {
    return type.kind != PIE_TYPE_VOID && type.kind != PIE_TYPE_ERROR;
  }
  if (strcmp(constraint, "printable") == 0) {
    return type.kind != PIE_TYPE_VOID && type.kind != PIE_TYPE_ERROR;
  }
  if (strcmp(constraint, "integer") == 0) {
    return type.kind == PIE_TYPE_INT || type.kind == PIE_TYPE_BYTE;
  }
  if (strcmp(constraint, "string_like") == 0) {
    return type.kind == PIE_TYPE_STRING || type.kind == PIE_TYPE_CHAR;
  }

  return 1;
}

static char *mangle_generic_name(const char *func_name, PieType *concrete_types,
                                 size_t count) {
  size_t total_len = strlen(func_name) + 1;
  for (size_t i = 0; i < count; i++) {
    switch (concrete_types[i].kind) {
    case PIE_TYPE_INT:
      total_len += 3;
      break;
    case PIE_TYPE_FLOAT:
      total_len += 5;
      break;
    case PIE_TYPE_CHAR:
      total_len += 4;
      break;
    case PIE_TYPE_BYTE:
      total_len += 4;
      break;
    case PIE_TYPE_BOOL:
      total_len += 4;
      break;
    case PIE_TYPE_STRING:
      total_len += 6;
      break;
    default:
      total_len += 4;
      break;
    }
    if (i < count - 1)
      total_len += 1;
  }
  total_len++;

  char *result = (char *)malloc(total_len);
  if (!result)
    return NULL;

  char *p = result;
  strcpy(p, func_name);
  p += strlen(func_name);

  for (size_t i = 0; i < count; i++) {
    *p++ = '_';
    switch (concrete_types[i].kind) {
    case PIE_TYPE_INT:
      strcpy(p, "int");
      p += 3;
      break;
    case PIE_TYPE_FLOAT:
      strcpy(p, "float");
      p += 5;
      break;
    case PIE_TYPE_CHAR:
      strcpy(p, "char");
      p += 4;
      break;
    case PIE_TYPE_BYTE:
      strcpy(p, "byte");
      p += 4;
      break;
    case PIE_TYPE_BOOL:
      strcpy(p, "bool");
      p += 4;
      break;
    case PIE_TYPE_STRING:
      strcpy(p, "string");
      p += 6;
      break;
    default:
      strcpy(p, "type");
      p += 4;
      break;
    }
  }
  *p = '\0';
  return result;
}

static PieAstType pie_type_to_ast(PieType type) {
  PieAstType ast;
  memset(&ast, 0, sizeof(ast));
  switch (type.kind) {
  case PIE_TYPE_INT:
    ast.kind = PIE_AST_TYPE_INT;
    break;
  case PIE_TYPE_FLOAT:
    ast.kind = PIE_AST_TYPE_FLOAT;
    break;
  case PIE_TYPE_CHAR:
    ast.kind = PIE_AST_TYPE_CHAR;
    break;
  case PIE_TYPE_BYTE:
    ast.kind = PIE_AST_TYPE_BYTE;
    break;
  case PIE_TYPE_BOOL:
    ast.kind = PIE_AST_TYPE_BOOL;
    break;
  case PIE_TYPE_STRING:
    ast.kind = PIE_AST_TYPE_STRING;
    break;
  case PIE_TYPE_STRUCT:
    ast.kind = PIE_AST_TYPE_STRUCT;
    break;
  case PIE_TYPE_MAP:
    ast.kind = PIE_AST_TYPE_MAP;
    break;
  case PIE_TYPE_ENUM:
    ast.kind = PIE_AST_TYPE_ENUM;
    break;
  case PIE_TYPE_THREAD:
    ast.kind = PIE_AST_TYPE_THREAD;
    break;
  case PIE_TYPE_MUTEX:
    ast.kind = PIE_AST_TYPE_MUTEX;
    break;
  case PIE_TYPE_CHANNEL:
    ast.kind = PIE_AST_TYPE_CHANNEL;
    break;
  default:
    ast.kind = PIE_AST_TYPE_INFER;
    break;
  }
  ast.width = type.type_width;
  return ast;
}

static PieFunction *create_specialized_function(const PieFunction *original,
                                                const char *mangled_name,
                                                PieType *concrete_types,
                                                size_t type_param_count,
                                                const char **type_param_names) {
  PieFunction *spec = (PieFunction *)calloc(1, sizeof(PieFunction));
  if (!spec)
    return NULL;

  spec->name = strdup(mangled_name);
  spec->is_unsafe = original->is_unsafe;
  spec->is_pub = original->is_pub;
  spec->type_param_count = 0;
  spec->type_params = NULL;

  spec->param_count = original->param_count;
  if (spec->param_count) {
    spec->param_names = (char **)calloc(spec->param_count, sizeof(char *));
    spec->param_types =
        (PieAstType *)calloc(spec->param_count, sizeof(PieAstType));
    for (size_t i = 0; i < spec->param_count; i++) {
      spec->param_names[i] = strdup(original->param_names[i]);

      if (original->param_types[i].kind == PIE_AST_TYPE_STRUCT &&
          original->param_types[i].struct_name != NULL) {
        int found = 0;
        for (size_t t = 0; t < type_param_count; t++) {
          if (strcmp(original->param_types[i].struct_name,
                     type_param_names[t]) == 0) {
            spec->param_types[i] = pie_type_to_ast(concrete_types[t]);
            found = 1;
            break;
          }
        }
        if (!found) {
          spec->param_types[i] = original->param_types[i];
        }
      } else {
        spec->param_types[i] = original->param_types[i];
      }
    }
  }

  spec->return_type = original->return_type;
  if (original->return_type.kind == PIE_AST_TYPE_STRUCT &&
      original->return_type.struct_name != NULL) {
    for (size_t t = 0; t < type_param_count; t++) {
      if (strcmp(original->return_type.struct_name, type_param_names[t]) == 0) {
        spec->return_type = pie_type_to_ast(concrete_types[t]);
        break;
      }
    }
  }

  spec->body = original->body;
  spec->borrowed_body = 1;

  return spec;
}

static size_t abi_slots_for_type(PieType type) {
  if (type.kind == PIE_TYPE_STRING)
    return 2;
  if (type.kind == PIE_TYPE_REF && type.ref_inner_kind == PIE_TYPE_STRING)
    return 2;
  return 1;
}

static int compatible_width(PieTypeKind kind, int a, int b) {
  if (a == b) {
    return 1;
  }
  if ((kind == PIE_TYPE_INT || kind == PIE_TYPE_FLOAT) &&
      ((a == PIE_WIDTH_INFER && b == PIE_WIDTH_64) ||
       (a == PIE_WIDTH_64 && b == PIE_WIDTH_INFER))) {
    return 1;
  }
  return 0;
}

static int is_assignable(PieType target, PieType value) {
  if (target.kind == PIE_TYPE_RAW_PTR && value.kind == PIE_TYPE_RAW_PTR) {
    return target.raw_pointee_kind == value.raw_pointee_kind &&
           compatible_width(target.raw_pointee_kind, target.raw_pointee_width,
                            value.raw_pointee_width);
  }

  const char *target_name =
      (target.kind == PIE_TYPE_STRUCT)
          ? target.struct_name
          : (target.kind == PIE_TYPE_ENUM ? target.enum_name : NULL);
  const char *value_name =
      (value.kind == PIE_TYPE_STRUCT)
          ? value.struct_name
          : (value.kind == PIE_TYPE_ENUM ? value.enum_name : NULL);

  if (target_name && value_name) {
    return strcmp(target_name, value_name) == 0;
  }

  if (target.kind == value.kind) {
    return 1;
  }
  if (target.kind == PIE_TYPE_BYTE && value.kind == PIE_TYPE_INT) {
    return 1;
  }
  return target.kind == PIE_TYPE_REF && value.kind == PIE_TYPE_REF_MUT;
}

PieSemaResult pie_feature_functions_sema_stmt(PieSemaContext *ctx,
                                              const PieStmt *stmt) {
  if (stmt->kind == PIE_STMT_EXPR) {
    PieType type;
    if (ctx->api->check_expr(ctx->sema, stmt->expr, &type) != PIE_SEMA_OK) {
      return PIE_SEMA_ERROR;
    }
    return PIE_SEMA_OK;
  }

  if (stmt->kind != PIE_STMT_RETURN) {
    return PIE_SEMA_NO_MATCH;
  }

  PieType expected = ctx->api->current_return_type(ctx->sema);
  if (expected.kind == PIE_TYPE_VOID) {
    if (stmt->expr) {
      ctx->api->error(ctx->sema, "void function cannot return a value");
      return PIE_SEMA_ERROR;
    }
    return PIE_SEMA_OK;
  }

  if (stmt->expr) {
    PieType value_type;
    if (ctx->api->check_expr(ctx->sema, stmt->expr, &value_type) !=
        PIE_SEMA_OK) {
      return PIE_SEMA_ERROR;
    }
    if (!is_assignable(expected, value_type)) {
      ctx->api->errorf(ctx->sema, "return value must be %s, got %s",
                       ctx->api->type_name(expected),
                       ctx->api->type_name(value_type));
      return PIE_SEMA_ERROR;
    }
  } else {
    ctx->api->errorf(ctx->sema, "return value must be %s",
                     ctx->api->type_name(expected));
    return PIE_SEMA_ERROR;
  }
  return PIE_SEMA_OK;
}

PieSemaResult pie_feature_functions_sema_expr(PieSemaContext *ctx,
                                              const PieExpr *expr,
                                              PieType *out_type) {
  if (expr->kind != PIE_EXPR_CALL) {
    return PIE_SEMA_NO_MATCH;
  }

  if (strcmp(expr->call_name, "maybe") == 0) {
    if (expr->call_arg_count != 0) {
      ctx->api->errorf(ctx->sema,
                       "builtin 'maybe' expects 0 argument(s), got %zu",
                       expr->call_arg_count);
      out_type->kind = PIE_TYPE_ERROR;
      return PIE_SEMA_ERROR;
    }
    memset(out_type, 0, sizeof(*out_type));
    out_type->kind = PIE_TYPE_BOOL;
    out_type->type_width = PIE_WIDTH_INFER;
    return PIE_SEMA_OK;
  }

  if (strcmp(expr->call_name, "format") == 0) {
    if (expr->call_arg_count < 1) {
      ctx->api->errorf(ctx->sema,
                       "builtin 'format' expects at least 1 argument, got %zu",
                       expr->call_arg_count);
      out_type->kind = PIE_TYPE_ERROR;
      return PIE_SEMA_ERROR;
    }
    for (size_t i = 0; i < expr->call_arg_count; i++) {
      PieType arg_type;
      if (ctx->api->check_expr(ctx->sema, expr->call_args[i].expr, &arg_type) !=
          PIE_SEMA_OK) {
        out_type->kind = PIE_TYPE_ERROR;
        return PIE_SEMA_ERROR;
      }
    }
    memset(out_type, 0, sizeof(*out_type));
    out_type->kind = PIE_TYPE_STRING;
    out_type->type_width = PIE_WIDTH_INFER;
    return PIE_SEMA_OK;
  }

  PieFunctionInfo function;
  if (ctx->api->find_function(ctx->sema, expr->call_name, &function)) {
    if (function.is_unsafe && !ctx->api->in_unsafe(ctx->sema)) {
      ctx->api->errorf(ctx->sema,
                       "call to unsafe function '%s' requires unsafe",
                       expr->call_name);
      out_type->kind = PIE_TYPE_ERROR;
      return PIE_SEMA_ERROR;
    }

    if (expr->call_arg_count != function.param_count) {
      ctx->api->errorf(
          ctx->sema, "function '%s' expects %zu argument(s), got %zu",
          expr->call_name, function.param_count, expr->call_arg_count);
      out_type->kind = PIE_TYPE_ERROR;
      return PIE_SEMA_ERROR;
    }

    if (function.type_param_count > 0) {
      PieType concrete_types[16];
      const char *type_param_names[16];
      size_t type_param_count = function.type_param_count;

      if (type_param_count > 16) {
        ctx->api->errorf(ctx->sema, "too many type parameters (max 16)");
        out_type->kind = PIE_TYPE_ERROR;
        return PIE_SEMA_ERROR;
      }

      for (size_t i = 0; i < type_param_count; i++) {
        type_param_names[i] = function.type_param_names[i];
        memset(&concrete_types[i], 0, sizeof(PieType));
        concrete_types[i].kind = PIE_TYPE_ERROR;
      }

      for (size_t i = 0; i < function.param_count && i < expr->call_arg_count;
           i++) {
        PieType arg_type;
        if (ctx->api->check_expr(ctx->sema, expr->call_args[i].expr,
                                 &arg_type) != PIE_SEMA_OK) {
          out_type->kind = PIE_TYPE_ERROR;
          return PIE_SEMA_ERROR;
        }

        const PieType *param_type = &function.param_types[i];
        if (param_type->kind == PIE_TYPE_STRUCT &&
            param_type->struct_name != NULL) {
          for (size_t t = 0; t < type_param_count; t++) {
            if (strcmp(param_type->struct_name, type_param_names[t]) == 0) {
              if (concrete_types[t].kind != PIE_TYPE_ERROR) {
                if (!is_assignable(concrete_types[t], arg_type)) {
                  ctx->api->errorf(
                      ctx->sema,
                      "type parameter '%s' has conflicting types: %s vs %s",
                      type_param_names[t],
                      ctx->api->type_name(concrete_types[t]),
                      ctx->api->type_name(arg_type));
                  out_type->kind = PIE_TYPE_ERROR;
                  return PIE_SEMA_ERROR;
                }
              } else {
                concrete_types[t] = arg_type;
              }
              break;
            }
          }
        }
      }

      for (size_t t = 0; t < type_param_count; t++) {
        if (concrete_types[t].kind == PIE_TYPE_ERROR) {
          ctx->api->errorf(ctx->sema, "could not infer type parameter '%s'",
                           type_param_names[t]);
          out_type->kind = PIE_TYPE_ERROR;
          return PIE_SEMA_ERROR;
        }
      }

      {
        const PieProgram *prog = ctx->api->program(ctx->sema);
        if (prog) {
          for (size_t i = 0; i < prog->function_count; i++) {
            if (strcmp(prog->functions[i].name, expr->call_name) == 0) {
              const PieFunction *orig = &prog->functions[i];
              if (orig->type_param_constraints) {
                for (size_t t = 0; t < type_param_count; t++) {
                  if (orig->type_param_constraints[t]) {
                    if (!satisfies_constraint(
                            concrete_types[t],
                            orig->type_param_constraints[t])) {
                      ctx->api->errorf(ctx->sema,
                                       "type parameter '%s' does not satisfy "
                                       "constraint '%s' (got %s)",
                                       type_param_names[t],
                                       orig->type_param_constraints[t],
                                       ctx->api->type_name(concrete_types[t]));
                      out_type->kind = PIE_TYPE_ERROR;
                      return PIE_SEMA_ERROR;
                    }
                  }
                }
              }
              break;
            }
          }
        }
      }

      char *mangled_name = mangle_generic_name(expr->call_name, concrete_types,
                                               type_param_count);
      if (!mangled_name) {
        ctx->api->errorf(ctx->sema,
                         "out of memory while creating mangled name");
        out_type->kind = PIE_TYPE_ERROR;
        return PIE_SEMA_ERROR;
      }

      PieFunctionInfo existing;
      int already_exists =
          ctx->api->find_function(ctx->sema, mangled_name, &existing);

      if (!already_exists) {
        const PieProgram *program = ctx->api->program(ctx->sema);
        if (!program) {
          free(mangled_name);
          ctx->api->errorf(ctx->sema,
                           "no program available for monomorphization");
          out_type->kind = PIE_TYPE_ERROR;
          return PIE_SEMA_ERROR;
        }

        const PieFunction *original = NULL;
        for (size_t i = 0; i < program->function_count; i++) {
          if (strcmp(program->functions[i].name, expr->call_name) == 0) {
            original = &program->functions[i];
            break;
          }
        }

        if (!original) {
          free(mangled_name);
          ctx->api->errorf(ctx->sema, "original generic function not found");
          out_type->kind = PIE_TYPE_ERROR;
          return PIE_SEMA_ERROR;
        }

        PieFunction *spec =
            create_specialized_function(original, mangled_name, concrete_types,
                                        type_param_count, type_param_names);
        if (!spec) {
          free(mangled_name);
          ctx->api->errorf(ctx->sema,
                           "out of memory while creating specialized function");
          out_type->kind = PIE_TYPE_ERROR;
          return PIE_SEMA_ERROR;
        }

        if (!ctx->api->push_pending_mono(ctx->sema, *spec)) {
          free(spec->name);
          for (size_t i = 0; i < spec->param_count; i++) {
            free(spec->param_names[i]);
          }
          free(spec->param_names);
          free(spec->param_types);
          free(spec);
          free(mangled_name);
          ctx->api->errorf(ctx->sema,
                           "out of memory while adding specialized function");
          out_type->kind = PIE_TYPE_ERROR;
          return PIE_SEMA_ERROR;
        }

        {
          PieType concrete_ret;
          memset(&concrete_ret, 0, sizeof(concrete_ret));
          if (spec->return_type.kind == PIE_AST_TYPE_STRUCT &&
              spec->return_type.struct_name) {
            int found = 0;
            for (size_t t = 0; t < type_param_count; t++) {
              if (strcmp(spec->return_type.struct_name, type_param_names[t]) ==
                  0) {
                concrete_ret = concrete_types[t];
                found = 1;
                break;
              }
            }
            if (!found) {
              concrete_ret.kind = PIE_TYPE_STRUCT;
              concrete_ret.struct_name = strdup(spec->return_type.struct_name);
            }
          } else {
            switch (spec->return_type.kind) {
            case PIE_AST_TYPE_INT:
              concrete_ret.kind = PIE_TYPE_INT;
              break;
            case PIE_AST_TYPE_FLOAT:
              concrete_ret.kind = PIE_TYPE_FLOAT;
              break;
            case PIE_AST_TYPE_CHAR:
              concrete_ret.kind = PIE_TYPE_CHAR;
              break;
            case PIE_AST_TYPE_BYTE:
              concrete_ret.kind = PIE_TYPE_BYTE;
              break;
            case PIE_AST_TYPE_BOOL:
              concrete_ret.kind = PIE_TYPE_BOOL;
              break;
            case PIE_AST_TYPE_STRING:
              concrete_ret.kind = PIE_TYPE_STRING;
              break;
            default:
              concrete_ret.kind = PIE_TYPE_INT;
              break;
            }
          }

          PieType *concrete_param_types =
              (PieType *)calloc(spec->param_count, sizeof(PieType));
          for (size_t i = 0; i < spec->param_count; i++) {
            if (spec->param_types[i].kind == PIE_AST_TYPE_STRUCT &&
                spec->param_types[i].struct_name) {
              int found = 0;
              for (size_t t = 0; t < type_param_count; t++) {
                if (strcmp(spec->param_types[i].struct_name,
                           type_param_names[t]) == 0) {
                  concrete_param_types[i] = concrete_types[t];
                  found = 1;
                  break;
                }
              }
              if (!found) {
                concrete_param_types[i].kind = PIE_TYPE_STRUCT;
                concrete_param_types[i].struct_name =
                    strdup(spec->param_types[i].struct_name);
              }
            } else {
              switch (spec->param_types[i].kind) {
              case PIE_AST_TYPE_INT:
                concrete_param_types[i].kind = PIE_TYPE_INT;
                break;
              case PIE_AST_TYPE_FLOAT:
                concrete_param_types[i].kind = PIE_TYPE_FLOAT;
                break;
              case PIE_AST_TYPE_CHAR:
                concrete_param_types[i].kind = PIE_TYPE_CHAR;
                break;
              case PIE_AST_TYPE_BYTE:
                concrete_param_types[i].kind = PIE_TYPE_BYTE;
                break;
              case PIE_AST_TYPE_BOOL:
                concrete_param_types[i].kind = PIE_TYPE_BOOL;
                break;
              case PIE_AST_TYPE_STRING:
                concrete_param_types[i].kind = PIE_TYPE_STRING;
                break;
              default:
                concrete_param_types[i].kind = PIE_TYPE_INT;
                break;
              }
            }
          }
          ctx->api->register_mono_func(ctx->sema, mangled_name, concrete_ret,
                                       concrete_param_types, spec->param_count);
          free(concrete_param_types);
        }

        free(spec);
      }

      free(expr->call_name);
      ((PieExpr *)expr)->call_name = mangled_name;

      *out_type = function.return_type;
      if (out_type->kind == PIE_TYPE_STRUCT && out_type->struct_name != NULL) {
        for (size_t t = 0; t < type_param_count; t++) {
          if (strcmp(out_type->struct_name, type_param_names[t]) == 0) {
            *out_type = concrete_types[t];
            break;
          }
        }
      }
      return PIE_SEMA_OK;
    }

    size_t abi_slots = 0;
    for (size_t i = 0; i < function.param_count; i++) {
      abi_slots += abi_slots_for_type(function.param_types[i]);
    }
    if (abi_slots > 6) {
      ctx->api->errorf(
          ctx->sema,
          "function '%s' uses %zu linux x64 argument register slot(s); "
          "stack-passed arguments are not implemented yet (limit 6)",
          expr->call_name, abi_slots);
      out_type->kind = PIE_TYPE_ERROR;
      return PIE_SEMA_ERROR;
    }

    for (size_t i = 0; i < expr->call_arg_count; i++) {
      PieType arg_type;
      if (ctx->api->check_expr(ctx->sema, expr->call_args[i].expr, &arg_type) !=
          PIE_SEMA_OK) {
        out_type->kind = PIE_TYPE_ERROR;
        return PIE_SEMA_ERROR;
      }
      if (!is_assignable(function.param_types[i], arg_type)) {
        ctx->api->errorf(ctx->sema, "argument %zu of '%s' must be %s, got %s",
                         i + 1, expr->call_name,
                         ctx->api->type_name(function.param_types[i]),
                         ctx->api->type_name(arg_type));
        out_type->kind = PIE_TYPE_ERROR;
        return PIE_SEMA_ERROR;
      }
    }

    *out_type = function.return_type;
    return PIE_SEMA_OK;
  }

  PieSymbolInfo symbol;
  if (ctx->api->find_symbol(ctx->sema, expr->call_name, &symbol)) {
    if (symbol.type.kind == PIE_TYPE_CLOSURE) {
      if (expr->call_arg_count != symbol.type.func_param_count) {
        ctx->api->errorf(ctx->sema,
                         "closure '%s' expects %zu argument(s), got %zu",
                         expr->call_name, symbol.type.func_param_count,
                         expr->call_arg_count);
        out_type->kind = PIE_TYPE_ERROR;
        return PIE_SEMA_ERROR;
      }

      for (size_t i = 0; i < expr->call_arg_count; i++) {
        PieType arg_type;
        if (ctx->api->check_expr(ctx->sema, expr->call_args[i].expr,
                                 &arg_type) != PIE_SEMA_OK) {
          out_type->kind = PIE_TYPE_ERROR;
          return PIE_SEMA_ERROR;
        }
        PieType param_type;
        memset(&param_type, 0, sizeof(param_type));
        param_type.kind = symbol.type.func_param_kinds[i];
        param_type.type_width = symbol.type.func_param_widths[i];
        if (!is_assignable(param_type, arg_type)) {
          ctx->api->errorf(
              ctx->sema, "argument %zu of closure '%s' must be %s, got %s",
              i + 1, expr->call_name, ctx->api->type_name(param_type),
              ctx->api->type_name(arg_type));
          out_type->kind = PIE_TYPE_ERROR;
          return PIE_SEMA_ERROR;
        }
      }

      PieType ret_type;
      memset(&ret_type, 0, sizeof(ret_type));
      ret_type.kind = symbol.type.func_return_kind;
      ret_type.type_width = symbol.type.func_return_width;
      *out_type = ret_type;
      return PIE_SEMA_OK;
    }
  }

  ctx->api->errorf(ctx->sema, "undefined function '%s'", expr->call_name);
  out_type->kind = PIE_TYPE_ERROR;
  return PIE_SEMA_ERROR;
}
