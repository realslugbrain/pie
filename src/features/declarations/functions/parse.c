#define _POSIX_C_SOURCE 200809L
#include "pie/core/parser/parser.h"

#include <stdlib.h>
#include <string.h>

static int token_text_eq(const PieToken *token, const char *text) {
  size_t len = strlen(text);
  return token->len == len && memcmp(token->start, text, len) == 0;
}

static PieAstType type_from_token(PieTokenKind kind, int allow_void) {
  switch (kind) {
  case PIE_TOK_VOID_TYPE:
    return allow_void ? pie_ast_type_simple(PIE_AST_TYPE_VOID)
                      : pie_ast_type_simple(PIE_AST_TYPE_INFER);
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

static PieAstType parse_type(PieParseContext *ctx, int allow_void) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (api->match(parser, PIE_TOK_QUESTION)) {
    PieAstType inner = parse_type(ctx, 0);
    if (inner.kind == PIE_AST_TYPE_INFER) {
      api->error_at(parser, api->peek(parser), "expected type after '?'");
      return pie_ast_type_simple(PIE_AST_TYPE_INFER);
    }
    return pie_ast_type_nullable(inner.kind, inner.width);
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
          PieAstType param_type = parse_type(ctx, 0);
          if (param_type.kind != PIE_AST_TYPE_INFER &&
              t.enum_type_param_count < 8) {
            t.enum_type_param_kinds[t.enum_type_param_count] = param_type.kind;
            t.enum_type_param_widths[t.enum_type_param_count] =
                param_type.width;
            t.enum_type_param_count++;
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

  if (api->match(parser, PIE_TOK_LPAREN)) {
    if (api->match(parser, PIE_TOK_RPAREN)) {
      if (api->check(parser, PIE_TOK_FAT_ARROW)) {
        api->advance(parser);
        PieAstType ret = parse_type(ctx, 0);
        PieAstType func_type = pie_ast_type_simple(PIE_AST_TYPE_CLOSURE);
        func_type.func_param_count = 0;
        func_type.func_return_kind = ret.kind;
        func_type.func_return_width = ret.width;
        return func_type;
      }
      return pie_ast_type_simple(PIE_AST_TYPE_TUPLE);
    }

    PieAstType first = parse_type(ctx, 0);
    if (first.kind == PIE_AST_TYPE_INFER) {
      api->error_at(parser, api->peek(parser), "expected type in tuple");
      return pie_ast_type_simple(PIE_AST_TYPE_INFER);
    }
    if (!api->match(parser, PIE_TOK_COMMA)) {
      if (!api->expect(parser, PIE_TOK_RPAREN, "expected ')' or ',' in type")) {
        return pie_ast_type_simple(PIE_AST_TYPE_INFER);
      }

      if (api->check(parser, PIE_TOK_FAT_ARROW)) {
        PieAstType func_type = pie_ast_type_simple(PIE_AST_TYPE_CLOSURE);
        func_type.func_param_kinds =
            (PieAstTypeKind *)malloc(1 * sizeof(PieAstTypeKind));
        func_type.func_param_widths = (int *)malloc(1 * sizeof(int));
        if (!func_type.func_param_kinds || !func_type.func_param_widths) {
          free(func_type.func_param_kinds);
          free(func_type.func_param_widths);
          return pie_ast_type_simple(PIE_AST_TYPE_INFER);
        }
        func_type.func_param_count = 1;
        func_type.func_param_kinds[0] = first.kind;
        func_type.func_param_widths[0] = first.width;

        api->advance(parser);

        PieAstType ret = parse_type(ctx, 0);
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
      PieAstType elem = parse_type(ctx, 0);
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

    if (api->check(parser, PIE_TOK_FAT_ARROW)) {
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

      PieAstType ret = parse_type(ctx, 0);
      func_type.func_return_kind = ret.kind;
      func_type.func_return_width = ret.width;
      return func_type;
    }

    return tuple_type;
  }

  if (api->match(parser, PIE_TOK_AMP)) {
    int is_mut_ref = api->match(parser, PIE_TOK_MUT);
    PieAstType inner = parse_type(ctx, 0);
    if (inner.kind == PIE_AST_TYPE_INFER) {
      api->error_at(parser, api->peek(parser),
                    "expected type after reference type marker");
      return pie_ast_type_simple(PIE_AST_TYPE_INFER);
    }
    PieAstType ref_type = pie_ast_type_ref(inner.kind, inner.width, is_mut_ref);
    if (inner.kind == PIE_AST_TYPE_STRUCT && inner.struct_name) {
      ref_type.ref_inner_struct_name = strdup(inner.struct_name);
    }
    return ref_type;
  }

  if (api->match(parser, PIE_TOK_STAR)) {
    PieAstType pointee = type_from_token(api->peek(parser)->kind, 0);
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
    PieAstType elem = parse_type(ctx, 0);
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

  PieAstType type = type_from_token(api->peek(parser)->kind, allow_void);
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

static char *copy_token_text_or_diag(PieParseContext *ctx,
                                     const PieToken *token) {
  char *text = ctx->api->copy_token_text(token);
  if (!text) {
    pie_diag_error(ctx->api->diag(ctx->parser),
                   "out of memory while copying token text");
  }
  return text;
}

static void free_function_params(PieFunction *function) {
  for (size_t i = 0; i < function->param_count; i++) {
    free(function->param_names[i]);
  }
  free(function->param_names);
  free(function->param_types);
  function->param_names = NULL;
  function->param_types = NULL;
  function->param_count = 0;

  for (size_t i = 0; i < function->type_param_count; i++) {
    free(function->type_params[i]);
  }
  free(function->type_params);
  function->type_params = NULL;
  function->type_param_count = 0;

  if (function->type_param_constraints) {
    for (size_t i = 0; i < function->type_param_count; i++) {
      free(function->type_param_constraints[i]);
    }
    free(function->type_param_constraints);
    function->type_param_constraints = NULL;
  }
}

static int push_function_param(PieParseContext *ctx, PieFunction *function,
                               char *name, PieAstType type) {
  char **next_names = (char **)realloc(
      function->param_names, (function->param_count + 1) * sizeof(char *));
  if (!next_names) {
    pie_diag_error(ctx->api->diag(ctx->parser),
                   "out of memory while storing function parameter");
    return 0;
  }
  function->param_names = next_names;

  PieAstType *next_types = (PieAstType *)realloc(
      function->param_types, (function->param_count + 1) * sizeof(PieAstType));
  if (!next_types) {
    pie_diag_error(ctx->api->diag(ctx->parser),
                   "out of memory while storing function parameter type");
    return 0;
  }
  function->param_types = next_types;
  function->param_names[function->param_count] = name;
  function->param_types[function->param_count] = type;
  function->param_count++;
  return 1;
}

static int parse_function_params(PieParseContext *ctx, PieFunction *function) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (api->match(parser, PIE_TOK_RPAREN)) {
    return 1;
  }

  for (;;) {
    const PieToken *name_token = api->peek(parser);
    if (api->check(parser, PIE_TOK_SELF)) {
      api->advance(parser);
      if (!api->expect(parser, PIE_TOK_COLON, "expected ':' after 'self'")) {
        return 0;
      }
      PieAstType param_type = parse_type(ctx, 0);
      if (param_type.kind == PIE_AST_TYPE_INFER) {
        api->error_at(parser, api->peek(parser),
                      "expected type for self parameter");
        return 0;
      }
      char *name = copy_token_text_or_diag(ctx, name_token);
      if (!name) {
        return 0;
      }
      if (!push_function_param(ctx, function, name, param_type)) {
        free(name);
        return 0;
      }
    } else {
      if (!api->expect(parser, PIE_TOK_IDENTIFIER, "expected parameter name")) {
        return 0;
      }
      if (!api->expect(parser, PIE_TOK_COLON,
                       "expected ':' after parameter name")) {
        return 0;
      }
      PieAstType param_type = parse_type(ctx, 0);
      if (param_type.kind == PIE_AST_TYPE_INFER) {
        api->error_at(parser, api->peek(parser),
                      "expected parameter type int, bool, char, byte, string, "
                      "&string, or &mut string");
        return 0;
      }
      char *name = copy_token_text_or_diag(ctx, name_token);
      if (!name) {
        return 0;
      }
      if (!push_function_param(ctx, function, name, param_type)) {
        free(name);
        return 0;
      }
    }

    if (api->match(parser, PIE_TOK_COMMA)) {
      continue;
    }
    if (!api->expect(parser, PIE_TOK_RPAREN,
                     "expected ')' after function parameters")) {
      return 0;
    }
    return 1;
  }
}

static void free_body(PieProgram *body) {
  if (!body) {
    return;
  }
  pie_program_free(body);
  free(body);
}

static PieProgram *parse_function_body(PieParseContext *ctx) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;
  PieProgram *body = (PieProgram *)malloc(sizeof(PieProgram));
  if (!body) {
    pie_diag_error(api->diag(parser),
                   "out of memory while parsing function body");
    return NULL;
  }
  pie_program_init(body);

  while (!api->check(parser, PIE_TOK_EOF)) {
    api->skip_separators(parser);
    if (api->match(parser, PIE_TOK_END)) {
      return body;
    }
    if (!api->parse_statement(parser, body)) {
      free_body(body);
      return NULL;
    }
  }

  pie_diag_error(api->diag(parser), "unterminated fn block; expected end");
  free_body(body);
  return NULL;
}

PieParseResult pie_feature_functions_parse_top_level(PieParseContext *ctx,
                                                     PieProgram *program) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  size_t start = api->pos(parser);
  int is_pub = 0;
  int is_export = 0;
  int is_unsafe = 0;
  for (;;) {
    if (!is_pub && !is_export && api->match(parser, PIE_TOK_PUB)) {
      is_pub = 1;
      continue;
    }
    if (!is_pub && !is_export && api->match(parser, PIE_TOK_EXPORT)) {
      is_export = 1;
      continue;
    }
    if (!is_unsafe && api->match(parser, PIE_TOK_UNSAFE)) {
      is_unsafe = 1;
      continue;
    }
    break;
  }
  if (!api->match(parser, PIE_TOK_FN)) {
    api->set_pos(parser, start);
    return PIE_PARSE_NO_MATCH;
  }

  const PieToken *first_token = api->peek(parser);
  if (!api->expect(parser, PIE_TOK_IDENTIFIER, "expected function name")) {
    return PIE_PARSE_ERROR;
  }

  char *func_name = NULL;
  int is_method = 0;
  if (api->check(parser, PIE_TOK_DOT)) {
    api->advance(parser);
    const PieToken *method_token = api->peek(parser);
    if (!api->expect(parser, PIE_TOK_IDENTIFIER,
                     "expected method name after '.'")) {
      return PIE_PARSE_ERROR;
    }
    size_t type_len = first_token->len;
    size_t method_len = method_token->len;
    size_t mangled_len = type_len + 1 + method_len;
    func_name = (char *)malloc(mangled_len + 1);
    if (!func_name) {
      pie_diag_error(api->diag(parser),
                     "out of memory while building method name");
      return PIE_PARSE_ERROR;
    }
    memcpy(func_name, first_token->start, type_len);
    func_name[type_len] = '_';
    memcpy(func_name + type_len + 1, method_token->start, method_len);
    func_name[mangled_len] = '\0';
    is_method = 1;
  } else {
    func_name = api->copy_token_text(first_token);
  }

  PieFunction function;
  memset(&function, 0, sizeof(function));

  if (api->check(parser, PIE_TOK_LBRACKET)) {
    api->advance(parser);

    function.type_param_constraints = NULL;

    do {
      if (!api->check(parser, PIE_TOK_IDENTIFIER)) {
        api->error_at(parser, api->peek(parser),
                      "expected type parameter name");
        free_function_params(&function);
        return PIE_PARSE_ERROR;
      }
      const PieToken *type_param_token = api->advance(parser);
      char *type_param_name = api->copy_token_text(type_param_token);

      size_t new_count = function.type_param_count + 1;
      char **new_type_params =
          (char **)realloc(function.type_params, new_count * sizeof(char *));
      if (!new_type_params) {
        free(type_param_name);
        free_function_params(&function);
        return PIE_PARSE_ERROR;
      }
      function.type_params = new_type_params;
      function.type_params[function.type_param_count] = type_param_name;

      if (api->match(parser, PIE_TOK_COLON)) {
        if (!api->check(parser, PIE_TOK_IDENTIFIER)) {
          api->error_at(parser, api->peek(parser), "expected constraint name");
          free_function_params(&function);
          return PIE_PARSE_ERROR;
        }
        const PieToken *constraint_token = api->advance(parser);

        if (!function.type_param_constraints) {
          function.type_param_constraints =
              (char **)calloc(new_count, sizeof(char *));
          if (!function.type_param_constraints) {
            free_function_params(&function);
            return PIE_PARSE_ERROR;
          }
        } else {
          char **new_constraints = (char **)realloc(
              function.type_param_constraints, new_count * sizeof(char *));
          if (!new_constraints) {
            free_function_params(&function);
            return PIE_PARSE_ERROR;
          }
          function.type_param_constraints = new_constraints;
        }
        function.type_param_constraints[function.type_param_count] =
            api->copy_token_text(constraint_token);
      }

      function.type_param_count = new_count;

      if (!api->check(parser, PIE_TOK_COMMA)) {
        break;
      }
      api->advance(parser);
    } while (1);

    if (!api->expect(parser, PIE_TOK_RBRACKET,
                     "expected ']' after type parameters")) {
      free_function_params(&function);
      return PIE_PARSE_ERROR;
    }
  }

  if (!api->expect(parser, PIE_TOK_LPAREN,
                   "expected '(' after function name")) {
    free_function_params(&function);
    return PIE_PARSE_ERROR;
  }

  if (!parse_function_params(ctx, &function)) {
    free_function_params(&function);
    return PIE_PARSE_ERROR;
  }

  PieAstType return_type = pie_ast_type_simple(PIE_AST_TYPE_VOID);
  if (api->match(parser, PIE_TOK_ARROW)) {
    return_type = parse_type(ctx, 1);
    if (return_type.kind == PIE_AST_TYPE_INFER) {
      api->error_at(parser, api->peek(parser), "expected function return type");
      free_function_params(&function);
      return PIE_PARSE_ERROR;
    }
  }

  if (function.type_param_count > 0 && api->check(parser, PIE_TOK_WHERE)) {
    api->advance(parser);

    function.type_param_constraints =
        (char **)calloc(function.type_param_count, sizeof(char *));
    if (!function.type_param_constraints) {
      free_function_params(&function);
      return PIE_PARSE_ERROR;
    }

    size_t constraints_parsed = 0;
    do {
      if (!api->check(parser, PIE_TOK_IDENTIFIER)) {
        api->error_at(parser, api->peek(parser),
                      "expected type parameter name in where clause");
        free_function_params(&function);
        return PIE_PARSE_ERROR;
      }

      const PieToken *type_param_token = api->advance(parser);
      char *type_param_name = api->copy_token_text(type_param_token);

      int found = 0;
      for (size_t i = 0; i < function.type_param_count; i++) {
        if (strcmp(type_param_name, function.type_params[i]) == 0) {
          free(type_param_name);

          if (!api->expect(
                  parser, PIE_TOK_COLON,
                  "expected ':' after type parameter in where clause")) {
            free_function_params(&function);
            return PIE_PARSE_ERROR;
          }

          if (!api->check(parser, PIE_TOK_IDENTIFIER)) {
            api->error_at(parser, api->peek(parser),
                          "expected constraint name");
            free_function_params(&function);
            return PIE_PARSE_ERROR;
          }

          const PieToken *constraint_token = api->advance(parser);
          function.type_param_constraints[i] =
              api->copy_token_text(constraint_token);
          constraints_parsed = i + 1;
          found = 1;
          break;
        }
      }

      if (!found) {
        free(type_param_name);
        api->error_at(parser, type_param_token,
                      "unknown type parameter in where clause");
        free_function_params(&function);
        return PIE_PARSE_ERROR;
      }

      if (!api->match(parser, PIE_TOK_COMMA)) {
        break;
      }
    } while (1);
  }

  if (!api->expect(parser, PIE_TOK_COLON,
                   "expected ':' after function signature")) {
    free_function_params(&function);
    return PIE_PARSE_ERROR;
  }

  PieProgram *body = parse_function_body(ctx);
  if (!body) {
    free_function_params(&function);
    return PIE_PARSE_ERROR;
  }

  if (strcmp(func_name, "main") == 0) {
    if (is_unsafe) {
      free_function_params(&function);
      free_body(body);
      free(func_name);
      pie_diag_error(api->diag(parser), "main cannot be unsafe");
      return PIE_PARSE_ERROR;
    }
    if (function.param_count != 0) {
      free_function_params(&function);
      free_body(body);
      free(func_name);
      pie_diag_error(api->diag(parser),
                     "main parameters are not implemented yet");
      return PIE_PARSE_ERROR;
    }
    if (return_type.kind != PIE_AST_TYPE_INT &&
        return_type.kind != PIE_AST_TYPE_VOID) {
      free_function_params(&function);
      free_body(body);
      free(func_name);
      pie_diag_error(
          api->diag(parser),
          "main return types other than int or void are not implemented yet");
      return PIE_PARSE_ERROR;
    }
    if (program->has_main) {
      free_function_params(&function);
      free_body(body);
      free(func_name);
      pie_diag_error(api->diag(parser), "duplicate main function body");
      return PIE_PARSE_ERROR;
    }
    program->has_main = 1;
    program->main_return_type = return_type;
    size_t needed = program->stmt_count + body->stmt_count;
    if (needed > program->stmt_capacity) {
      size_t next_capacity =
          program->stmt_capacity ? program->stmt_capacity * 2 : 16;
      while (next_capacity < needed) {
        next_capacity *= 2;
      }
      PieStmt *next =
          (PieStmt *)realloc(program->stmts, next_capacity * sizeof(PieStmt));
      if (!next) {
        pie_diag_error(api->diag(parser),
                       "out of memory while appending main body");
        free_function_params(&function);
        free_body(body);
        return PIE_PARSE_ERROR;
      }
      program->stmts = next;
      program->stmt_capacity = next_capacity;
    }
    for (size_t i = 0; i < body->stmt_count; i++) {
      program->stmts[program->stmt_count++] = body->stmts[i];
    }
    body->stmts = NULL;
    body->stmt_count = 0;
    body->stmt_capacity = 0;
    for (size_t i = 0; i < body->enum_count; i++) {
      PieEnumDef copy;
      memset(&copy, 0, sizeof(copy));
      if (body->enums[i].name) {
        size_t len = strlen(body->enums[i].name);
        copy.name = (char *)malloc(len + 1);
        if (copy.name) {
          memcpy(copy.name, body->enums[i].name, len + 1);
        }
      }
      copy.variant_count = body->enums[i].variant_count;
      for (size_t v = 0; v < copy.variant_count && v < PIE_ENUM_MAX_VARIANTS;
           v++) {
        PieEnumVariant *v_orig = &body->enums[i].variants[v];
        PieEnumVariant *v_copy = &copy.variants[v];
        if (v_orig->name) {
          size_t len = strlen(v_orig->name);
          v_copy->name = (char *)malloc(len + 1);
          if (v_copy->name) {
            memcpy(v_copy->name, v_orig->name, len + 1);
          }
        }
        v_copy->payload_count = v_orig->payload_count;
        if (v_orig->payload_count > 0) {
          v_copy->payload_kinds = (PieAstTypeKind *)calloc(
              v_orig->payload_count, sizeof(PieAstTypeKind));
          v_copy->payload_widths =
              (int *)calloc(v_orig->payload_count, sizeof(int));
          if (v_copy->payload_kinds && v_copy->payload_widths) {
            memcpy(v_copy->payload_kinds, v_orig->payload_kinds,
                   v_orig->payload_count * sizeof(PieAstTypeKind));
            memcpy(v_copy->payload_widths, v_orig->payload_widths,
                   v_orig->payload_count * sizeof(int));
          }
        }
      }
      pie_program_push_enum(program, copy);
    }
    free_body(body);
    free_function_params(&function);
    free(func_name);
    return PIE_PARSE_OK;
  }

  if (is_method) {
    if (function.param_count == 0 || !function.param_names[0] ||
        strcmp(function.param_names[0], "self") != 0) {
      free_function_params(&function);
      free_body(body);
      free(func_name);
      pie_diag_error(api->diag(parser),
                     "method must have 'self' as first parameter");
      return PIE_PARSE_ERROR;
    }
  }

  function.name = func_name;
  function.return_type = return_type;
  function.is_unsafe = is_unsafe;
  function.is_pub = is_pub;
  function.is_export = is_export;
  function.body = body;
  if (!function.name) {
    free_function_params(&function);
    free_body(body);
    pie_diag_error(api->diag(parser),
                   "out of memory while storing function name");
    return PIE_PARSE_ERROR;
  }
  if (!pie_program_push_function(program, function)) {
    free(function.name);
    free_function_params(&function);
    free_body(body);
    pie_diag_error(api->diag(parser),
                   "out of memory while storing function declaration");
    return PIE_PARSE_ERROR;
  }
  return PIE_PARSE_OK;
}

static int has_assignment_at_depth_zero(PieParser *parser,
                                        const PieParserApi *api) {
  size_t start = api->pos(parser);
  size_t end = api->find_stmt_end(parser, start);
  int paren_depth = 0;
  int bracket_depth = 0;
  int brace_depth = 0;
  for (size_t i = start; i < end; i++) {
    const PieToken *tok = api->peek_n(parser, i - start);
    if (tok->kind == PIE_TOK_LPAREN) {
      paren_depth++;
    } else if (tok->kind == PIE_TOK_RPAREN) {
      if (paren_depth > 0)
        paren_depth--;
    } else if (tok->kind == PIE_TOK_LBRACKET) {
      bracket_depth++;
    } else if (tok->kind == PIE_TOK_RBRACKET) {
      if (bracket_depth > 0)
        bracket_depth--;
    } else if (tok->kind == PIE_TOK_LBRACE) {
      brace_depth++;
    } else if (tok->kind == PIE_TOK_RBRACE) {
      if (brace_depth > 0)
        brace_depth--;
    } else if (paren_depth == 0 && bracket_depth == 0 && brace_depth == 0) {
      if (tok->kind == PIE_TOK_EQ || tok->kind == PIE_TOK_PLUS_EQ ||
          tok->kind == PIE_TOK_MINUS_EQ || tok->kind == PIE_TOK_STAR_EQ ||
          tok->kind == PIE_TOK_SLASH_EQ || tok->kind == PIE_TOK_PERCENT_EQ) {
        return 1;
      }
    }
  }
  return 0;
}

PieParseResult pie_feature_functions_parse_expr_stmt(PieParseContext *ctx,
                                                     PieProgram *program) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (has_assignment_at_depth_zero(parser, api)) {
    return PIE_PARSE_NO_MATCH;
  }

  int is_call = 0;
  if (api->check(parser, PIE_TOK_IDENTIFIER) ||
      api->check(parser, PIE_TOK_SELF)) {
    if (api->peek_n(parser, 1)->kind == PIE_TOK_LPAREN) {
      is_call = 1;
    } else if (api->peek_n(parser, 1)->kind == PIE_TOK_DOT) {
      is_call = 1;
    }
  }

  if (!is_call) {
    return PIE_PARSE_NO_MATCH;
  }

  size_t stmt_end = api->find_stmt_end(parser, api->pos(parser));
  PieExpr *expr = api->parse_expr_until(parser, stmt_end);
  if (!expr) {
    return PIE_PARSE_ERROR;
  }

  PieStmt stmt;
  memset(&stmt, 0, sizeof(stmt));
  stmt.kind = PIE_STMT_EXPR;
  stmt.expr = expr;
  if (!pie_program_push_stmt(program, stmt)) {
    pie_expr_free(expr);
    pie_diag_error(api->diag(parser),
                   "out of memory while storing expression statement");
    return PIE_PARSE_ERROR;
  }
  return PIE_PARSE_OK;
}

PieParseResult pie_feature_functions_parse_return_stmt(PieParseContext *ctx,
                                                       PieProgram *program) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->check(parser, PIE_TOK_RETURN)) {
    return PIE_PARSE_NO_MATCH;
  }
  api->advance(parser);

  size_t stmt_end = api->find_stmt_end(parser, api->pos(parser));
  PieStmt stmt;
  memset(&stmt, 0, sizeof(stmt));
  stmt.kind = PIE_STMT_RETURN;
  if (api->pos(parser) < stmt_end) {
    stmt.expr = api->parse_expr_until(parser, stmt_end);
    if (!stmt.expr) {
      return PIE_PARSE_ERROR;
    }
  } else {
    api->set_pos(parser, stmt_end);
  }

  if (!pie_program_push_stmt(program, stmt)) {
    pie_expr_free(stmt.expr);
    pie_diag_error(api->diag(parser),
                   "out of memory while storing return statement");
    return PIE_PARSE_ERROR;
  }
  return PIE_PARSE_OK;
}

PieParseResult pie_feature_functions_parse_call_expr(PieParseContext *ctx,
                                                     PieExpr **out_expr) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->check(parser, PIE_TOK_IDENTIFIER)) {
    return PIE_PARSE_NO_MATCH;
  }

  const PieToken *name_token = api->peek(parser);
  char *name = api->copy_token_text(name_token);
  int is_maybe = (strcmp(name, "maybe") == 0);
  int is_variant = (strcmp(name, "Some") == 0 || strcmp(name, "None") == 0 ||
                    strcmp(name, "Ok") == 0 || strcmp(name, "Err") == 0);
  free(name);

  if (api->peek_n(parser, 1)->kind != PIE_TOK_LPAREN && !is_maybe) {
    return PIE_PARSE_NO_MATCH;
  }

  if (is_variant) {
    return PIE_PARSE_NO_MATCH;
  }

  name_token = api->advance(parser);
  if (is_maybe && api->peek(parser)->kind != PIE_TOK_LPAREN) {
    *out_expr = pie_expr_call("maybe");
    if (!*out_expr) {
      pie_diag_error(api->diag(parser),
                     "out of memory while building function call");
      return PIE_PARSE_ERROR;
    }
    return PIE_PARSE_OK;
  }

  api->advance(parser);
  name = api->copy_token_text(name_token);
  *out_expr = pie_expr_call(name);
  free(name);
  if (!*out_expr) {
    pie_diag_error(api->diag(parser),
                   "out of memory while building function call");
    return PIE_PARSE_ERROR;
  }

  if (api->match(parser, PIE_TOK_RPAREN)) {
    return PIE_PARSE_OK;
  }

  for (;;) {
    PieExpr *arg = api->parse_expr(parser);
    if (!arg) {
      pie_expr_free(*out_expr);
      *out_expr = NULL;
      return PIE_PARSE_ERROR;
    }
    if (!pie_expr_call_add_arg(*out_expr, arg)) {
      pie_expr_free(arg);
      pie_expr_free(*out_expr);
      *out_expr = NULL;
      pie_diag_error(api->diag(parser),
                     "out of memory while storing function call argument");
      return PIE_PARSE_ERROR;
    }
    if (api->match(parser, PIE_TOK_COMMA)) {
      continue;
    }
    if (!api->expect(parser, PIE_TOK_RPAREN,
                     "expected ')' after function call arguments")) {
      pie_expr_free(*out_expr);
      *out_expr = NULL;
      return PIE_PARSE_ERROR;
    }
    break;
  }
  return PIE_PARSE_OK;
}
