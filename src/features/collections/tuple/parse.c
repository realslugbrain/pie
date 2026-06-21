#include "pie/core/parser/parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PieParseResult pie_feature_tuple_parse_field_access(PieParseContext *ctx,
                                                    PieExpr **left,
                                                    int min_precedence) {
  (void)min_precedence;
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->match(parser, PIE_TOK_DOT)) {
    return PIE_PARSE_NO_MATCH;
  }

  if (!api->check(parser, PIE_TOK_INT_LITERAL)) {
    return PIE_PARSE_NO_MATCH;
  }

  const PieToken *idx_token = api->advance(parser);
  char idx_str[32];
  snprintf(idx_str, sizeof(idx_str), "%lld", idx_token->int_value);

  PieExpr *field = pie_expr_field(*left, idx_str);
  if (!field) {
    pie_diag_error(api->diag(parser),
                   "out of memory while building tuple field access");
    return PIE_PARSE_ERROR;
  }

  *left = field;
  return PIE_PARSE_OK;
}
