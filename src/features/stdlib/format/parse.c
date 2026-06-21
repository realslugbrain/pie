#include "pie/core/parser/parser.h"

#include <stdlib.h>
#include <string.h>

PieParseResult pie_feature_format_parse_expr(PieParseContext *ctx,
                                             PieExpr **out_expr) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->match(parser, PIE_TOK_FORMAT)) {
    return PIE_PARSE_NO_MATCH;
  }

  if (!api->expect(parser, PIE_TOK_LPAREN, "expected '(' after format")) {
    return PIE_PARSE_ERROR;
  }

  PieExpr *template_expr = api->parse_expr(parser);
  if (!template_expr) {
    return PIE_PARSE_ERROR;
  }

  size_t arg_cap = 4;
  PieCallArg *args = (PieCallArg *)malloc(arg_cap * sizeof(PieCallArg));
  size_t arg_count = 0;

  args[arg_count].expr = template_expr;
  arg_count++;

  while (api->match(parser, PIE_TOK_COMMA)) {
    api->skip_newlines(parser);
    PieExpr *arg = api->parse_expr(parser);
    if (!arg) {
      for (size_t i = 0; i < arg_count; i++) {
        pie_expr_free(args[i].expr);
      }
      free(args);
      return PIE_PARSE_ERROR;
    }
    if (arg_count >= arg_cap) {
      arg_cap *= 2;
      PieCallArg *new_args =
          (PieCallArg *)realloc(args, arg_cap * sizeof(PieCallArg));
      if (!new_args) {
        pie_expr_free(arg);
        for (size_t i = 0; i < arg_count; i++) {
          pie_expr_free(args[i].expr);
        }
        free(args);
        return PIE_PARSE_ERROR;
      }
      args = new_args;
    }
    args[arg_count].expr = arg;
    arg_count++;
  }

  if (!api->expect(parser, PIE_TOK_RPAREN,
                   "expected ')' after format arguments")) {
    for (size_t i = 0; i < arg_count; i++) {
      pie_expr_free(args[i].expr);
    }
    free(args);
    return PIE_PARSE_ERROR;
  }

  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr) {
    for (size_t i = 0; i < arg_count; i++) {
      pie_expr_free(args[i].expr);
    }
    free(args);
    pie_diag_error(api->diag(parser),
                   "out of memory while creating format expression");
    return PIE_PARSE_ERROR;
  }
  expr->kind = PIE_EXPR_CALL;
  expr->call_name = (char *)malloc(7);
  if (!expr->call_name) {
    for (size_t i = 0; i < arg_count; i++) {
      pie_expr_free(args[i].expr);
    }
    free(args);
    free(expr);
    return PIE_PARSE_ERROR;
  }
  memcpy(expr->call_name, "format", 7);
  expr->call_args = args;
  expr->call_arg_count = arg_count;

  *out_expr = expr;
  return PIE_PARSE_OK;
}
