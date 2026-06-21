#include "pie/core/parser/parser.h"

PieParseResult pie_feature_range_parse_expr(PieParseContext *ctx,
                                            PieExpr **left,
                                            int min_precedence) {
  (void)min_precedence;
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  int inclusive = 0;
  if (api->match(parser, PIE_TOK_DOT_DOT)) {
    inclusive = 0;
  } else if (api->match(parser, PIE_TOK_DOT_DOT_EQ)) {
    inclusive = 1;
  } else {
    return PIE_PARSE_NO_MATCH;
  }

  PieExpr *start = *left;
  PieExpr *end = api->parse_expr(parser);
  if (!end) {
    api->error_at(parser, api->peek(parser), "expected expression after '..'");
    return PIE_PARSE_ERROR;
  }

  *left = pie_expr_range(start, end, inclusive);
  if (!*left) {
    pie_expr_free(start);
    pie_expr_free(end);
    api->error_at(parser, api->peek(parser),
                  "out of memory while building range expression");
    return PIE_PARSE_ERROR;
  }

  return PIE_PARSE_OK;
}
