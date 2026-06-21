#include "pie/core/parser/parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static PieAstTypeKind parse_type_keyword(PieParseContext *ctx) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (api->match(parser, PIE_TOK_INT_TYPE))
    return PIE_AST_TYPE_INT;
  if (api->match(parser, PIE_TOK_FLOAT_TYPE))
    return PIE_AST_TYPE_FLOAT;
  if (api->match(parser, PIE_TOK_STRING_TYPE))
    return PIE_AST_TYPE_STRING;
  if (api->match(parser, PIE_TOK_BOOL_TYPE))
    return PIE_AST_TYPE_BOOL;
  if (api->match(parser, PIE_TOK_CHAR_TYPE))
    return PIE_AST_TYPE_CHAR;
  if (api->match(parser, PIE_TOK_BYTE_TYPE))
    return PIE_AST_TYPE_BYTE;
  if (api->match(parser, PIE_TOK_VOID_TYPE))
    return PIE_AST_TYPE_VOID;

  return PIE_AST_TYPE_INFER;
}

PieParseResult pie_feature_closures_parse_expr(PieParseContext *ctx,
                                               PieExpr **out_expr) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->match(parser, PIE_TOK_FN)) {
    return PIE_PARSE_NO_MATCH;
  }

  PieExpr *expr = pie_expr_closure();
  if (!expr) {
    pie_diag_error(api->diag(parser), "out of memory while creating closure");
    return PIE_PARSE_ERROR;
  }

  if (!api->expect(parser, PIE_TOK_LPAREN, "expected '(' after 'fn'")) {
    pie_expr_free(expr);
    return PIE_PARSE_ERROR;
  }

  size_t param_cap = 4;
  expr->closure_param_names = (char **)calloc(param_cap, sizeof(char *));
  expr->closure_param_types =
      (PieAstType *)calloc(param_cap, sizeof(PieAstType));
  expr->closure_param_count = 0;

  while (!api->check(parser, PIE_TOK_RPAREN)) {
    if (expr->closure_param_count > 0) {
      if (!api->match(parser, PIE_TOK_COMMA)) {
        api->error_at(parser, api->peek(parser),
                      "expected ',' or ')' in closure parameters");
        pie_expr_free(expr);
        return PIE_PARSE_ERROR;
      }
    }

    const PieToken *name_token = api->peek(parser);
    if (!api->expect(parser, PIE_TOK_IDENTIFIER, "expected parameter name")) {
      pie_expr_free(expr);
      return PIE_PARSE_ERROR;
    }

    if (!api->expect(parser, PIE_TOK_COLON,
                     "expected ':' after parameter name")) {
      pie_expr_free(expr);
      return PIE_PARSE_ERROR;
    }

    PieAstType param_type;
    memset(&param_type, 0, sizeof(param_type));
    param_type.kind = parse_type_keyword(ctx);

    if (param_type.kind == PIE_AST_TYPE_INFER) {
      const PieToken *type_token = api->peek(parser);
      if (api->check(parser, PIE_TOK_IDENTIFIER)) {
        api->advance(parser);
        param_type.kind = PIE_AST_TYPE_STRUCT;
        param_type.struct_name = api->copy_token_text(type_token);
      } else {
        api->error_at(parser, api->peek(parser),
                      "expected type in closure parameter");
        pie_expr_free(expr);
        return PIE_PARSE_ERROR;
      }
    }

    if (expr->closure_param_count >= param_cap) {
      param_cap *= 2;
      char **new_names = (char **)realloc(expr->closure_param_names,
                                          param_cap * sizeof(char *));
      if (!new_names) {
        pie_expr_free(expr);
        pie_diag_error(api->diag(parser),
                       "out of memory while growing closure parameters");
        return PIE_PARSE_ERROR;
      }
      expr->closure_param_names = new_names;

      PieAstType *new_types = (PieAstType *)realloc(
          expr->closure_param_types, param_cap * sizeof(PieAstType));
      if (!new_types) {
        pie_expr_free(expr);
        pie_diag_error(api->diag(parser),
                       "out of memory while growing closure parameters");
        return PIE_PARSE_ERROR;
      }
      expr->closure_param_types = new_types;
    }

    expr->closure_param_names[expr->closure_param_count] =
        api->copy_token_text(name_token);
    expr->closure_param_types[expr->closure_param_count] = param_type;
    expr->closure_param_count++;
  }

  if (!api->expect(parser, PIE_TOK_RPAREN,
                   "expected ')' after closure parameters")) {
    pie_expr_free(expr);
    return PIE_PARSE_ERROR;
  }

  if (!api->expect(parser, PIE_TOK_ARROW,
                   "expected '->' after closure parameters")) {
    pie_expr_free(expr);
    return PIE_PARSE_ERROR;
  }

  memset(&expr->closure_return_type, 0, sizeof(expr->closure_return_type));
  expr->closure_return_type.kind = parse_type_keyword(ctx);

  if (expr->closure_return_type.kind == PIE_AST_TYPE_INFER) {
    const PieToken *type_token = api->peek(parser);
    if (api->check(parser, PIE_TOK_IDENTIFIER)) {
      api->advance(parser);
      expr->closure_return_type.kind = PIE_AST_TYPE_STRUCT;
      expr->closure_return_type.struct_name = api->copy_token_text(type_token);
    } else {
      api->error_at(parser, api->peek(parser),
                    "expected return type in closure");
      pie_expr_free(expr);
      return PIE_PARSE_ERROR;
    }
  }

  if (!api->expect(parser, PIE_TOK_COLON, "expected ':' after return type")) {
    pie_expr_free(expr);
    return PIE_PARSE_ERROR;
  }

  expr->closure_body = (PieProgram *)malloc(sizeof(PieProgram));
  if (!expr->closure_body) {
    pie_expr_free(expr);
    pie_diag_error(api->diag(parser),
                   "out of memory while creating closure body");
    return PIE_PARSE_ERROR;
  }
  pie_program_init(expr->closure_body);

  api->skip_separators(parser);
  while (!api->check(parser, PIE_TOK_END) && !api->check(parser, PIE_TOK_EOF)) {
    if (!api->parse_statement(parser, expr->closure_body)) {
      pie_program_free(expr->closure_body);
      free(expr->closure_body);
      expr->closure_body = NULL;
      pie_expr_free(expr);
      return PIE_PARSE_ERROR;
    }
    api->skip_separators(parser);
  }

  if (!api->expect(parser, PIE_TOK_END, "expected 'end' after closure body")) {
    pie_program_free(expr->closure_body);
    free(expr->closure_body);
    expr->closure_body = NULL;
    pie_expr_free(expr);
    return PIE_PARSE_ERROR;
  }

  *out_expr = expr;
  return PIE_PARSE_OK;
}