#include "pie/core/parser/parser.h"

#include <stdlib.h>

static size_t find_colon(PieParseContext *ctx) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;
  size_t start = api->pos(parser);
  int paren = 0;

  for (size_t i = start;; i++) {
    PieTokenKind kind = api->peek_n(parser, i - start)->kind;
    if (kind == PIE_TOK_EOF || kind == PIE_TOK_END || kind == PIE_TOK_NEWLINE) {
      return (size_t)-1;
    }
    if (kind == PIE_TOK_LPAREN) {
      paren++;
    } else if (kind == PIE_TOK_RPAREN && paren > 0) {
      paren--;
    } else if (kind == PIE_TOK_COLON && paren == 0) {
      return i;
    }
  }
}

PieParseResult pie_feature_if_expr_parse_prefix(PieParseContext *ctx,
                                                PieExpr **out_expr) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->check(parser, PIE_TOK_IF)) {
    return PIE_PARSE_NO_MATCH;
  }

  size_t saved_pos = api->pos(parser);
  api->advance(parser);

  size_t colon = find_colon(ctx);
  if (colon == (size_t)-1) {
    api->error_at(parser, api->peek(parser),
                  "expected ':' after if condition in if expression");
    return PIE_PARSE_ERROR;
  }

  PieExpr *condition = api->parse_expr_until(parser, colon);
  if (!condition) {
    return PIE_PARSE_ERROR;
  }
  api->set_pos(parser, colon);
  if (!api->expect(parser, PIE_TOK_COLON, "expected ':' after if condition")) {
    pie_expr_free(condition);
    return PIE_PARSE_ERROR;
  }

  api->skip_separators(parser);

  PieExpr *then_expr = api->parse_expr(parser);
  if (!then_expr) {
    pie_expr_free(condition);
    return PIE_PARSE_ERROR;
  }

  api->skip_separators(parser);

  if (!api->check(parser, PIE_TOK_ELSE)) {
    pie_expr_free(condition);
    pie_expr_free(then_expr);
    api->set_pos(parser, saved_pos);
    return PIE_PARSE_NO_MATCH;
  }
  api->advance(parser);

  api->skip_separators(parser);

  if (!api->expect(parser, PIE_TOK_COLON,
                   "expected ':' after else in if expression")) {
    pie_expr_free(condition);
    pie_expr_free(then_expr);
    api->set_pos(parser, saved_pos);
    return PIE_PARSE_NO_MATCH;
  }

  api->skip_separators(parser);

  PieExpr *else_expr = api->parse_expr(parser);
  if (!else_expr) {
    pie_expr_free(condition);
    pie_expr_free(then_expr);
    return PIE_PARSE_ERROR;
  }

  api->skip_separators(parser);

  if (!api->expect(parser, PIE_TOK_END, "expected 'end' after if expression")) {
    pie_expr_free(condition);
    pie_expr_free(then_expr);
    pie_expr_free(else_expr);
    return PIE_PARSE_ERROR;
  }

  PieExpr *expr = (PieExpr *)calloc(1, sizeof(PieExpr));
  if (!expr) {
    pie_expr_free(condition);
    pie_expr_free(then_expr);
    pie_expr_free(else_expr);
    pie_diag_error(api->diag(parser),
                   "out of memory while building if expression");
    return PIE_PARSE_ERROR;
  }
  expr->kind = PIE_EXPR_IF;
  expr->if_condition = condition;
  expr->if_then = then_expr;
  expr->if_else = else_expr;

  *out_expr = expr;
  return PIE_PARSE_OK;
}
