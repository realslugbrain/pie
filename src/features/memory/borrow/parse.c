#include "pie/core/parser/parser.h"

PieParseResult pie_feature_borrow_parse_expr(PieParseContext *ctx,
                                             PieExpr **out_expr) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (api->check(parser, PIE_TOK_AMP) &&
      api->peek_n(parser, 1)->kind == PIE_TOK_RAW) {
    return PIE_PARSE_NO_MATCH;
  }

  if (!api->match(parser, PIE_TOK_AMP)) {
    return PIE_PARSE_NO_MATCH;
  }

  const char *op = "&";
  if (api->match(parser, PIE_TOK_MUT)) {
    op = "&mut";
  }

  PieExpr *inner = api->parse_expr_prec(parser, 30);
  if (!inner) {
    return PIE_PARSE_ERROR;
  }

  *out_expr = pie_expr_unary_op(op, inner);
  if (!*out_expr) {
    pie_expr_free(inner);
    pie_diag_error(api->diag(parser),
                   "out of memory while building borrow expression");
    return PIE_PARSE_ERROR;
  }
  return PIE_PARSE_OK;
}
