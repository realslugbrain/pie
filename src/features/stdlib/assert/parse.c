#include "pie/core/parser/parser.h"

#include <stdlib.h>
#include <string.h>

PieParseResult pie_feature_assert_parse_stmt(PieParseContext *ctx,
                                             PieProgram *program) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (api->match(parser, PIE_TOK_ASSERT_EQ)) {
    if (!api->expect(parser, PIE_TOK_LPAREN, "expected '(' after assert_eq")) {
      return PIE_PARSE_ERROR;
    }

    PieExpr *left = api->parse_expr(parser);
    if (!left) {
      return PIE_PARSE_ERROR;
    }

    if (!api->expect(parser, PIE_TOK_COMMA,
                     "expected ',' between assert_eq arguments")) {
      pie_expr_free(left);
      return PIE_PARSE_ERROR;
    }

    PieExpr *right = api->parse_expr(parser);
    if (!right) {
      pie_expr_free(left);
      return PIE_PARSE_ERROR;
    }

    if (!api->expect(parser, PIE_TOK_RPAREN,
                     "expected ')' after assert_eq arguments")) {
      pie_expr_free(left);
      pie_expr_free(right);
      return PIE_PARSE_ERROR;
    }

    PieStmt stmt;
    memset(&stmt, 0, sizeof(stmt));
    stmt.kind = PIE_STMT_ASSERT_EQ;
    stmt.target = left;
    stmt.expr = right;

    if (!pie_program_push_stmt(program, stmt)) {
      pie_expr_free(left);
      pie_expr_free(right);
      pie_diag_error(api->diag(parser),
                     "out of memory while storing assert_eq statement");
      return PIE_PARSE_ERROR;
    }
    return PIE_PARSE_OK;
  }

  if (!api->match(parser, PIE_TOK_ASSERT)) {
    return PIE_PARSE_NO_MATCH;
  }

  if (!api->expect(parser, PIE_TOK_LPAREN, "expected '(' after assert")) {
    return PIE_PARSE_ERROR;
  }

  PieExpr *cond = api->parse_expr(parser);
  if (!cond) {
    return PIE_PARSE_ERROR;
  }

  if (!api->expect(parser, PIE_TOK_RPAREN,
                   "expected ')' after assert condition")) {
    pie_expr_free(cond);
    return PIE_PARSE_ERROR;
  }

  PieStmt stmt;
  memset(&stmt, 0, sizeof(stmt));
  stmt.kind = PIE_STMT_ASSERT;
  stmt.target = cond;

  if (!pie_program_push_stmt(program, stmt)) {
    pie_expr_free(cond);
    pie_diag_error(api->diag(parser),
                   "out of memory while storing assert statement");
    return PIE_PARSE_ERROR;
  }
  return PIE_PARSE_OK;
}
