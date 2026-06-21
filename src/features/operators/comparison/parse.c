#include "pie/core/parser/parser.h"

static int comparison_precedence(PieTokenKind kind, const char **out_op) {
  switch (kind) {
  case PIE_TOK_EQ_EQ:
    *out_op = "==";
    return 5;
  case PIE_TOK_BANG_EQ:
    *out_op = "!=";
    return 5;
  case PIE_TOK_LT:
    *out_op = "<";
    return 6;
  case PIE_TOK_LT_EQ:
    *out_op = "<=";
    return 6;
  case PIE_TOK_GT:
    *out_op = ">";
    return 6;
  case PIE_TOK_GT_EQ:
    *out_op = ">=";
    return 6;
  default:
    *out_op = "";
    return -1;
  }
}

PieParseResult pie_feature_comparison_parse_infix_expr(PieParseContext *ctx,
                                                       PieExpr **left,
                                                       int min_precedence) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;
  const char *op = "";
  int precedence = comparison_precedence(api->peek(parser)->kind, &op);

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
                   "out of memory while building comparison expression");
    return PIE_PARSE_ERROR;
  }
  *left = expr;
  return PIE_PARSE_OK;
}
