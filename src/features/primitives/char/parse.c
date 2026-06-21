#include "pie/core/parser/parser.h"

PieParseResult pie_feature_char_parse_expr(PieParseContext *ctx,
                                           PieExpr **out_expr) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->check(parser, PIE_TOK_CHAR_LITERAL)) {
    return PIE_PARSE_NO_MATCH;
  }

  const PieToken *token = api->advance(parser);
  *out_expr = pie_expr_char((unsigned int)token->int_value);
  if (!*out_expr) {
    pie_diag_error(api->diag(parser),
                   "out of memory while building char literal");
    return PIE_PARSE_ERROR;
  }
  return PIE_PARSE_OK;
}
