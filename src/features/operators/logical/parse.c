#include "pie/core/parser/parser.h"

static int logical_precedence(PieTokenKind kind, const char **out_op) {
  switch (kind) {
  case PIE_TOK_OR:
    *out_op = "or";
    return 2;
  case PIE_TOK_AND:
    *out_op = "and";
    return 3;
  default:
    *out_op = "";
    return -1;
  }
}

PieParseResult pie_feature_logical_parse_prefix_expr(PieParseContext *ctx,
                                                     PieExpr **out_expr) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->match(parser, PIE_TOK_NOT) && !api->match(parser, PIE_TOK_BANG)) {
    return PIE_PARSE_NO_MATCH;
  }

  PieExpr *inner = api->parse_expr_prec(parser, 30);
  if (!inner) {
    return PIE_PARSE_ERROR;
  }

  *out_expr = pie_expr_unary_op("not", inner);
  if (!*out_expr) {
    pie_expr_free(inner);
    pie_diag_error(api->diag(parser),
                   "out of memory while building logical not expression");
    return PIE_PARSE_ERROR;
  }
  return PIE_PARSE_OK;
}

PieParseResult pie_feature_logical_parse_infix_expr(PieParseContext *ctx,
                                                    PieExpr **left,
                                                    int min_precedence) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;
  const char *op = "";
  int precedence = logical_precedence(api->peek(parser)->kind, &op);

  if (precedence < 0 || precedence < min_precedence) {
    return PIE_PARSE_NO_MATCH;
  }

  api->advance(parser);
  PieExpr *right = api->parse_expr_prec(parser, precedence + 1);
  if (!right) {
    return PIE_PARSE_ERROR;
  }

  PieExpr *expr = pie_expr_binary_op(op, *left, right);
  if (!expr) {
    pie_expr_free(right);
    pie_diag_error(api->diag(parser),
                   "out of memory while building logical expression");
    return PIE_PARSE_ERROR;
  }
  *left = expr;
  return PIE_PARSE_OK;
}
