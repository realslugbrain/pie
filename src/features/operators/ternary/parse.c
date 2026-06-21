#include "pie/core/parser/parser.h"

PieParseResult pie_feature_ternary_parse_infix(PieParseContext *ctx,
                                               PieExpr **left,
                                               int min_precedence) {
  (void)left;
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  int precedence = 2;
  if (precedence < min_precedence) {
    return PIE_PARSE_NO_MATCH;
  }

  if (!api->check(parser, PIE_TOK_QUESTION)) {
    return PIE_PARSE_NO_MATCH;
  }
  api->advance(parser);

  api->skip_separators(parser);

  PieExpr *true_expr = api->parse_expr(parser);
  if (!true_expr) {
    return PIE_PARSE_ERROR;
  }

  api->skip_separators(parser);

  if (!api->expect(parser, PIE_TOK_COLON,
                   "expected ':' after true branch in ternary")) {
    pie_expr_free(true_expr);
    return PIE_PARSE_ERROR;
  }

  api->skip_separators(parser);

  PieExpr *false_expr = api->parse_expr(parser);
  if (!false_expr) {
    pie_expr_free(true_expr);
    return PIE_PARSE_ERROR;
  }

  PieExpr *expr = pie_expr_ternary(*left, true_expr, false_expr);
  if (!expr) {
    pie_expr_free(true_expr);
    pie_expr_free(false_expr);
    pie_diag_error(api->diag(parser),
                   "out of memory while building ternary expression");
    return PIE_PARSE_ERROR;
  }

  *left = expr;
  return PIE_PARSE_OK;
}
