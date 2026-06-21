#include "pie/core/parser/parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PieParseResult pie_feature_method_call_parse_expr(PieParseContext *ctx,
                                                  PieExpr **left,
                                                  int min_precedence) {
  (void)min_precedence;
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->check(parser, PIE_TOK_DOT)) {
    return PIE_PARSE_NO_MATCH;
  }

  if (api->peek_n(parser, 1)->kind != PIE_TOK_IDENTIFIER) {
    return PIE_PARSE_NO_MATCH;
  }

  if (api->peek_n(parser, 2)->kind != PIE_TOK_LPAREN) {
    return PIE_PARSE_NO_MATCH;
  }

  api->advance(parser);

  const PieToken *method_token = api->advance(parser);
  char *method_name = api->copy_token_text(method_token);
  if (!method_name) {
    pie_diag_error(api->diag(parser), "out of memory");
    return PIE_PARSE_ERROR;
  }

  api->advance(parser);

  PieExpr *call = pie_expr_method_call(*left, method_name);
  free(method_name);
  if (!call) {
    pie_diag_error(api->diag(parser), "out of memory");
    return PIE_PARSE_ERROR;
  }

  if (!api->check(parser, PIE_TOK_RPAREN)) {
    do {
      api->skip_newlines(parser);
      PieExpr *arg = api->parse_expr(parser);
      if (!arg) {
        pie_expr_free(call);
        return PIE_PARSE_ERROR;
      }
      if (!pie_expr_method_call_add_arg(call, arg)) {
        pie_expr_free(arg);
        pie_expr_free(call);
        pie_diag_error(api->diag(parser), "out of memory");
        return PIE_PARSE_ERROR;
      }
      api->skip_newlines(parser);
    } while (api->match(parser, PIE_TOK_COMMA));
  }

  if (!api->expect(parser, PIE_TOK_RPAREN,
                   "expected ')' after method arguments")) {
    pie_expr_free(call);
    return PIE_PARSE_ERROR;
  }

  *left = call;
  return PIE_PARSE_OK;
}
