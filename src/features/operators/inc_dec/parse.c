#include "pie/core/parser/parser.h"

static int is_primary_start(PieTokenKind kind) {
  switch (kind) {
  case PIE_TOK_INT_LITERAL:
  case PIE_TOK_FLOAT_LITERAL:
  case PIE_TOK_CHAR_LITERAL:
  case PIE_TOK_STRING_LITERAL:
  case PIE_TOK_IDENTIFIER:
  case PIE_TOK_SELF:
  case PIE_TOK_LPAREN:
  case PIE_TOK_PLUS_PLUS:
  case PIE_TOK_MINUS_MINUS:
  case PIE_TOK_MINUS:
  case PIE_TOK_NOT:
  case PIE_TOK_NEW:
  case PIE_TOK_AMP:
  case PIE_TOK_IF:
  case PIE_TOK_MATCH:
  case PIE_TOK_NULL:
  case PIE_TOK_TRUE:
  case PIE_TOK_FALSE:
    return 1;
  default:
    return 0;
  }
}

PieParseResult pie_feature_incdec_parse_prefix_expr(PieParseContext *ctx,
                                                    PieExpr **out_expr) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  const PieToken *tok = api->peek(parser);
  if (tok->kind != PIE_TOK_PLUS_PLUS && tok->kind != PIE_TOK_MINUS_MINUS) {
    return PIE_PARSE_NO_MATCH;
  }

  const char *op = tok->kind == PIE_TOK_PLUS_PLUS ? "++" : "--";
  api->advance(parser);

  PieExpr *inner = api->parse_expr_prec(parser, 30);
  if (!inner) {
    return PIE_PARSE_ERROR;
  }

  *out_expr = pie_expr_unary_op(op, inner);
  if (!*out_expr) {
    pie_expr_free(inner);
    pie_diag_error(api->diag(parser),
                   "out of memory while building prefix inc/dec expression");
    return PIE_PARSE_ERROR;
  }
  return PIE_PARSE_OK;
}

PieParseResult pie_feature_incdec_parse_infix_expr(PieParseContext *ctx,
                                                   PieExpr **left,
                                                   int min_precedence) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (min_precedence > 40) {
    return PIE_PARSE_NO_MATCH;
  }

  const PieToken *tok = api->peek(parser);
  if (tok->kind != PIE_TOK_PLUS_PLUS && tok->kind != PIE_TOK_MINUS_MINUS) {
    return PIE_PARSE_NO_MATCH;
  }

  const PieToken *next = api->peek_n(parser, 1);
  if (is_primary_start(next->kind)) {
    return PIE_PARSE_NO_MATCH;
  }

  const char *op = tok->kind == PIE_TOK_PLUS_PLUS ? "++" : "--";
  api->advance(parser);

  PieExpr *expr = pie_expr_postfix(op, *left);
  if (!expr) {
    return PIE_PARSE_ERROR;
  }
  *left = expr;
  return PIE_PARSE_OK;
}
