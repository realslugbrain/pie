#include "pie/core/parser/parser.h"

PieParseResult pie_feature_string_concat_parse_infix_expr(PieParseContext *ctx,
                                                          PieExpr **left,
                                                          int min_precedence) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  int precedence = 5;
  if (precedence < min_precedence) {
    return PIE_PARSE_NO_MATCH;
  }

  if (!api->match(parser, PIE_TOK_PLUS_PLUS)) {
    return PIE_PARSE_NO_MATCH;
  }

  PieExpr *right = api->parse_expr_prec(parser, precedence + 1);
  if (!right) {
    return PIE_PARSE_ERROR;
  }

  PieExpr *expr = pie_expr_binary_op("++", *left, right);
  if (!expr) {
    pie_expr_free(right);
    pie_diag_error(api->diag(parser),
                   "out of memory while building string concat expression");
    return PIE_PARSE_ERROR;
  }
  *left = expr;
  return PIE_PARSE_OK;
}
