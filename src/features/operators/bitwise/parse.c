#include "pie/core/parser/parser.h"

static int bitwise_precedence(PieTokenKind kind) {
  switch (kind) {
  case PIE_TOK_LT_LT:
  case PIE_TOK_GT_GT:
    return 7;
  case PIE_TOK_AMP:
    return 10;
  case PIE_TOK_CARET:
    return 11;
  case PIE_TOK_PIPE:
    return 12;
  default:
    return -1;
  }
}

PieParseResult pie_feature_bitwise_parse_infix(PieParseContext *ctx,
                                               PieExpr **left,
                                               int min_precedence) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  int precedence = bitwise_precedence(api->peek(parser)->kind);
  if (precedence < 0 || precedence < min_precedence) {
    return PIE_PARSE_NO_MATCH;
  }

  const char *op = "";
  PieTokenKind kind = api->peek(parser)->kind;
  switch (kind) {
  case PIE_TOK_AMP:
    op = "&";
    break;
  case PIE_TOK_PIPE:
    op = "|";
    break;
  case PIE_TOK_CARET:
    op = "^";
    break;
  case PIE_TOK_LT_LT:
    op = "<<";
    break;
  case PIE_TOK_GT_GT:
    op = ">>";
    break;
  default:
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
                   "out of memory while building bitwise expression");
    return PIE_PARSE_ERROR;
  }
  *left = expr;
  return PIE_PARSE_OK;
}

PieParseResult pie_feature_bitwise_parse_prefix(PieParseContext *ctx,
                                                PieExpr **out_expr) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->match(parser, PIE_TOK_TILDE)) {
    return PIE_PARSE_NO_MATCH;
  }

  PieExpr *inner = api->parse_expr(parser);
  if (!inner) {
    return PIE_PARSE_ERROR;
  }

  PieExpr *expr = pie_expr_unary('~', inner);
  if (!expr) {
    pie_expr_free(inner);
    pie_diag_error(api->diag(parser),
                   "out of memory while building bitwise NOT expression");
    return PIE_PARSE_ERROR;
  }

  *out_expr = expr;
  return PIE_PARSE_OK;
}
