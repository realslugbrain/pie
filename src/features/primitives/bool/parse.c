#include "pie/core/parser/parser.h"

PieParseResult pie_feature_bool_parse_expr(PieParseContext *ctx,
                                           PieExpr **out_expr) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (api->match(parser, PIE_TOK_TRUE)) {
    *out_expr = pie_expr_bool(1);
  } else if (api->match(parser, PIE_TOK_FALSE)) {
    *out_expr = pie_expr_bool(0);
  } else {
    return PIE_PARSE_NO_MATCH;
  }

  if (!*out_expr) {
    pie_diag_error(api->diag(parser),
                   "out of memory while building bool literal");
    return PIE_PARSE_ERROR;
  }
  return PIE_PARSE_OK;
}
