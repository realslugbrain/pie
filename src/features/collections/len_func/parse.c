#include "pie/core/parser/parser.h"

#include <stdlib.h>
#include <string.h>

PieParseResult pie_feature_len_func_parse_prefix_expr(PieParseContext *ctx,
                                                      PieExpr **out_expr) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  size_t start = api->pos(parser);
  const PieToken *tok = api->peek(parser);
  if (tok->kind != PIE_TOK_IDENTIFIER || tok->len != 3 ||
      memcmp(tok->start, "len", 3) != 0) {
    return PIE_PARSE_NO_MATCH;
  }
  api->advance(parser);

  if (!api->match(parser, PIE_TOK_LPAREN)) {
    api->set_pos(parser, start);
    return PIE_PARSE_NO_MATCH;
  }

  PieExpr *arg = api->parse_expr_prec(parser, 0);
  if (!arg) {
    api->set_pos(parser, start);
    return PIE_PARSE_NO_MATCH;
  }

  if (!api->match(parser, PIE_TOK_RPAREN)) {
    pie_expr_free(arg);
    api->set_pos(parser, start);
    return PIE_PARSE_NO_MATCH;
  }

  PieExpr *call = pie_expr_call("len");
  if (!call) {
    pie_expr_free(arg);
    return PIE_PARSE_ERROR;
  }
  if (!pie_expr_call_add_arg(call, arg)) {
    pie_expr_free(call);
    return PIE_PARSE_ERROR;
  }
  *out_expr = call;
  return PIE_PARSE_OK;
}
