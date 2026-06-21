#include "pie/core/parser/parser.h"

#include <stdlib.h>
#include <string.h>

static void free_body(PieProgram *program) {
  if (!program) {
    return;
  }
  pie_program_free(program);
  free(program);
}

static PieProgram *new_body(PieParseContext *ctx) {
  PieProgram *program = (PieProgram *)malloc(sizeof(PieProgram));
  if (!program) {
    pie_diag_error(ctx->api->diag(ctx->parser),
                   "out of memory while parsing unsafe body");
    return NULL;
  }
  pie_program_init(program);
  return program;
}

static PieParseResult parse_body(PieParseContext *ctx, PieProgram *body) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  for (;;) {
    api->skip_separators(parser);
    if (api->check(parser, PIE_TOK_EOF) || api->check(parser, PIE_TOK_END)) {
      return PIE_PARSE_OK;
    }
    if (!api->parse_statement(parser, body)) {
      return PIE_PARSE_ERROR;
    }
  }
}

PieParseResult pie_feature_unsafe_parse_stmt(PieParseContext *ctx,
                                             PieProgram *program) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->match(parser, PIE_TOK_UNSAFE)) {
    return PIE_PARSE_NO_MATCH;
  }
  if (!api->expect(parser, PIE_TOK_COLON, "expected ':' after unsafe")) {
    return PIE_PARSE_ERROR;
  }

  PieProgram *body = new_body(ctx);
  if (!body) {
    return PIE_PARSE_ERROR;
  }
  if (parse_body(ctx, body) != PIE_PARSE_OK) {
    free_body(body);
    return PIE_PARSE_ERROR;
  }

  api->skip_separators(parser);
  if (!api->expect(parser, PIE_TOK_END, "expected 'end' after unsafe block")) {
    free_body(body);
    return PIE_PARSE_ERROR;
  }

  PieStmt stmt;
  memset(&stmt, 0, sizeof(stmt));
  stmt.kind = PIE_STMT_UNSAFE;
  stmt.then_branch = body;
  if (!pie_program_push_stmt(program, stmt)) {
    free_body(body);
    pie_diag_error(api->diag(parser),
                   "out of memory while storing unsafe statement");
    return PIE_PARSE_ERROR;
  }
  return PIE_PARSE_OK;
}

PieParseResult pie_feature_unsafe_parse_raw_store_stmt(PieParseContext *ctx,
                                                       PieProgram *program) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  size_t start = api->pos(parser);
  if (!api->match(parser, PIE_TOK_STAR)) {
    return PIE_PARSE_NO_MATCH;
  }

  PieExpr *target = api->parse_expr_prec(parser, 30);
  if (!target) {
    return PIE_PARSE_ERROR;
  }

  if (!api->match(parser, PIE_TOK_EQ)) {
    pie_expr_free(target);
    api->set_pos(parser, start);
    return PIE_PARSE_NO_MATCH;
  }

  size_t stmt_end = api->find_stmt_end(parser, api->pos(parser));
  PieExpr *value = api->parse_expr_until(parser, stmt_end);
  if (!value) {
    pie_expr_free(target);
    return PIE_PARSE_ERROR;
  }

  PieStmt stmt;
  memset(&stmt, 0, sizeof(stmt));
  stmt.kind = PIE_STMT_RAW_STORE;
  stmt.target = target;
  stmt.expr = value;
  strncpy(stmt.assign_op, "=", sizeof(stmt.assign_op) - 1);
  if (!pie_program_push_stmt(program, stmt)) {
    pie_expr_free(target);
    pie_expr_free(value);
    pie_diag_error(api->diag(parser),
                   "out of memory while storing raw pointer store");
    return PIE_PARSE_ERROR;
  }
  return PIE_PARSE_OK;
}

PieParseResult pie_feature_unsafe_parse_raw_addr_expr(PieParseContext *ctx,
                                                      PieExpr **out_expr) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  size_t start = api->pos(parser);
  if (!api->match(parser, PIE_TOK_AMP)) {
    return PIE_PARSE_NO_MATCH;
  }
  if (!api->match(parser, PIE_TOK_RAW)) {
    api->set_pos(parser, start);
    return PIE_PARSE_NO_MATCH;
  }

  PieExpr *inner = api->parse_expr_prec(parser, 30);
  if (!inner) {
    return PIE_PARSE_ERROR;
  }

  *out_expr = pie_expr_unary_op("&raw", inner);
  if (!*out_expr) {
    pie_expr_free(inner);
    pie_diag_error(api->diag(parser),
                   "out of memory while building raw address expression");
    return PIE_PARSE_ERROR;
  }
  return PIE_PARSE_OK;
}

PieParseResult pie_feature_unsafe_parse_raw_deref_expr(PieParseContext *ctx,
                                                       PieExpr **out_expr) {
  const PieParserApi *api = ctx->api;
  PieParser *parser = ctx->parser;

  if (!api->match(parser, PIE_TOK_STAR)) {
    return PIE_PARSE_NO_MATCH;
  }

  PieExpr *inner = api->parse_expr_prec(parser, 30);
  if (!inner) {
    return PIE_PARSE_ERROR;
  }

  *out_expr = pie_expr_unary_op("*raw", inner);
  if (!*out_expr) {
    pie_expr_free(inner);
    pie_diag_error(
        api->diag(parser),
        "out of memory while building raw pointer dereference expression");
    return PIE_PARSE_ERROR;
  }
  return PIE_PARSE_OK;
}
