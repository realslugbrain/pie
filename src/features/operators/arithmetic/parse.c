#include "pie/core/parser/parser.h"

static int arithmetic_precedence(PieTokenKind kind, char *out_op,
                                 const char **out_op_text) {
  switch (kind) {
  case PIE_TOK_PLUS:
    *out_op = '+';
    *out_op_text = NULL;
    return 10;
  case PIE_TOK_MINUS:
    *out_op = '-';
    *out_op_text = NULL;
    return 10;
  case PIE_TOK_STAR:
    *out_op = '*';
    *out_op_text = NULL;
    return 20;
  case PIE_TOK_SLASH:
    *out_op = '/';
    *out_op_text = NULL;
    return 20;
  case PIE_TOK_PERCENT:
    *out_op = '%';
    *out_op_text = NULL;
    return 20;
  case PIE_TOK_STAR_STAR:
    *out_op = '*';
    *out_op_text = "**";
    return 30;
  default:
    *out_op = '\0';
    *out_op_text = NULL;
    return -1;
  }
}

PieParseResult pie_feature_arithmetic_parse_prefix_expr(PieParseContext *ctx,
                                                        PieExpr **out_expr) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->match(parser, PIE_TOK_MINUS)) {
    return PIE_PARSE_NO_MATCH;
  }

  PieExpr *inner = api->parse_expr_prec(parser, 30);
  if (!inner) {
    return PIE_PARSE_ERROR;
  }

  *out_expr = pie_expr_unary('-', inner);
  if (!*out_expr) {
    pie_expr_free(inner);
    pie_diag_error(api->diag(parser),
                   "out of memory while building unary expression");
    return PIE_PARSE_ERROR;
  }
  return PIE_PARSE_OK;
}

PieParseResult pie_feature_arithmetic_parse_infix_expr(PieParseContext *ctx,
                                                       PieExpr **left,
                                                       int min_precedence) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;
  char op = '\0';
  const char *op_text = NULL;
  int precedence =
      arithmetic_precedence(api->peek(parser)->kind, &op, &op_text);

  if (precedence < 0 || precedence < min_precedence) {
    return PIE_PARSE_NO_MATCH;
  }

  api->advance(parser);
  PieExpr *right = api->parse_expr_prec(parser, precedence + 1);
  if (!right) {
    return PIE_PARSE_ERROR;
  }

  PieExpr *expr;
  if (op_text) {
    expr = pie_expr_binary_op(op_text, *left, right);
  } else {
    expr = pie_expr_binary(op, *left, right);
  }
  if (!expr) {
    pie_expr_free(right);
    pie_diag_error(api->diag(parser),
                   "out of memory while building binary expression");
    return PIE_PARSE_ERROR;
  }
  *left = expr;
  return PIE_PARSE_OK;
}
