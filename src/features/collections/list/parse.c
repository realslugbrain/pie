#include "pie/core/parser/parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PieParseResult pie_feature_list_parse_literal(PieParseContext *ctx,
                                              PieExpr **out_expr) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->match(parser, PIE_TOK_LBRACKET)) {
    return PIE_PARSE_NO_MATCH;
  }

  PieExpr *list = pie_expr_list(0);
  if (!list) {
    pie_diag_error(api->diag(parser),
                   "out of memory while building list literal");
    return PIE_PARSE_ERROR;
  }

  if (api->check(parser, PIE_TOK_RBRACKET)) {
    api->advance(parser);
    *out_expr = list;
    return PIE_PARSE_OK;
  }

  for (;;) {
    PieExpr *elem = api->parse_expr(parser);
    if (!elem) {
      pie_expr_free(list);
      return PIE_PARSE_ERROR;
    }
    if (!pie_expr_list_add_element(list, elem)) {
      pie_expr_free(elem);
      pie_expr_free(list);
      pie_diag_error(api->diag(parser),
                     "out of memory while adding list element");
      return PIE_PARSE_ERROR;
    }
    if (!api->match(parser, PIE_TOK_COMMA)) {
      break;
    }
  }

  if (!api->expect(parser, PIE_TOK_RBRACKET,
                   "expected ']' after list elements")) {
    pie_expr_free(list);
    return PIE_PARSE_ERROR;
  }

  *out_expr = list;
  return PIE_PARSE_OK;
}

PieParseResult pie_feature_list_parse_index(PieParseContext *ctx,
                                            PieExpr **left,
                                            int min_precedence) {
  (void)min_precedence;
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->match(parser, PIE_TOK_LBRACKET)) {
    return PIE_PARSE_NO_MATCH;
  }

  PieExpr *index = api->parse_expr(parser);
  if (!index) {
    return PIE_PARSE_ERROR;
  }

  if (!api->expect(parser, PIE_TOK_RBRACKET,
                   "expected ']' after index expression")) {
    pie_expr_free(index);
    return PIE_PARSE_ERROR;
  }

  PieExpr *expr = pie_expr_index(*left, index);
  if (!expr) {
    pie_expr_free(index);
    pie_diag_error(api->diag(parser),
                   "out of memory while building list index");
    return PIE_PARSE_ERROR;
  }

  *left = expr;
  return PIE_PARSE_OK;
}

PieParseResult pie_feature_list_parse_index_assign(PieParseContext *ctx,
                                                   PieProgram *program) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  size_t start = api->pos(parser);

  if (!api->check(parser, PIE_TOK_IDENTIFIER)) {
    return PIE_PARSE_NO_MATCH;
  }
  const PieToken *name_token = api->peek(parser);
  api->advance(parser);

  if (!api->check(parser, PIE_TOK_LBRACKET)) {
    api->set_pos(parser, start);
    return PIE_PARSE_NO_MATCH;
  }
  api->advance(parser);

  PieExpr *index = api->parse_expr(parser);
  if (!index) {
    api->set_pos(parser, start);
    return PIE_PARSE_NO_MATCH;
  }

  if (!api->check(parser, PIE_TOK_RBRACKET)) {
    pie_expr_free(index);
    api->set_pos(parser, start);
    return PIE_PARSE_NO_MATCH;
  }
  api->advance(parser);

  api->skip_separators(parser);

  if (!api->check(parser, PIE_TOK_EQ) && !api->check(parser, PIE_TOK_PLUS_EQ) &&
      !api->check(parser, PIE_TOK_MINUS_EQ) &&
      !api->check(parser, PIE_TOK_STAR_EQ) &&
      !api->check(parser, PIE_TOK_SLASH_EQ) &&
      !api->check(parser, PIE_TOK_PERCENT_EQ)) {
    pie_expr_free(index);
    api->set_pos(parser, start);
    return PIE_PARSE_NO_MATCH;
  }
  const PieToken *op_token = api->advance(parser);
  char assign_op[4] = {0};
  if (op_token->len < 4) {
    memcpy(assign_op, op_token->start, op_token->len);
  }

  api->skip_separators(parser);

  size_t stmt_end = api->find_stmt_end(parser, api->pos(parser));
  PieExpr *value = api->parse_expr_until(parser, stmt_end);
  if (!value) {
    pie_expr_free(index);
    return PIE_PARSE_ERROR;
  }

  char *target_name = api->copy_token_text(name_token);
  PieExpr *target = pie_expr_var(target_name);
  free(target_name);

  PieStmt stmt;
  memset(&stmt, 0, sizeof(stmt));
  stmt.kind = PIE_STMT_INDEX_ASSIGN;
  stmt.index_target = target;
  stmt.index_expr = index;
  stmt.expr = value;
  strncpy(stmt.assign_op, assign_op, sizeof(stmt.assign_op) - 1);
  if (!pie_program_push_stmt(program, stmt)) {
    pie_expr_free(target);
    pie_expr_free(index);
    pie_expr_free(value);
    pie_diag_error(api->diag(parser), "out of memory");
    return PIE_PARSE_ERROR;
  }
  return PIE_PARSE_OK;
}
