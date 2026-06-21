#include "pie/core/ast/ast.h"
#include "pie/core/parser/parser.h"

#include <string.h>

static int token_is_maybe(const PieToken *token) {
  return token->kind == PIE_TOK_IDENTIFIER && token->len == 5 &&
         memcmp(token->start, "maybe", 5) == 0;
}

PieParseResult pie_feature_maybe_parse_expr(PieParseContext *ctx,
                                            PieExpr **out_expr) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!token_is_maybe(api->peek(parser))) {
    return PIE_PARSE_NO_MATCH;
  }

  if (api->peek_n(parser, 1)->kind == PIE_TOK_LPAREN) {
    return PIE_PARSE_NO_MATCH;
  }

  api->advance(parser);
  *out_expr = pie_expr_maybe();
  if (!*out_expr) {
    pie_diag_error(api->diag(parser),
                   "out of memory while parsing maybe expression");
    return PIE_PARSE_ERROR;
  }
  return PIE_PARSE_OK;
}
