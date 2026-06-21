#include "pie/core/parser/parser.h"

PieParseResult pie_feature_null_parse_expr(PieParseContext *ctx,
                                           PieExpr **out_expr) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->match(parser, PIE_TOK_NULL)) {
    return PIE_PARSE_NO_MATCH;
  }

  *out_expr = pie_expr_null();
  if (!*out_expr) {
    pie_diag_error(api->diag(parser),
                   "out of memory while building null literal");
    return PIE_PARSE_ERROR;
  }
  return PIE_PARSE_OK;
}
