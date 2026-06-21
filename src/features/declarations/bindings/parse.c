#include "pie/core/parser/parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PieParseResult pie_feature_bindings_parse_var_expr(PieParseContext *ctx,
                                                   PieExpr **out_expr) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  int is_self = api->check(parser, PIE_TOK_SELF);
  if ((!api->check(parser, PIE_TOK_IDENTIFIER) && !is_self) ||
      api->peek_n(parser, 1)->kind == PIE_TOK_LPAREN) {
    return PIE_PARSE_NO_MATCH;
  }

  const PieToken *token = api->advance(parser);
  char *name = api->copy_token_text(token);
  if (!name) {
    pie_diag_error(api->diag(parser), "out of memory while parsing identifier");
    return PIE_PARSE_ERROR;
  }

  *out_expr = pie_expr_var(name);
  free(name);
  if (!*out_expr) {
    pie_diag_error(api->diag(parser),
                   "out of memory while building identifier expression");
    return PIE_PARSE_ERROR;
  }
  return PIE_PARSE_OK;
}

static const char *assignment_op(PieTokenKind kind) {
  switch (kind) {
  case PIE_TOK_LARROW:
    return "<-";
  case PIE_TOK_PLUS_EQ:
    return "+=";
  case PIE_TOK_MINUS_EQ:
    return "-=";
  case PIE_TOK_STAR_EQ:
    return "*=";
  case PIE_TOK_SLASH_EQ:
    return "/=";
  case PIE_TOK_PERCENT_EQ:
    return "%=";
  case PIE_TOK_STAR_STAR_EQ:
    return "**=";
  default:
    return NULL;
  }
}

static PieAstType type_annotation_from_token(PieTokenKind kind) {
  switch (kind) {
  case PIE_TOK_INT_TYPE:
    return pie_ast_type_simple(PIE_AST_TYPE_INT);
  case PIE_TOK_FLOAT_TYPE:
    return pie_ast_type_simple(PIE_AST_TYPE_FLOAT);
  case PIE_TOK_CHAR_TYPE:
    return pie_ast_type_simple(PIE_AST_TYPE_CHAR);
  case PIE_TOK_BYTE_TYPE:
    return pie_ast_type_simple(PIE_AST_TYPE_BYTE);
  case PIE_TOK_BOOL_TYPE:
    return pie_ast_type_simple(PIE_AST_TYPE_BOOL);
  case PIE_TOK_STRING_TYPE:
    return pie_ast_type_simple(PIE_AST_TYPE_STRING);
  case PIE_TOK_VOID_TYPE:
    return pie_ast_type_simple(PIE_AST_TYPE_VOID);
  default:
    return pie_ast_type_simple(PIE_AST_TYPE_INFER);
  }
}

static int parse_type_width_suffix(PieParseContext *ctx, PieAstType *type) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->check(parser, PIE_TOK_LT)) {
    return 1;
  }

  api->advance(parser);
  if (api->check(parser, PIE_TOK_AUTO)) {
    api->advance(parser);
    type->width = PIE_WIDTH_AUTO;
  } else if (api->check(parser, PIE_TOK_INT_LITERAL)) {
    const PieToken *width_token = api->advance(parser);
    int w = (int)width_token->int_value;
    if (w == 8 || w == 16 || w == 32 || w == 64 || w == 128) {
      type->width = w;
    } else {
      api->error_at(parser, width_token,
                    "invalid width; expected 8, 16, 32, 64, or 128");
      type->kind = PIE_AST_TYPE_INFER;
      return 0;
    }
  } else if (api->check(parser, PIE_TOK_IDENTIFIER)) {
    const PieToken *name_token = api->peek(parser);
    if (name_token->len == 4 && memcmp(name_token->start, "wide", 4) == 0) {
      api->advance(parser);
      type->width = PIE_WIDTH_WIDE;
    } else {
      api->error_at(parser, name_token,
                    "expected auto, wide, or integer width after '<'");
      type->kind = PIE_AST_TYPE_INFER;
      return 0;
    }
  } else {
    api->error_at(parser, api->peek(parser), "expected width after '<'");
    type->kind = PIE_AST_TYPE_INFER;
    return 0;
  }

  if (!api->expect(parser, PIE_TOK_GT, "expected '>' after type width")) {
    type->kind = PIE_AST_TYPE_INFER;
    return 0;
  }
  return 1;
}

static PieAstType parse_type_annotation(PieParseContext *ctx) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (api->match(parser, PIE_TOK_QUESTION)) {
    PieAstType inner = parse_type_annotation(ctx);
    if (inner.kind == PIE_AST_TYPE_INFER) {
      api->error_at(parser, api->peek(parser), "expected type after '?'");
      return pie_ast_type_simple(PIE_AST_TYPE_INFER);
    }
    return pie_ast_type_nullable(inner.kind, inner.width);
  }

  if (api->match(parser, PIE_TOK_LPAREN)) {
    if (api->match(parser, PIE_TOK_RPAREN)) {
      if (api->check(parser, PIE_TOK_ARROW)) {
        api->advance(parser);
        PieAstType ret = parse_type_annotation(ctx);
        PieAstType func_type = pie_ast_type_simple(PIE_AST_TYPE_CLOSURE);
        func_type.func_param_count = 0;
        func_type.func_return_kind = ret.kind;
        func_type.func_return_width = ret.width;
        return func_type;
      }
      return pie_ast_type_simple(PIE_AST_TYPE_TUPLE);
    }

    PieAstType first = parse_type_annotation(ctx);
    if (first.kind == PIE_AST_TYPE_INFER) {
      api->error_at(parser, api->peek(parser), "expected type in tuple");
      return pie_ast_type_simple(PIE_AST_TYPE_INFER);
    }
    if (!api->match(parser, PIE_TOK_COMMA)) {
      if (!api->expect(parser, PIE_TOK_RPAREN, "expected ')' or ',' in type")) {
        return pie_ast_type_simple(PIE_AST_TYPE_INFER);
      }
      if (api->check(parser, PIE_TOK_ARROW)) {
        api->advance(parser);
        PieAstType ret = parse_type_annotation(ctx);
        PieAstType func_type = pie_ast_type_simple(PIE_AST_TYPE_CLOSURE);
        func_type.func_param_count = 1;
        func_type.func_param_kinds =
            (PieAstTypeKind *)malloc(sizeof(PieAstTypeKind));
        func_type.func_param_widths = (int *)malloc(sizeof(int));
        if (func_type.func_param_kinds && func_type.func_param_widths) {
          func_type.func_param_kinds[0] = first.kind;
          func_type.func_param_widths[0] = first.width;
        }
        func_type.func_return_kind = ret.kind;
        func_type.func_return_width = ret.width;
        return func_type;
      }
      return first;
    }
    PieAstType tuple_type = pie_ast_type_simple(PIE_AST_TYPE_TUPLE);
    tuple_type.tuple_element_kinds[0] = first.kind;
    tuple_type.tuple_element_widths[0] = first.width;
    tuple_type.tuple_element_count = 1;
    while (!api->check(parser, PIE_TOK_RPAREN)) {
      PieAstType elem = parse_type_annotation(ctx);
      if (elem.kind == PIE_AST_TYPE_INFER) {
        api->error_at(parser, api->peek(parser), "expected type in tuple");
        return pie_ast_type_simple(PIE_AST_TYPE_INFER);
      }
      if (tuple_type.tuple_element_count >= PIE_AST_TUPLE_MAX_ELEMENTS) {
        api->error_at(parser, api->peek(parser),
                      "tuple type too many elements");
        return pie_ast_type_simple(PIE_AST_TYPE_INFER);
      }
      tuple_type.tuple_element_kinds[tuple_type.tuple_element_count] =
          elem.kind;
      tuple_type.tuple_element_widths[tuple_type.tuple_element_count] =
          elem.width;
      tuple_type.tuple_element_count++;
      if (!api->match(parser, PIE_TOK_COMMA)) {
        break;
      }
    }
    if (!api->expect(parser, PIE_TOK_RPAREN, "expected ')' after tuple type")) {
      return pie_ast_type_simple(PIE_AST_TYPE_INFER);
    }

    if (api->check(parser, PIE_TOK_ARROW)) {
      PieAstType func_type = pie_ast_type_simple(PIE_AST_TYPE_CLOSURE);
      func_type.func_param_kinds = (PieAstTypeKind *)malloc(
          tuple_type.tuple_element_count * sizeof(PieAstTypeKind));
      func_type.func_param_widths =
          (int *)malloc(tuple_type.tuple_element_count * sizeof(int));
      if (!func_type.func_param_kinds || !func_type.func_param_widths) {
        free(func_type.func_param_kinds);
        free(func_type.func_param_widths);
        return pie_ast_type_simple(PIE_AST_TYPE_INFER);
      }
      func_type.func_param_count = tuple_type.tuple_element_count;
      for (size_t i = 0; i < tuple_type.tuple_element_count; i++) {
        func_type.func_param_kinds[i] = tuple_type.tuple_element_kinds[i];
        func_type.func_param_widths[i] = tuple_type.tuple_element_widths[i];
      }

      api->advance(parser);

      PieAstType ret = parse_type_annotation(ctx);
      func_type.func_return_kind = ret.kind;
      func_type.func_return_width = ret.width;
      return func_type;
    }

    return tuple_type;
  }

  if (api->match(parser, PIE_TOK_AMP)) {
    int is_mut_ref = api->match(parser, PIE_TOK_MUT);
    PieAstType inner = parse_type_annotation(ctx);
    if (inner.kind == PIE_AST_TYPE_INFER) {
      api->error_at(parser, api->peek(parser),
                    "expected type after reference type marker");
      return pie_ast_type_simple(PIE_AST_TYPE_INFER);
    }
    return pie_ast_type_ref(inner.kind, inner.width, is_mut_ref);
  }

  if (api->match(parser, PIE_TOK_STAR)) {
    PieAstType pointee = type_annotation_from_token(api->peek(parser)->kind);
    if (pointee.kind == PIE_AST_TYPE_INT ||
        pointee.kind == PIE_AST_TYPE_FLOAT ||
        pointee.kind == PIE_AST_TYPE_CHAR ||
        pointee.kind == PIE_AST_TYPE_BYTE ||
        pointee.kind == PIE_AST_TYPE_BOOL ||
        pointee.kind == PIE_AST_TYPE_STRING) {
      api->advance(parser);
      if (!parse_type_width_suffix(ctx, &pointee)) {
        return pie_ast_type_simple(PIE_AST_TYPE_INFER);
      }
      return pie_ast_type_raw_ptr(pointee.kind, pointee.width);
    }
    api->error_at(parser, api->peek(parser),
                  "expected primitive type after raw pointer marker");
    return pie_ast_type_simple(PIE_AST_TYPE_INFER);
  }

  if (api->check(parser, PIE_TOK_LIST_TYPE)) {
    api->advance(parser);
    if (!api->expect(parser, PIE_TOK_LPAREN, "expected '(' after 'list'")) {
      return pie_ast_type_simple(PIE_AST_TYPE_INFER);
    }
    PieAstType elem = parse_type_annotation(ctx);
    if (elem.kind == PIE_AST_TYPE_INFER) {
      api->error_at(parser, api->peek(parser), "expected type in list");
      return pie_ast_type_simple(PIE_AST_TYPE_INFER);
    }
    if (!api->expect(parser, PIE_TOK_RPAREN,
                     "expected ')' after list element type")) {
      return pie_ast_type_simple(PIE_AST_TYPE_INFER);
    }
    return pie_ast_type_list(elem.kind, elem.width);
  }

  if (api->check(parser, PIE_TOK_MAP_TYPE)) {
    api->advance(parser);
    if (api->check(parser, PIE_TOK_LBRACKET)) {
      api->advance(parser);
      PieAstType key_type = parse_type_annotation(ctx);
      if (key_type.kind == PIE_AST_TYPE_INFER) {
        api->error_at(parser, api->peek(parser), "expected key type in map");
        return pie_ast_type_simple(PIE_AST_TYPE_INFER);
      }
      if (!api->expect(parser, PIE_TOK_RBRACKET,
                       "expected ']' after map key type")) {
        return pie_ast_type_simple(PIE_AST_TYPE_INFER);
      }
      PieAstType value_type = parse_type_annotation(ctx);
      if (value_type.kind == PIE_AST_TYPE_INFER) {
        api->error_at(parser, api->peek(parser), "expected value type in map");
        return pie_ast_type_simple(PIE_AST_TYPE_INFER);
      }
      return pie_ast_type_map(key_type.kind, key_type.width, value_type.kind,
                              value_type.width);
    }
    return pie_ast_type_simple(PIE_AST_TYPE_MAP);
  }

  if (api->check(parser, PIE_TOK_IDENTIFIER)) {
    const PieToken *tok = api->peek(parser);
    int is_option = (tok->len == 6 && memcmp(tok->start, "Option", 6) == 0);
    int is_result = (tok->len == 6 && memcmp(tok->start, "Result", 6) == 0);
    if (is_option || is_result) {
      char *type_name = api->copy_token_text(tok);
      api->advance(parser);
      PieAstType t;
      memset(&t, 0, sizeof(t));
      t.kind = PIE_AST_TYPE_ENUM;
      t.enum_name = type_name;
      if (api->match(parser, PIE_TOK_LPAREN)) {
        while (!api->check(parser, PIE_TOK_RPAREN) &&
               !api->check(parser, PIE_TOK_EOF)) {
          if (t.enum_type_param_count > 0) {
            if (!api->match(parser, PIE_TOK_COMMA)) {
              api->error_at(parser, api->peek(parser),
                            "expected ',' between type parameters");
              free(type_name);
              return pie_ast_type_simple(PIE_AST_TYPE_INFER);
            }
          }
          PieAstType param_type =
              type_annotation_from_token(api->peek(parser)->kind);
          if (param_type.kind != PIE_AST_TYPE_INFER) {
            api->advance(parser);
            if (t.enum_type_param_count < 8) {
              t.enum_type_param_kinds[t.enum_type_param_count] =
                  param_type.kind;
              t.enum_type_param_widths[t.enum_type_param_count] =
                  param_type.width;
              t.enum_type_param_count++;
            }
          } else if (api->check(parser, PIE_TOK_IDENTIFIER)) {
            const PieToken *pt = api->peek(parser);
            if (pt->len == 6 && memcmp(pt->start, "string", 6) == 0) {
              api->advance(parser);
              if (t.enum_type_param_count < 8) {
                t.enum_type_param_kinds[t.enum_type_param_count] =
                    PIE_AST_TYPE_STRING;
                t.enum_type_param_widths[t.enum_type_param_count] = 0;
                t.enum_type_param_count++;
              }
            } else if (pt->len == 3 && memcmp(pt->start, "int", 3) == 0) {
              api->advance(parser);
              if (t.enum_type_param_count < 8) {
                t.enum_type_param_kinds[t.enum_type_param_count] =
                    PIE_AST_TYPE_INT;
                t.enum_type_param_widths[t.enum_type_param_count] = 0;
                t.enum_type_param_count++;
              }
            } else if (pt->len == 4 && memcmp(pt->start, "void", 4) == 0) {
              api->advance(parser);
              if (t.enum_type_param_count < 8) {
                t.enum_type_param_kinds[t.enum_type_param_count] =
                    PIE_AST_TYPE_VOID;
                t.enum_type_param_widths[t.enum_type_param_count] = 0;
                t.enum_type_param_count++;
              }
            } else {
              api->advance(parser);
              if (t.enum_type_param_count < 8) {
                t.enum_type_param_kinds[t.enum_type_param_count] =
                    PIE_AST_TYPE_ENUM;
                t.enum_type_param_widths[t.enum_type_param_count] = 0;
                t.enum_type_param_count++;
              }
            }
          } else {
            api->advance(parser);
          }
        }
        if (!api->expect(parser, PIE_TOK_RPAREN,
                         "expected ')' after type parameters")) {
          free(type_name);
          return pie_ast_type_simple(PIE_AST_TYPE_INFER);
        }
      }
      return t;
    }
  }

  PieAstType type = type_annotation_from_token(api->peek(parser)->kind);
  if (type.kind != PIE_AST_TYPE_INFER) {
    api->advance(parser);
    parse_type_width_suffix(ctx, &type);
    return type;
  }

  if (api->check(parser, PIE_TOK_IDENTIFIER)) {
    const PieToken *tok = api->advance(parser);
    type.kind = PIE_AST_TYPE_STRUCT;
    type.struct_name = api->copy_token_text(tok);
    return type;
  }

  return type;
}

static int parse_expr_list(PieParseContext *ctx, PieExpr ***out_exprs,
                           size_t *out_count) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  size_t cap = 8;
  size_t count = 0;
  PieExpr **exprs = (PieExpr **)malloc(cap * sizeof(PieExpr *));
  if (!exprs) {
    pie_diag_error(api->diag(parser), "out of memory");
    return 0;
  }

  PieExpr *first = api->parse_expr(parser);
  if (!first) {
    free(exprs);
    return 0;
  }
  exprs[count++] = first;

  while (api->match(parser, PIE_TOK_COMMA)) {
    PieExpr *e = api->parse_expr(parser);
    if (!e) {
      for (size_t i = 0; i < count; i++)
        pie_expr_free(exprs[i]);
      free(exprs);
      return 0;
    }
    if (count == cap) {
      cap *= 2;
      PieExpr **next = (PieExpr **)realloc(exprs, cap * sizeof(PieExpr *));
      if (!next) {
        pie_expr_free(e);
        for (size_t i = 0; i < count; i++)
          pie_expr_free(exprs[i]);
        free(exprs);
        return 0;
      }
      exprs = next;
    }
    exprs[count++] = e;
  }

  *out_exprs = exprs;
  *out_count = count;
  return 1;
}

static int parse_name_list(PieParseContext *ctx, char ***out_names,
                           size_t *out_count) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  size_t cap = 8;
  size_t count = 0;
  char **names = (char **)malloc(cap * sizeof(char *));
  if (!names) {
    pie_diag_error(api->diag(parser), "out of memory");
    return 0;
  }

  const PieToken *first = api->advance(parser);
  names[count++] = api->copy_token_text(first);

  while (api->match(parser, PIE_TOK_COMMA)) {
    const PieToken *t = api->advance(parser);
    if (count == cap) {
      cap *= 2;
      char **next = (char **)realloc(names, cap * sizeof(char *));
      if (!next) {
        for (size_t i = 0; i < count; i++)
          free(names[i]);
        free(names);
        return 0;
      }
      names = next;
    }
    names[count++] = api->copy_token_text(t);
  }

  *out_names = names;
  *out_count = count;
  return 1;
}

static void free_name_list(char **names, size_t count) {
  for (size_t i = 0; i < count; i++)
    free(names[i]);
  free(names);
}

static void free_expr_list(PieExpr **exprs, size_t count) {
  for (size_t i = 0; i < count; i++)
    pie_expr_free(exprs[i]);
  free(exprs);
}

PieParseResult pie_feature_bindings_parse_stmt(PieParseContext *ctx,
                                               PieProgram *program) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  int is_mut = 0;
  if (api->match(parser, PIE_TOK_LET)) {
  }
  if (api->match(parser, PIE_TOK_MUT)) {
    is_mut = 1;
  }

  if (api->check(parser, PIE_TOK_LPAREN)) {
    api->advance(parser);

    size_t name_count = 0;
    char **names = NULL;
    if (!parse_name_list(ctx, &names, &name_count)) {
      return PIE_PARSE_ERROR;
    }

    if (!api->expect(parser, PIE_TOK_RPAREN, "expected ')' after names")) {
      free_name_list(names, name_count);
      return PIE_PARSE_ERROR;
    }

    PieAstType tuple_type = pie_ast_type_simple(PIE_AST_TYPE_TUPLE);
    if (api->match(parser, PIE_TOK_COLON)) {
      if (!api->expect(parser, PIE_TOK_LPAREN, "expected '(' for type tuple")) {
        free_name_list(names, name_count);
        return PIE_PARSE_ERROR;
      }
      PieAstType first = parse_type_annotation(ctx);
      tuple_type.tuple_element_kinds[0] = first.kind;
      tuple_type.tuple_element_widths[0] = first.width;
      tuple_type.tuple_element_count = 1;
      while (api->match(parser, PIE_TOK_COMMA)) {
        PieAstType elem = parse_type_annotation(ctx);
        if (tuple_type.tuple_element_count >= PIE_AST_TUPLE_MAX_ELEMENTS) {
          api->error_at(parser, api->peek(parser),
                        "tuple type too many elements");
          free_name_list(names, name_count);
          return PIE_PARSE_ERROR;
        }
        tuple_type.tuple_element_kinds[tuple_type.tuple_element_count] =
            elem.kind;
        tuple_type.tuple_element_widths[tuple_type.tuple_element_count] =
            elem.width;
        tuple_type.tuple_element_count++;
      }
      if (!api->expect(parser, PIE_TOK_RPAREN,
                       "expected ')' after type tuple")) {
        free_name_list(names, name_count);
        return PIE_PARSE_ERROR;
      }
    }

    if (!api->expect(parser, PIE_TOK_ARROW, "expected '->'")) {
      free_name_list(names, name_count);
      return PIE_PARSE_ERROR;
    }

    PieExpr **values = NULL;
    size_t value_count = 0;
    if (!api->expect(parser, PIE_TOK_LPAREN, "expected '(' for values")) {
      free_name_list(names, name_count);
      return PIE_PARSE_ERROR;
    }
    if (!parse_expr_list(ctx, &values, &value_count)) {
      free_name_list(names, name_count);
      return PIE_PARSE_ERROR;
    }
    if (!api->expect(parser, PIE_TOK_RPAREN, "expected ')' after values")) {
      free_name_list(names, name_count);
      free_expr_list(values, value_count);
      return PIE_PARSE_ERROR;
    }

    if (value_count != name_count) {
      pie_diag_errorf(api->diag(parser),
                      "multi-let: expected %zu values, got %zu", name_count,
                      value_count);
      free_name_list(names, name_count);
      free_expr_list(values, value_count);
      return PIE_PARSE_ERROR;
    }

    if (tuple_type.tuple_element_count == name_count) {
      for (size_t i = 0; i < name_count; i++) {
        PieStmt stmt;
        memset(&stmt, 0, sizeof(stmt));
        stmt.kind = PIE_STMT_LET;
        stmt.is_mut = is_mut;
        stmt.type_annotation =
            pie_ast_type_simple(tuple_type.tuple_element_kinds[i]);
        stmt.type_annotation.width = tuple_type.tuple_element_widths[i];
        stmt.name = names[i];
        stmt.expr = values[i];
        if (!pie_program_push_stmt(program, stmt)) {
          free_expr_list(values + i + 1, value_count - i - 1);
          free(values[i]);
          free_name_list(names, name_count);
          return PIE_PARSE_ERROR;
        }
      }
    } else {
      for (size_t i = 0; i < name_count; i++) {
        PieStmt stmt;
        memset(&stmt, 0, sizeof(stmt));
        stmt.kind = PIE_STMT_LET;
        stmt.is_mut = is_mut;
        stmt.type_annotation = pie_ast_type_simple(PIE_AST_TYPE_INFER);
        stmt.name = names[i];
        stmt.expr = values[i];
        if (!pie_program_push_stmt(program, stmt)) {
          free_expr_list(values + i + 1, value_count - i - 1);
          free(values[i]);
          free_name_list(names, name_count);
          return PIE_PARSE_ERROR;
        }
      }
    }

    free(names);
    free(values);
    return PIE_PARSE_OK;
  }

  if (api->check(parser, PIE_TOK_IDENTIFIER)) {
    const PieToken *peek1 = api->peek_n(parser, 1);

    if (peek1->kind == PIE_TOK_LARROW || peek1->kind == PIE_TOK_PLUS_EQ ||
        peek1->kind == PIE_TOK_MINUS_EQ || peek1->kind == PIE_TOK_STAR_EQ ||
        peek1->kind == PIE_TOK_SLASH_EQ || peek1->kind == PIE_TOK_PERCENT_EQ ||
        peek1->kind == PIE_TOK_STAR_STAR_EQ) {
      return PIE_PARSE_NO_MATCH;
    }

    if (peek1->kind == PIE_TOK_COLON) {
      const PieToken *name_token = api->peek(parser);
      api->advance(parser);
      api->advance(parser);

      PieAstType type_annotation = parse_type_annotation(ctx);
      if (type_annotation.kind == PIE_AST_TYPE_INFER) {
        api->error_at(parser, api->peek(parser),
                      "expected type annotation after ':'");
        return PIE_PARSE_ERROR;
      }
      if (type_annotation.kind == PIE_AST_TYPE_VOID) {
        api->error_at(parser, api->peek(parser),
                      "bindings cannot have type void");
        return PIE_PARSE_ERROR;
      }

      if (api->check(parser, PIE_TOK_LARROW)) {
        api->advance(parser);
        PieExpr *expr = api->parse_expr(parser);
        if (!expr)
          return PIE_PARSE_ERROR;

        PieStmt stmt;
        memset(&stmt, 0, sizeof(stmt));
        stmt.kind = PIE_STMT_LET;
        stmt.is_mut = is_mut;
        stmt.type_annotation = type_annotation;
        stmt.name = api->copy_token_text(name_token);
        stmt.expr = expr;
        if (!stmt.name) {
          pie_expr_free(expr);
          return PIE_PARSE_ERROR;
        }
        if (!pie_program_push_stmt(program, stmt)) {
          free(stmt.name);
          pie_expr_free(expr);
          return PIE_PARSE_ERROR;
        }
        return PIE_PARSE_OK;
      }

      if (!api->expect(parser, PIE_TOK_ARROW, "expected '->' after type")) {
        return PIE_PARSE_ERROR;
      }

      PieExpr *expr = api->parse_expr(parser);
      if (!expr)
        return PIE_PARSE_ERROR;

      PieStmt stmt;
      memset(&stmt, 0, sizeof(stmt));
      stmt.kind = PIE_STMT_LET;
      stmt.is_mut = is_mut;
      stmt.type_annotation = type_annotation;
      stmt.name = api->copy_token_text(name_token);
      stmt.expr = expr;
      if (!stmt.name) {
        pie_expr_free(expr);
        return PIE_PARSE_ERROR;
      }
      if (!pie_program_push_stmt(program, stmt)) {
        free(stmt.name);
        pie_expr_free(expr);
        return PIE_PARSE_ERROR;
      }
      return PIE_PARSE_OK;
    }

    if (peek1->kind == PIE_TOK_ARROW) {
      const PieToken *name_token = api->peek(parser);
      api->advance(parser);
      api->advance(parser);

      PieExpr *expr = api->parse_expr(parser);
      if (!expr)
        return PIE_PARSE_ERROR;

      PieStmt stmt;
      memset(&stmt, 0, sizeof(stmt));
      stmt.kind = PIE_STMT_LET;
      stmt.is_mut = is_mut;
      stmt.type_annotation = pie_ast_type_simple(PIE_AST_TYPE_INFER);
      stmt.name = api->copy_token_text(name_token);
      stmt.expr = expr;
      if (!stmt.name) {
        pie_expr_free(expr);
        return PIE_PARSE_ERROR;
      }
      if (!pie_program_push_stmt(program, stmt)) {
        free(stmt.name);
        pie_expr_free(expr);
        return PIE_PARSE_ERROR;
      }
      return PIE_PARSE_OK;
    }
  }

  return PIE_PARSE_NO_MATCH;
}

PieParseResult pie_feature_bindings_parse_assign_stmt(PieParseContext *ctx,
                                                      PieProgram *program) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->check(parser, PIE_TOK_IDENTIFIER)) {
    return PIE_PARSE_NO_MATCH;
  }

  if (api->peek_n(parser, 1)->kind == PIE_TOK_LARROW) {
    const PieToken *name_token = api->advance(parser);
    api->advance(parser);

    PieExpr *expr = api->parse_expr(parser);
    if (!expr)
      return PIE_PARSE_ERROR;

    PieStmt stmt;
    memset(&stmt, 0, sizeof(stmt));
    stmt.kind = PIE_STMT_ASSIGN;
    stmt.name = api->copy_token_text(name_token);
    stmt.expr = expr;
    strncpy(stmt.assign_op, "<-", sizeof(stmt.assign_op) - 1);
    if (!stmt.name) {
      pie_expr_free(expr);
      return PIE_PARSE_ERROR;
    }
    if (!pie_program_push_stmt(program, stmt)) {
      free(stmt.name);
      pie_expr_free(expr);
      return PIE_PARSE_ERROR;
    }
    return PIE_PARSE_OK;
  }

  {
    PieTokenKind op_kind = api->peek_n(parser, 1)->kind;
    if (op_kind != PIE_TOK_LARROW) {
      const char *op_str = assignment_op(op_kind);
      if (op_str) {
        const PieToken *name_token = api->advance(parser);
        api->advance(parser);

        PieExpr *expr = api->parse_expr(parser);
        if (!expr)
          return PIE_PARSE_ERROR;

        PieStmt stmt;
        memset(&stmt, 0, sizeof(stmt));
        stmt.kind = PIE_STMT_ASSIGN;
        stmt.name = api->copy_token_text(name_token);
        stmt.expr = expr;
        strncpy(stmt.assign_op, op_str, sizeof(stmt.assign_op) - 1);
        if (!stmt.name) {
          pie_expr_free(expr);
          return PIE_PARSE_ERROR;
        }
        if (!pie_program_push_stmt(program, stmt)) {
          free(stmt.name);
          pie_expr_free(expr);
          return PIE_PARSE_ERROR;
        }
        return PIE_PARSE_OK;
      }
    }
  }

  if (api->peek_n(parser, 1)->kind == PIE_TOK_COMMA) {
    size_t name_cap = 8;
    size_t name_count = 0;
    char **names = (char **)malloc(name_cap * sizeof(char *));
    if (!names)
      return PIE_PARSE_ERROR;

    while (1) {
      const PieToken *t = api->advance(parser);
      if (name_count == name_cap) {
        name_cap *= 2;
        char **next = (char **)realloc(names, name_cap * sizeof(char *));
        if (!next) {
          free_name_list(names, name_count);
          return PIE_PARSE_ERROR;
        }
        names = next;
      }
      names[name_count++] = api->copy_token_text(t);
      if (!api->match(parser, PIE_TOK_COMMA))
        break;
    }

    if (!api->expect(parser, PIE_TOK_LARROW,
                     "expected '<-' in multi-assignment")) {
      free_name_list(names, name_count);
      return PIE_PARSE_ERROR;
    }

    size_t expr_cap = 8;
    size_t expr_count = 0;
    PieExpr **exprs = (PieExpr **)malloc(expr_cap * sizeof(PieExpr *));
    if (!exprs) {
      free_name_list(names, name_count);
      return PIE_PARSE_ERROR;
    }

    size_t rhs_start = api->pos(parser);
    size_t rhs_end = rhs_start;
    {
      int depth = 0;
      for (size_t p = rhs_start;; p++) {
        api->set_pos(parser, p);
        PieTokenKind k = api->peek(parser)->kind;
        if (k == PIE_TOK_EOF) {
          rhs_end = p;
          break;
        }
        if (k == PIE_TOK_LPAREN)
          depth++;
        else if (k == PIE_TOK_RPAREN && depth > 0)
          depth--;
        else if (k == PIE_TOK_NEWLINE && depth == 0) {
          rhs_end = p;
          break;
        } else if (k == PIE_TOK_END && depth == 0) {
          rhs_end = p;
          break;
        }
      }
    }
    api->set_pos(parser, rhs_start);

    while (api->pos(parser) < rhs_end) {
      size_t seg_start = api->pos(parser);
      int depth = 0;
      size_t seg_end = rhs_end;
      for (size_t p = seg_start; p < rhs_end; p++) {
        api->set_pos(parser, p);
        PieTokenKind k = api->peek(parser)->kind;
        if (k == PIE_TOK_LPAREN)
          depth++;
        else if (k == PIE_TOK_RPAREN)
          depth--;
        else if (k == PIE_TOK_COMMA && depth == 0) {
          seg_end = p;
          break;
        }
      }
      api->set_pos(parser, seg_start);
      PieExpr *e = api->parse_expr_until(parser, seg_end);
      if (!e) {
        free_expr_list(exprs, expr_count);
        free_name_list(names, name_count);
        return PIE_PARSE_ERROR;
      }
      if (expr_count == expr_cap) {
        expr_cap *= 2;
        PieExpr **next =
            (PieExpr **)realloc(exprs, expr_cap * sizeof(PieExpr *));
        if (!next) {
          pie_expr_free(e);
          free_expr_list(exprs, expr_count);
          free_name_list(names, name_count);
          return PIE_PARSE_ERROR;
        }
        exprs = next;
      }
      exprs[expr_count++] = e;
      if (api->pos(parser) < rhs_end &&
          api->peek(parser)->kind == PIE_TOK_COMMA) {
        api->advance(parser);
      } else {
        break;
      }
    }

    if (expr_count != name_count) {
      pie_diag_errorf(api->diag(parser),
                      "multi-assignment: expected %zu expressions, got %zu",
                      name_count, expr_count);
      free_expr_list(exprs, expr_count);
      free_name_list(names, name_count);
      return PIE_PARSE_ERROR;
    }

    PieStmt stmt;
    memset(&stmt, 0, sizeof(stmt));
    stmt.kind = PIE_STMT_ASSIGN_MULTI;
    stmt.multi_names = names;
    stmt.multi_exprs = exprs;
    stmt.multi_count = expr_count;
    if (!pie_program_push_stmt(program, stmt)) {
      free_expr_list(exprs, expr_count);
      free_name_list(names, name_count);
      return PIE_PARSE_ERROR;
    }
    return PIE_PARSE_OK;
  }

  return PIE_PARSE_NO_MATCH;
}
